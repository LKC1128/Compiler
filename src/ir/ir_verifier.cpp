#include "ir/ir_verifier.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include "backend/location_assignment.hpp"

namespace sysy {
namespace {
using Blocks = std::unordered_set<const BasicBlock*>;
bool integer(Type* t, unsigned bits) { return t && t->is_integer() && static_cast<IntegerType*>(t)->bits() == bits; }
}
void IRVerifier::report_error(const std::string& msg) {
    if (!err_.empty()) err_ += "\n";
    err_ += "[IR Verifier Error] " + msg;
}
bool IRVerifier::verify(const Module& mod, std::string& msg) {
    err_.clear();
    check_uses(mod);
    if (err_.empty()) for (const auto& fn : mod.functions()) if (!fn->is_declaration()) check_function(*fn);
    msg=err_; return err_.empty();
}
bool IRVerifier::verify(const Function& fn, std::string& msg) {
    err_.clear(); check_function(fn); msg=err_; return err_.empty();
}
bool IRVerifier::verify(const BasicBlock& bb, std::string& msg) {
    err_.clear();
    if (bb.parent()) check_function(*bb.parent());
    else report_error("Basic block has no function");
    msg=err_; return err_.empty();
}
bool IRVerifier::verify(const Instruction& inst, std::string& msg) {
    err_.clear(); check_instruction(inst); msg=err_; return err_.empty();
}
void IRVerifier::check_uses(const Module& mod) {
    std::unordered_set<const Value*> values;
    std::unordered_set<const Use*> uses;
    std::vector<const User*> users;
    auto add=[&](const Value* v) { values.insert(v); if (auto* u=dynamic_cast<const User*>(v)) users.push_back(u); };
    for (const auto& v:mod.type_context().constants()) add(v.get());
    for (const auto& v:mod.globals()) add(v.get());
    for (const auto& f:mod.functions()) {
        add(f.get()); for (const auto& a:f->args()) add(a.get());
        for (const auto& b:f->blocks()) { add(b.get()); for (const auto& i:b->instructions()) add(i.get()); }
    }
    std::unordered_map<const Use*,size_t> reverse_count;
    for (auto* v:values) for (auto* edge:v->uses()) ++reverse_count[edge];
    for (auto* u:users) {
        size_t index=0;
        for (const auto& edge:u->operand_uses()) {
            auto* v=edge->value(); uses.insert(edge.get());
            if (!v || !values.count(v)) { report_error("Operand references a null, foreign, or deleted value"); ++index; continue; }
            if (edge->user()!=u || edge->operand_index()!=index || reverse_count[edge.get()]!=1)
                report_error("Inconsistent operand/def-use edge");
            ++index;
        }
    }
    for (auto* v:values) {
        std::unordered_set<const Use*> seen;
        for (auto* edge:v->uses()) {
            if (!uses.count(edge)) { report_error("Def-use list contains an unknown use"); continue; }
            if (!seen.insert(edge).second || edge->value()!=v) report_error("Inconsistent reverse def-use edge");
        }
    }
}
void IRVerifier::check_instruction(const Instruction& i) {
    for (size_t k=0;k<i.num_operands();++k) if (!i.get_operand(k) || !i.get_operand(k)->type()) {
        report_error("Null operand or type"); return;
    }
    if (!i.type()) { report_error("Instruction has no type"); return; }
    auto arity=[&](size_t n) { if (i.num_operands()!=n) {report_error("Wrong operand count for "+i.get_opcode_name());return false;}return true; };
    auto require=[&](bool ok) {if(!ok) report_error("Invalid types for "+i.get_opcode_name());};
    auto ty=[&](size_t n){return i.get_operand(n)->type();};
    unsigned allowed=0;
    switch(i.opcode()) {
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Shl: allowed=UB_NSW|UB_NUW; break;
    case Opcode::SDiv: case Opcode::LShr: case Opcode::AShr: allowed=UB_EXACT; break;
    case Opcode::GetElementPtr: allowed=UB_INBOUNDS; break;
    default: break;
    }
    if(i.ub_flags() & ~allowed) report_error("Illegal poison flags for "+i.get_opcode_name());
    if (i.is_binary()) {
        if (!arity(2)) return;
        bool fp=!i.is_integer_binary();
        require(ty(0)==ty(1) && i.type()==ty(0) && (fp ? ty(0)->is_float() : integer(ty(0),32)));
        return;
    }
    switch(i.opcode()) {
    case Opcode::FNeg:
        if(arity(1)) { require(ty(0)->is_float() && i.type()==ty(0)); }
        break;
    case Opcode::ICmp:
        if(arity(2)) {
            auto* compare=dynamic_cast<const ICmpInst*>(&i);
            bool equality=compare && (compare->predicate()==ICmpPred::EQ || compare->predicate()==ICmpPred::NE);
            require(ty(0)==ty(1) && integer(i.type(),1) &&
                    (ty(0)->is_integer() || (equality && ty(0)->is_pointer())));
        }
        break;
    case Opcode::FCmp:
        if(arity(2)) require(ty(0)==ty(1) && integer(i.type(),1) && ty(0)->is_float());
        break;
    case Opcode::SIToFP:
        if(arity(1)) { require(integer(ty(0),32) && i.type()->is_float()); }
        break;
    case Opcode::FPToSI:
        if(arity(1)) { require(ty(0)->is_float() && integer(i.type(),32)); }
        break;
    case Opcode::ZExt: case Opcode::SExt: case Opcode::Trunc:
        if(arity(1)) {
            bool ints=ty(0)->is_integer() && i.type()->is_integer();
            require(ints && (i.opcode()==Opcode::Trunc ? static_cast<IntegerType*>(ty(0))->bits()>static_cast<IntegerType*>(i.type())->bits() : static_cast<IntegerType*>(ty(0))->bits()<static_cast<IntegerType*>(i.type())->bits()));
        }
        break;
    case Opcode::FPExt:
        if(arity(1)) { require(ty(0)->is_float() && ty(0)->size_in_bytes()==4 && i.type()->is_float() && i.type()->size_in_bytes()==8); }
        break;
    case Opcode::BitCast:
        if(arity(1)) { require(ty(0)->is_pointer() && i.type()->is_pointer()); }
        break;
    case Opcode::Alloca:
        if(arity(0)) {
            auto* a=dynamic_cast<const AllocaInst*>(&i);
            require(a && i.type()->is_pointer() && static_cast<PointerType*>(i.type())->pointee()==a->allocated_type());
        } break;
    case Opcode::Load:
        if(arity(1)) { require(ty(0)->is_pointer() && static_cast<PointerType*>(ty(0))->pointee()==i.type() && !i.type()->is_array()); }
        break;
    case Opcode::Store:
        if(arity(2)) { require(ty(1)->is_pointer() && static_cast<PointerType*>(ty(1))->pointee()==ty(0) && i.type()->is_void() && !ty(0)->is_array()); }
        break;
    case Opcode::GetElementPtr: {
        if(i.num_operands()<2 || !ty(0)->is_pointer()) {report_error("Invalid GEP base/indices");break;}
        Type* cur=static_cast<PointerType*>(ty(0))->pointee();
        auto* gep=dynamic_cast<const GetElementPtrInst*>(&i);
        require(gep && gep->source_element_type()==cur);
        for(size_t k=1;k<i.num_operands();++k) {
            require(integer(ty(k),32));
            if(k>1) {if(cur->is_array()) cur=static_cast<ArrayType*>(cur)->element_type(); else report_error("Excess GEP index");}
        }
        require(i.type()->is_pointer() && static_cast<PointerType*>(i.type())->pointee()==cur);
        break;
    }
    case Opcode::Branch:
        if(arity(1)) { require(dynamic_cast<BasicBlock*>(i.get_operand(0)) && i.type()->is_void()); }
        break;
    case Opcode::CondBranch:
        if(arity(3)) { require(integer(ty(0),1) && dynamic_cast<BasicBlock*>(i.get_operand(1)) && dynamic_cast<BasicBlock*>(i.get_operand(2)) && i.type()->is_void()); }
        break;
    case Opcode::Return: {
        auto* f=i.parent() ? i.parent()->parent() : nullptr;
        if(!f) {report_error("Return has no function");break;}
        if(f->return_type()->is_void()) require(i.num_operands()==0);
        else if(arity(1)) require(ty(0)==f->return_type());
        break;
    }
    case Opcode::Call: {
        if(i.num_operands()==0) {report_error("Call has no callee");break;}
        auto* f=dynamic_cast<Function*>(i.get_operand(0));
        if(!f) {report_error("Call callee is not a function");break;}
        const auto& ps=f->function_type()->param_types();
        require((i.num_operands()==ps.size()+1 || (f->function_type()->is_variadic() && i.num_operands()>ps.size())) && i.type()==f->return_type());
        for(size_t k=0;k<ps.size() && k+1<i.num_operands();++k) require(ps[k]==ty(k+1));
        break;
    }
    case Opcode::Phi:
        require(i.num_operands()%2==0 && !i.type()->is_void());
        for(size_t k=0;k+1<i.num_operands();k+=2) require(ty(k)==i.type() && dynamic_cast<BasicBlock*>(i.get_operand(k+1)));
        break;
    default: report_error("Unhandled instruction in verifier"); break;
    }
}
void IRVerifier::check_function(const Function& fn) {
    if(fn.blocks().empty()) {report_error("Function has no blocks");return;}
    if(fn.level()==IRLevel::HIR) {
        if(!(fn.state_flags()&SF_STRUCTURED) || fn.regions().empty()) report_error("HIR has no structured source regions");
        for(size_t r=0;r<fn.regions().size();++r) {
            const auto& region=fn.regions()[r];
            auto owned=[&](const BasicBlock* b) { for(const auto& x:fn.blocks()) if(x.get()==b) return true; return false; };
            if(region.parent>=static_cast<int>(r) || region.parent< -1 || !owned(region.entry) || (region.exit && !owned(region.exit)))
                report_error("Invalid structured region tree");
        }
    } else if(!fn.regions().empty() || (fn.state_flags()&SF_STRUCTURED)) report_error("Structured metadata survived HIR lowering");
    if(fn.level()==IRLevel::LIR && ((fn.state_flags()&SF_SSA) || !(fn.state_flags()&SF_REGALLOC))) report_error("LIR location assignment missing");
    if(fn.level()!=IRLevel::LIR && (fn.state_flags()&SF_REGALLOC)) report_error("Register allocation marked before LIR");
    Blocks all;
    std::unordered_map<const BasicBlock*,Blocks> succ,pred;
    std::unordered_map<const Instruction*,size_t> order;
    for(const auto& b:fn.blocks()) all.insert(b.get());
    size_t before=err_.size();
    for(const auto& b:fn.blocks()) {
        if(b->parent()!=&fn || !b->get_terminator()) report_error("Block parent/terminator missing");
        bool term=false,nonphi=false;size_t idx=0;
        for(const auto& i:b->instructions()) {
            order[i.get()]=idx++;
            if(i->level()!=fn.level() || i->state_flags()!=fn.state_flags()) report_error("Instruction/function representation mismatch");
            if(fn.level()==IRLevel::LIR && i->is_phi()) report_error("Phi survived LIR lowering");
            if(i->region_id()>=static_cast<int>(fn.regions().size()) || i->region_id()< -1) report_error("Invalid instruction region");
            if(i->parent()!=b.get() || term) report_error("Wrong instruction parent or instruction after terminator");
            if(i->is_phi() && nonphi) report_error("Phi after non-phi");
            if(!i->is_phi()) nonphi=true;
            term |= i->is_terminator();check_instruction(*i);
        }
    }
    if(err_.size()!=before) return;
    for(const auto& b:fn.blocks()) if(auto* br=dynamic_cast<BranchInst*>(b->get_terminator())) {
        auto edge=[&](BasicBlock* target) {
            if(!all.count(target)) report_error("Branch target outside function");
            else {succ[b.get()].insert(target);pred[target].insert(b.get());}
        };
        if(br->is_conditional()) {edge(br->true_target());edge(br->false_target());} else edge(br->target());
    }
    for(const auto& b:fn.blocks()) {
        Blocks s(b->successors().begin(),b->successors().end()),p(b->predecessors().begin(),b->predecessors().end());
        if(s!=succ[b.get()] || p!=pred[b.get()] || s.size()!=b->successors().size() || p.size()!=b->predecessors().size()) report_error("CFG does not match terminator edges");
    }
    if(!pred[fn.get_entry_block()].empty()) report_error("Entry block must not have predecessors");
    if(err_.size()!=before) return;
    Blocks reachable{fn.get_entry_block()};std::queue<const BasicBlock*> work;work.push(fn.get_entry_block());
    while(!work.empty()) {auto*b=work.front();work.pop();for(auto*s:succ[b])if(reachable.insert(s).second)work.push(s);}
    // Compute idoms on the already validated CFG, without mutating IR. Full
    // dominator sets are quadratic in space on long chains of conditional blocks.
    std::vector<const BasicBlock*> postorder;
    std::unordered_set<const BasicBlock*> seen;
    std::vector<std::pair<const BasicBlock*,bool>> dfs{{fn.get_entry_block(),false}};
    while(!dfs.empty()) {
        auto [block,done]=dfs.back();dfs.pop_back();
        if(done) {postorder.push_back(block);continue;}
        if(!seen.insert(block).second) continue;
        dfs.push_back({block,true});
        for(auto* next:succ[block]) if(!seen.count(next)) dfs.push_back({next,false});
    }
    std::reverse(postorder.begin(),postorder.end());
    std::unordered_map<const BasicBlock*,size_t> rank;
    std::unordered_map<const BasicBlock*,const BasicBlock*> idom;
    for(size_t k=0;k<postorder.size();++k) rank[postorder[k]]=k;
    idom[fn.get_entry_block()]=fn.get_entry_block();
    auto intersect=[&](const BasicBlock* a,const BasicBlock* b) {
        while(a!=b) {
            while(rank[a]>rank[b]) a=idom[a];
            while(rank[b]>rank[a]) b=idom[b];
        }
        return a;
    };
    bool changed=true;
    while(changed) {
        changed=false;
        for(size_t k=1;k<postorder.size();++k) {
            auto* block=postorder[k];const BasicBlock* candidate=nullptr;
            for(auto* previous:pred[block]) if(idom.count(previous))
                candidate=candidate?intersect(candidate,previous):previous;
            if(candidate && (!idom.count(block)||idom[block]!=candidate)) {idom[block]=candidate;changed=true;}
        }
    }
    std::unordered_map<const BasicBlock*,std::vector<const BasicBlock*>> children;
    for(auto* block:postorder) if(block!=fn.get_entry_block()) children[idom[block]].push_back(block);
    std::unordered_map<const BasicBlock*,size_t> enter,leave;
    size_t clock=0;dfs={{fn.get_entry_block(),false}};
    while(!dfs.empty()) {
        auto [block,done]=dfs.back();dfs.pop_back();
        if(done) {leave[block]=clock++;continue;}
        enter[block]=clock++;dfs.push_back({block,true});
        for(auto* child:children[block]) dfs.push_back({child,false});
    }
    auto dominates=[&](const BasicBlock* a,const BasicBlock* b) {
        return enter.count(a) && enter.count(b) && enter[a]<=enter[b] && leave[b]<=leave[a];
    };
    auto available=[&](Value* val,const BasicBlock* use,size_t index) {
        if(auto* def=dynamic_cast<Instruction*>(val)) {
            if(!order.count(def)) {report_error("Definition outside function");return;}
            if(def->parent()==use) {if(order[def]>=index)report_error("Use before definition");}
            else if(reachable.count(use) && !dominates(def->parent(),use)) report_error("Definition does not dominate use");
        } else if(auto* arg=dynamic_cast<Argument*>(val)) {if(arg->parent()!=&fn)report_error("Foreign argument");}
    };
    for(const auto& b:fn.blocks()) for(const auto& i:b->instructions()) {
        if(auto* phi=dynamic_cast<PhiInst*>(i.get())) {
            Blocks incoming;
            for(size_t k=0;k<phi->num_incoming();++k) {
                auto*p=phi->incoming_block(k);
                if(!all.count(p) || !incoming.insert(p).second) {report_error("Duplicate or foreign phi predecessor");continue;}
                available(phi->incoming_value(k),p,p->instructions().size());
            }
            if(incoming!=pred[b.get()]) report_error("Phi does not cover all predecessors exactly once");
        } else for(size_t k=0;k<i->num_operands();++k) available(i->get_operand(k),b.get(),order[i.get()]);
    }
    if(fn.level()==IRLevel::LIR && err_.size()==before) {
        std::string error;if(!verify_machine_layout(fn,error)) report_error(error);
    }
}
} // namespace sysy
