#include "backend/location_assignment.hpp"
#include "backend/arm64_abi.hpp"
#include "analysis/cfg.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <map>
#include <unordered_set>
#include <stdexcept>
namespace sysy {
namespace {
using Values=std::unordered_set<const Value*>;
bool local(const Value* v) {
    if(dynamic_cast<const Argument*>(v)) return true;
    auto* i=dynamic_cast<const Instruction*>(v);return i && !i->type()->is_void() && !i->is_alloca();
}
struct Interval {const Value* value;size_t first,last;};
std::vector<Interval> intervals(const Function& fn) {
    // Post-phi SSA still supplies exact def/use edges. Extend intervals across
    // complete blocks using a backwards fixed-point liveness computation.
    std::unordered_map<const BasicBlock*,Values> use,def,in,out;
    std::unordered_map<const BasicBlock*,std::pair<size_t,size_t>> positions;
    std::unordered_map<const Value*,std::pair<size_t,size_t>> spans;
    for(auto& a:fn.args()) spans[a.get()]={0,0};
    size_t pos=1;
    for(const auto& b:fn.blocks()) {
        size_t begin=pos;
        for(const auto& i:b->instructions()) {
            for(size_t k=0;k<i->num_operands();++k) {
                const Value* v=i->get_operand(k);if(!local(v)) continue;
                if(!def[b.get()].count(v)) use[b.get()].insert(v);
            }
            if(local(i.get())) {def[b.get()].insert(i.get());spans[i.get()]={pos,pos};}
            ++pos;
        }
        positions[b.get()]={begin,pos};++pos;
    }
    bool changed=true;
    while(changed) {changed=false;
        for(auto it=fn.blocks().rbegin();it!=fn.blocks().rend();++it) {
            const auto* b=it->get();Values next;
            for(auto* s:b->successors()) next.insert(in[s].begin(),in[s].end());
            Values live=use[b];for(auto* v:next)if(!def[b].count(v))live.insert(v);
            if(live!=in[b] || next!=out[b]) {in[b]=std::move(live);out[b]=std::move(next);changed=true;}
        }
    }
    auto extend=[&](const Value* v,size_t p) {auto& s=spans.at(v);s.first=std::min(s.first,p);s.second=std::max(s.second,p);};
    pos=1;
    for(const auto& b:fn.blocks()) {
        for(auto* v:in[b.get()]) extend(v,positions[b.get()].first);
        for(auto* v:out[b.get()]) extend(v,positions[b.get()].second);
        for(const auto& i:b->instructions()) {
            for(size_t k=0;k<i->num_operands();++k)if(local(i->get_operand(k)))extend(i->get_operand(k),pos);
            ++pos;
        }
        ++pos;
    }
    std::vector<Interval> result;
    // Stable ordering for equal starts, independent of hash iteration/ASLR.
    for(const auto& a:fn.args()) {auto s=spans.at(a.get());result.push_back({a.get(),s.first,s.second});}
    for(const auto& b:fn.blocks()) for(const auto& i:b->instructions()) if(local(i.get())) {
        auto s=spans.at(i.get());result.push_back({i.get(),s.first,s.second});
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    return result;
}
std::vector<std::string> pool(bool fp) {
    std::vector<std::string> result;
    for(int k=fp?8:19;k<=(fp?15:28);++k)result.push_back((fp?"v":"x")+std::to_string(k));
    return result;
}
}
void assign_machine_locations(Function& fn,bool registers) {
    auto& layout=fn.machine_layout();layout=MachineLayout{};
    auto live=intervals(fn);std::set<std::string> saved;
    if(registers) for(bool fp:{false,true}) {
        std::vector<std::pair<Interval,std::string>> active;auto regs=pool(fp);
        for(const auto& interval:live) {
            if(interval.value->type()->is_float()!=fp) continue;
            active.erase(std::remove_if(active.begin(),active.end(),[&](const auto& a){return a.first.last<interval.first;}),active.end());
            for(const auto& reg:regs) {
                bool used=false;for(const auto& a:active)used |= a.second==reg;
                if(!used) {layout.values[interval.value]={reg,0,8,false};saved.insert(reg);active.push_back({interval,reg});break;}
            }
        }
    }
    int64_t cursor=-16;
    auto reserve=[&](size_t bytes) {
        if(bytes>size_t(std::numeric_limits<int>::max())-32)throw std::runtime_error("Stack object exceeds ARM64 frame limit");
        cursor-=int64_t(std::max<size_t>(bytes,4));cursor &= ~int64_t(7);
        if(cursor< -int64_t(std::numeric_limits<int>::max())+32)throw std::runtime_error("Stack frame exceeds ARM64 offset range");
        return int(cursor);
    };
    for(const auto& reg:saved) layout.saved_registers.push_back({reg,reserve(8)});
    for(const auto& a:fn.args()) if(!layout.values.count(a.get())) layout.values[a.get()]={"",reserve(8),8,false};
    for(const auto& b:fn.blocks()) for(const auto& i:b->instructions()) {
        if(auto* a=dynamic_cast<const AllocaInst*>(i.get())) {
            size_t bytes=std::max<size_t>(a->allocated_type()->size_in_bytes(),4);
            layout.values[a]={"",reserve(bytes),(bytes+7)&~size_t(7),true};
        } else if(local(i.get()) && !layout.values.count(i.get()))layout.values[i.get()]={"",reserve(8),8,false};
        if(auto* call=dynamic_cast<const CallInst*>(i.get())) {
            ArgAllocator allocator;
            for(size_t k=0;k<call->num_args();++k)allocator.next(call->arg(k)->type(),!call->callee()->function_type()->is_variadic() || k<call->callee()->function_type()->param_types().size());
            layout.outgoing_bytes=std::max(layout.outgoing_bytes,allocator.stack_bytes());
        }
    }
    int64_t total=-cursor+layout.outgoing_bytes;
    if(total>std::numeric_limits<int>::max()-15)throw std::runtime_error("Stack frame exceeds ARM64 offset range");
    layout.frame_size=std::max(32,int((total+15)&~int64_t(15)));
}
bool verify_machine_layout(const Function& fn,std::string& error) {
    const auto& l=fn.machine_layout();
    auto fail=[&](const std::string& s){error=s;return false;};
    if(l.frame_size<32 || l.frame_size%16 || l.outgoing_bytes<0 || l.outgoing_bytes%8) return fail("Invalid LIR frame alignment");
    auto live=intervals(fn);std::map<std::string,std::vector<Interval>> allocated;
    std::set<std::string> saves;std::vector<std::pair<int64_t,int64_t>> slots{{-16,0}};
    for(const auto& s:l.saved_registers) {
        auto allowed=pool(false);auto fp=pool(true);allowed.insert(allowed.end(),fp.begin(),fp.end());
        if(std::find(allowed.begin(),allowed.end(),s.first)==allowed.end() || !saves.insert(s.first).second || s.second%8 || s.second>-24 || int64_t(s.second)< -int64_t(l.frame_size)+l.outgoing_bytes)
            return fail("Invalid saved register or frame slot");
        slots.push_back({s.second,int64_t(s.second)+8});
    }
    auto check=[&](const Value* v,bool object) {
        auto it=l.values.find(v);if(it==l.values.end())return false;
        const auto& loc=it->second;if(loc.object!=object)return false;
        if(!loc.reg.empty()) {
            if(object || !saves.count(loc.reg))return false;
            auto regs=pool(v->type()->is_float());return std::find(regs.begin(),regs.end(),loc.reg)!=regs.end();
        }
        size_t needed=object ? std::max<size_t>(static_cast<const AllocaInst*>(v)->allocated_type()->size_in_bytes(),4) : 8;
        if(loc.bytes<needed || loc.bytes>size_t(std::numeric_limits<int>::max())) return false;
        if(loc.offset%8 || loc.offset>=-16 || loc.bytes==0 || int64_t(loc.offset)< -int64_t(l.frame_size)+l.outgoing_bytes || int64_t(loc.offset)+int64_t(loc.bytes)>-16)return false;
        slots.push_back({loc.offset,int64_t(loc.offset)+int64_t(loc.bytes)});return true;
    };
    for(auto& a:fn.args())if(!check(a.get(),false))return fail("Missing/invalid argument location");
    for(auto& b:fn.blocks())for(auto& i:b->instructions())if(local(i.get()) || i->is_alloca())
        if(!check(i.get(),i->is_alloca()))return fail("Missing/invalid instruction location");
    std::sort(slots.begin(),slots.end());for(size_t k=1;k<slots.size();++k)if(slots[k].first<slots[k-1].second)return fail("Overlapping stack objects or saved registers");
    for(const auto& i:live) {auto it=l.values.find(i.value);if(it!=l.values.end() && !it->second.reg.empty())allocated[it->second.reg].push_back(i);}
    for(auto& pair:allocated) {size_t end=0;bool first=true;for(const auto& i:pair.second) {
        if(!first && i.first<=end)return fail("Interfering values share register "+pair.first);end=i.last;first=false;
    }}
    return true;
}
}
