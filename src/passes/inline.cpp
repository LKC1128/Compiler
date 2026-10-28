#include "passes/middle_end.hpp"
#include "ir/clone.hpp"
#include "analysis/cfg.hpp"
#include <unordered_set>
#include <unordered_map>
namespace sysy {
namespace {
bool recursive(Function* root) {
    std::unordered_set<Function*> seen;std::vector<Function*> work{root};
    while(!work.empty()) {
        auto* f=work.back();work.pop_back();if(!seen.insert(f).second) continue;
        for(const auto& b:f->blocks()) for(const auto& i:b->instructions()) if(auto* c=dynamic_cast<CallInst*>(i.get())) {
            if(c->callee()==root) return true;
            if(!c->callee()->is_declaration()) work.push_back(c->callee());
        }
    }
    return false;
}
size_t size(Function& f) {size_t n=0;for(auto& b:f.blocks())n+=b->instructions().size();return n;}
bool eligible(Function& f) {
    if(f.is_declaration() || f.function_type()->is_variadic() || f.blocks().size()>8 || size(f)>80 || recursive(&f)) return false;
    for(auto& b:f.blocks()) for(auto& i:b->instructions()) if(auto* a=dynamic_cast<AllocaInst*>(i.get()))
        if(a->allocated_type()->size_in_bytes()>256) return false;
    return true;
}
void inline_call(CallInst* call,Function& caller,TypeContext& ctx) {
    Function& callee=*call->callee();auto* start=call->parent();
    std::unordered_map<const Value*,Value*> map;
    for(size_t k=0;k<callee.args().size();++k) map[callee.get_arg(k)]=call->arg(k);
    auto* continuation=caller.create_block(ctx,"inline.cont");
    auto& list=start->instructions();auto it=list.begin();while(it->get()!=call) ++it;
    ++it;
    while(it!=list.end()) {auto* i=it->get();++it;continuation->append_instruction(start->remove_instruction(i));}
    // The original outgoing edges now originate in the continuation block.
    for(auto& b:caller.blocks()) for(auto& i:b->instructions()) if(auto* p=dynamic_cast<PhiInst*>(i.get()))
        for(size_t k=0;k<p->num_incoming();++k) if(p->incoming_block(k)==start) p->set_incoming_block(k,continuation);
    for(auto& b:callee.blocks()) map[b.get()]=caller.create_block(ctx,"inline."+callee.name());
    std::vector<std::pair<const Instruction*,Instruction*>> clones;
    for(auto& b:callee.blocks()) {
        auto* dest=static_cast<BasicBlock*>(map[b.get()]);
        for(auto& i:b->instructions()) {
            if(dynamic_cast<ReturnInst*>(i.get())) continue;
            auto n=clone_instruction(*i,ctx);auto* ptr=n.get();map[i.get()]=ptr;
            // Fixed-size local objects have function lifetime in this backend.
            if(i->is_alloca()) caller.get_entry_block()->insert_front(std::move(n));
            else dest->append_instruction(std::move(n));
            clones.push_back({i.get(),ptr});
        }
    }
    auto mapped=[&](Value* v) {auto p=map.find(v);return p==map.end()?v:p->second;};
    for(auto [old,n]:clones) for(size_t k=0;k<old->num_operands();++k) n->set_operand(k,mapped(old->get_operand(k)));
    std::vector<std::pair<Value*,BasicBlock*>> returns;
    for(auto& b:callee.blocks()) if(auto* ret=dynamic_cast<ReturnInst*>(b->get_terminator())) {
        auto* dest=static_cast<BasicBlock*>(map[b.get()]);
        if(ret->has_return_value()) returns.push_back({mapped(ret->return_value()),dest});
        dest->append_instruction(std::make_unique<BranchInst>(continuation,ctx));
    }
    if(!call->type()->is_void()) {
        Value* result=UndefValue::get(call->type());
        if(returns.size()==1) result=returns.front().first;
        else if(!returns.empty()) {
            auto p=std::make_unique<PhiInst>(call->type());
            for(auto [v,b]:returns) p->add_incoming(v,b);
            result=continuation->insert_front(std::move(p));
        }
        call->replace_all_uses_with(result);
    }
    start->erase_instruction(call);
    start->append_instruction(std::make_unique<BranchInst>(static_cast<BasicBlock*>(map[callee.get_entry_block()]),ctx));
    CFGAnalysis::eliminate_unreachable_blocks(caller);
}
}
bool InlinePass::run_on_module(Module& m) {
    bool changed=false;size_t budget=2000;
    for(auto& f:m.functions()) if(!f->is_declaration()) {
        // Snapshot: newly introduced calls wait for a later explicit phase.
        std::vector<CallInst*> calls;
        for(auto& b:f->blocks()) for(auto& i:b->instructions()) if(auto* c=dynamic_cast<CallInst*>(i.get()))
            if(c->callee()!=f.get() && eligible(*c->callee())) calls.push_back(c);
        for(auto* call:calls) {
            // Earlier inlining may remove unreachable call sites; never retain
            // raw pointers across CFG deletion without checking membership.
            bool exists=false;for(auto& b:f->blocks()) for(auto& i:b->instructions()) exists |= i.get()==call;
            if(!exists) continue;
            auto cost=size(*call->callee());if(cost>budget || !eligible(*call->callee())) continue;
            budget-=cost;inline_call(call,*f,m.type_context());changed=true;
        }
    }
    return changed;
}
}
