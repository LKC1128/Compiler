#include "passes/middle_end.hpp"
#include "analysis/loop.hpp"
#include "analysis/cfg.hpp"
#include "ir/clone.hpp"
#include "ir/int_math.hpp"
#include <unordered_map>
#include <limits>
namespace sysy {
bool LICMPass::run_on_module(Module& m) {
    bool changed=false;
    for(auto& f:m.functions()) if(!f->is_declaration()) {
        auto loops=find_natural_loops(*f);
        for(auto& loop:loops) {
            // Find candidates before creating a preheader, so empty loops do
            // not grow CFGs. Only non-trapping, memory-free expressions move.
            std::unordered_set<Instruction*> invariant;
            std::vector<Instruction*> order;bool progress=true;
            while(progress) {progress=false;
                for(auto& b:f->blocks()) if(loop.blocks.count(b.get())) for(auto& owner:b->instructions()) {
                    auto* i=owner.get();if(invariant.count(i) || !is_pure_expression(*i) || i->opcode()==Opcode::SDiv || i->opcode()==Opcode::SRem) continue;
                    bool ok=true;for(size_t k=0;k<i->num_operands();++k) if(auto* def=dynamic_cast<Instruction*>(i->get_operand(k)))
                        if(loop.blocks.count(def->parent()) && !invariant.count(def)) ok=false;
                    if(ok) {invariant.insert(i);order.push_back(i);progress=true;}
                }
            }
            if(order.empty()) continue;
            auto before=f->blocks().size();auto* pre=ensure_preheader(*f,loop,m.type_context());if(!pre) continue;
            changed |= before!=f->blocks().size();
            DominatorTree dom(*f);
            for(auto* i:order) {
                bool ok=true;for(size_t k=0;k<i->num_operands();++k) if(auto* def=dynamic_cast<Instruction*>(i->get_operand(k)))
                    if(!dom.dominates(def->parent(),pre)) ok=false;
                if(ok) {auto n=i->parent()->remove_instruction(i);pre->insert_before(pre->get_terminator(),std::move(n));changed=true;}
            }
        }
    }
    return changed;
}
bool InductionPass::run_on_module(Module& m) {
    bool changed=false;size_t budget=32;auto& ctx=m.type_context();
    for(auto& f:m.functions()) if(!f->is_declaration()) for(auto& loop:find_natural_loops(*f)) {
        if(loop.latches.size()!=1) continue;auto* latch=loop.latches[0];
        std::vector<BinaryInst*> candidates;
        for(auto& b:f->blocks()) if(loop.blocks.count(b.get())) for(auto& owner:b->instructions()) {
            auto* mul=dynamic_cast<BinaryInst*>(owner.get());if(!mul || mul->opcode()!=Opcode::Mul) continue;
            auto* iv=dynamic_cast<PhiInst*>(mul->lhs());auto* factor=dynamic_cast<ConstantInt*>(mul->rhs());
            if(!iv || iv->parent()!=loop.header || iv->num_incoming()!=2 || !factor || !iv->type()->is_integer()) continue;
            auto* next=dynamic_cast<BinaryInst*>(iv->get_incoming_value_for_block(latch));
            if(!next || next->opcode()!=Opcode::Add || next->lhs()!=iv || !dynamic_cast<ConstantInt*>(next->rhs())) continue;
            candidates.push_back(mul);
        }
        for(auto* mul:candidates) {
            if(!budget) break;--budget;
            auto* iv=static_cast<PhiInst*>(mul->lhs());auto* pre=ensure_preheader(*f,loop,ctx);if(!pre) continue;
            auto* init=iv->get_incoming_value_for_block(pre);if(!init) continue;
            auto* next=static_cast<BinaryInst*>(iv->get_incoming_value_for_block(latch));
            int32_t step=static_cast<ConstantInt*>(next->rhs())->value();
            int32_t factor=static_cast<ConstantInt*>(mul->rhs())->value();
            auto* initial=pre->insert_before(pre->get_terminator(),std::make_unique<BinaryInst>(Opcode::Mul,init,ConstantInt::get(ctx,factor)));
            auto p=std::make_unique<PhiInst>(iv->type());auto* recurrence=static_cast<PhiInst*>(loop.header->insert_front(std::move(p)));
            auto* update=latch->insert_before(latch->get_terminator(),std::make_unique<BinaryInst>(Opcode::Add,recurrence,ConstantInt::get(ctx,int_mul(step,factor))));
            recurrence->add_incoming(initial,pre);recurrence->add_incoming(update,latch);
            mul->replace_all_uses_with(recurrence);mul->parent()->erase_instruction(mul);changed=true;
        }
    }
    return changed;
}
bool StrengthReducePass::run_on_module(Module& m) {
    bool changed=false;
    for(auto& f:m.functions()) for(auto& b:f->blocks()) {
        std::vector<Instruction*> remove;
        for(auto& owner:b->instructions()) {
            auto* i=dynamic_cast<BinaryInst*>(owner.get());if(!i || i->opcode()!=Opcode::Mul) continue;
            auto* c=dynamic_cast<ConstantInt*>(i->rhs());if(!c || c->value()<=1) continue;
            uint32_t n=uint32_t(c->value());if(n&(n-1)) continue;
            unsigned shift=0;while((uint32_t(1)<<shift)!=n) ++shift;
            auto replacement=std::make_unique<BinaryInst>(Opcode::Shl,i->lhs(),ConstantInt::get(m.type_context(),int32_t(shift)));
            replacement->set_ub_flags(i->ub_flags());
            auto* v=b->insert_before(i,std::move(replacement));i->replace_all_uses_with(v);remove.push_back(i);changed=true;
        }
        for(auto* i:remove) b->erase_instruction(i);
    }
    return changed;
}
namespace {
bool compare(ICmpPred p,int32_t a,int32_t b) {
    switch(p) {
    case ICmpPred::EQ:return a==b;case ICmpPred::NE:return a!=b;
    case ICmpPred::SLT:return a<b;case ICmpPred::SLE:return a<=b;
    case ICmpPred::SGT:return a>b;case ICmpPred::SGE:return a>=b;
    case ICmpPred::ULT:return uint32_t(a)<uint32_t(b);case ICmpPred::ULE:return uint32_t(a)<=uint32_t(b);
    case ICmpPred::UGT:return uint32_t(a)>uint32_t(b);case ICmpPred::UGE:return uint32_t(a)>=uint32_t(b);
    }return false;
}
}
bool UnrollPass::run_on_module(Module& m) {
    bool changed=false;size_t budget=1024;auto& ctx=m.type_context();
    for(auto& f:m.functions()) if(!f->is_declaration()) {
        // Recompute loop analysis after each successful CFG replacement.
        bool again=true;while(again) {again=false;
            for(auto& loop:find_natural_loops(*f)) {
                if(loop.blocks.size()!=2 || loop.latches.size()!=1 || loop.latches[0]==loop.header) continue;
                auto* header=loop.header;auto* body=loop.latches[0];
                auto* br=dynamic_cast<BranchInst*>(header->get_terminator());auto* back=dynamic_cast<BranchInst*>(body->get_terminator());
                if(!br || !br->is_conditional() || !back || back->is_conditional() || back->target()!=header) continue;
                bool body_true=br->true_target()==body;
                auto* exit=body_true ? br->false_target() : br->true_target();
                if((body_true ? br->true_target() : br->false_target())!=body || loop.blocks.count(exit)) continue;
                auto* cmp=dynamic_cast<ICmpInst*>(br->condition());if(!cmp || cmp->parent()!=header) continue;
                auto* iv=dynamic_cast<PhiInst*>(cmp->lhs());auto* bound=dynamic_cast<ConstantInt*>(cmp->rhs());
                if(!iv || iv->parent()!=header || iv->num_incoming()!=2 || !bound) continue;
                BasicBlock* pre=nullptr;for(auto* p:header->predecessors()) if(!loop.blocks.count(p)) {if(pre){pre=nullptr;break;}pre=p;}
                if(!pre || pre->successors().size()!=1) continue;
                auto* first=dynamic_cast<ConstantInt*>(iv->get_incoming_value_for_block(pre));
                auto* step=dynamic_cast<BinaryInst*>(iv->get_incoming_value_for_block(body));
                if(!first || !step || step->opcode()!=Opcode::Add || step->lhs()!=iv) continue;
                auto* delta=dynamic_cast<ConstantInt*>(step->rhs());if(!delta || !delta->value()) continue;
                int trips=0;int32_t value=first->value();
                while(compare(cmp->predicate(),value,bound->value())==body_true && trips<=8) {value=int_add(value,delta->value());++trips;}
                if(trips>8) continue;
                bool supported=true;
                for(auto& i:body->instructions()) if(i->is_phi() || i->is_alloca()) supported=false;
                for(auto& i:header->instructions()) if(!i->is_phi() && !i->is_terminator() && !is_pure_expression(*i)) supported=false;
                size_t cost=(header->instructions().size()+body->instructions().size())*size_t(trips+1);
                if(!supported || cost>128 || cost>budget) continue;
                budget-=cost;
                std::unordered_map<const Value*,Value*> map;
                auto mapped=[&](Value* v){auto it=map.find(v);return it==map.end()?v:it->second;};
                std::vector<PhiInst*> phis;
                for(auto& i:header->instructions()) if(auto* p=dynamic_cast<PhiInst*>(i.get())) {phis.push_back(p);map[p]=p->get_incoming_value_for_block(pre);}
                auto* expanded=f->create_block(ctx,"loop.unrolled");
                auto clone_block=[&](BasicBlock* b) {
                    for(auto& i:b->instructions()) if(!i->is_phi() && !i->is_terminator()) {
                        auto n=clone_instruction(*i,ctx);for(size_t k=0;k<n->num_operands();++k)n->set_operand(k,mapped(i->get_operand(k)));
                        map[i.get()]=expanded->append_instruction(std::move(n));
                    }
                };
                for(int t=0;t<trips;++t) {
                    clone_block(header);clone_block(body);
                    std::vector<Value*> next;for(auto* p:phis)next.push_back(mapped(p->get_incoming_value_for_block(body)));
                    for(size_t k=0;k<phis.size();++k)map[phis[k]]=next[k];
                }
                clone_block(header);expanded->append_instruction(std::make_unique<BranchInst>(exit,ctx));
                for(auto* b:loop.blocks) for(auto& i:b->instructions()) if(!i->type()->is_void()) {
                    auto uses=i->uses();for(auto* use:uses) if(auto* user=dynamic_cast<Instruction*>(use->user()))
                        if(!loop.blocks.count(user->parent())) user->set_operand(use->operand_index(),mapped(i.get()));
                }
                for(auto& i:exit->instructions()) if(auto* p=dynamic_cast<PhiInst*>(i.get()))
                    for(size_t k=0;k<p->num_incoming();++k)if(p->incoming_block(k)==header)p->set_incoming_block(k,expanded);
                auto* entry_branch=static_cast<BranchInst*>(pre->get_terminator());
                // CFG successors are unique blocks: a conditional branch can
                // have one successor while still carrying two edge operands.
                if(entry_branch->is_conditional()) {
                    entry_branch->set_true_target(expanded);
                    entry_branch->set_false_target(expanded);
                } else entry_branch->set_target(expanded);
                CFGAnalysis::eliminate_unreachable_blocks(*f);changed=true;again=true;break;
            }
        }
    }
    return changed;
}
}
