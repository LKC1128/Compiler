#include "passes/middle_end.hpp"
#include "analysis/dominator.hpp"
#include "ir/clone.hpp"
#include <map>
#include <sstream>
namespace sysy {
namespace {
std::string key(const Instruction& i) {
    std::ostringstream s;s<<int(i.opcode())<<':'<<i.type()<<':'<<i.ub_flags();
    if(auto* p=dynamic_cast<const ICmpInst*>(&i)) s<<':'<<int(p->predicate());
    if(auto* p=dynamic_cast<const FCmpInst*>(&i)) s<<':'<<int(p->predicate());
    for(size_t k=0;k<i.num_operands();++k) s<<':'<<i.get_operand(k);
    return s.str();
}
}
bool GVNPass::run_on_module(Module& mod) {
    bool changed=false;
    for(auto& f:mod.functions()) if(!f->is_declaration()) {
        DominatorTree dom(*f);
        std::map<std::string,Instruction*> available;
        struct Visit {BasicBlock* b;bool leave;std::vector<std::string> inserted;};
        std::vector<Visit> stack{{f->get_entry_block(),false,{}}};
        while(!stack.empty()) {
            auto visit=std::move(stack.back());stack.pop_back();
            if(visit.leave) {for(const auto& k:visit.inserted) available.erase(k);continue;}
            std::vector<Instruction*> remove;
            for(auto& owner:visit.b->instructions()) {
                auto* i=owner.get();if(!is_pure_expression(*i)) continue;
                auto k=key(*i);auto it=available.find(k);
                if(it!=available.end()) {i->replace_all_uses_with(it->second);remove.push_back(i);changed=true;}
                else {available.emplace(k,i);visit.inserted.push_back(k);}
            }
            for(auto* i:remove) visit.b->erase_instruction(i);
            stack.push_back({visit.b,true,std::move(visit.inserted)});
            auto& children=dom.get_children(visit.b);
            for(auto it=children.rbegin();it!=children.rend();++it) stack.push_back({*it,false,{}});
        }
    }
    return changed;
}
}
