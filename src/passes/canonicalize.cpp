#include "passes/middle_end.hpp"
#include "passes/const_folding.hpp"
#include "ir/constant.hpp"
#include "analysis/cfg.hpp"
#include <stdexcept>
namespace sysy {
namespace {
bool constant(Value* v) {return dynamic_cast<Constant*>(v)!=nullptr;}
bool commutes(const Instruction& i) {
    return i.opcode()==Opcode::Add || i.opcode()==Opcode::Mul || i.opcode()==Opcode::And || i.opcode()==Opcode::Or || i.opcode()==Opcode::Xor;
}
bool bad(const Instruction& i) {
    if(auto* br=dynamic_cast<const BranchInst*>(&i)) return br->is_conditional() && dynamic_cast<ConstantInt*>(br->condition());
    if(commutes(i) || i.opcode()==Opcode::ICmp) return constant(i.get_operand(0)) && !constant(i.get_operand(1));
    return false;
}
ICmpPred swap_pred(ICmpPred p) {
    switch(p) {
    case ICmpPred::SLT:return ICmpPred::SGT;case ICmpPred::SLE:return ICmpPred::SGE;
    case ICmpPred::SGT:return ICmpPred::SLT;case ICmpPred::SGE:return ICmpPred::SLE;
    case ICmpPred::ULT:return ICmpPred::UGT;case ICmpPred::ULE:return ICmpPred::UGE;
    case ICmpPred::UGT:return ICmpPred::ULT;case ICmpPred::UGE:return ICmpPred::ULE;
    default:return p;
    }
}
}
CanonicalRank canonical_rank(const Module& mod) {
    CanonicalRank rank;
    for(const auto& f:mod.functions()) for(const auto& b:f->blocks()) for(const auto& i:b->instructions()) {
        ++rank.inst_count;rank.bad_count+=bad(*i);
    }
    return rank;
}
bool CanonicalizePass::run_on_module(Module& mod) {
    bool changed=false;
    for(;;) {
        auto before=canonical_rank(mod);bool local=false;
        for(auto& f:mod.functions()) for(auto& b:f->blocks()) {
            std::vector<Instruction*> remove;
            for(auto& i:b->instructions()) {
                if((commutes(*i) || i->opcode()==Opcode::ICmp) && bad(*i)) {
                    Value* left=i->get_operand(0);i->set_operand(0,i->get_operand(1));i->set_operand(1,left);
                    if(auto* cmp=dynamic_cast<ICmpInst*>(i.get())) cmp->set_predicate(swap_pred(cmp->predicate()));
                    local=true;
                }
                // This refinement needs an explicit no-signed-wrap contract.
                if(auto* cmp=dynamic_cast<ICmpInst*>(i.get())) {
                    auto* ext=dynamic_cast<CastInst*>(cmp->lhs());
                    auto* literal=dynamic_cast<ConstantInt*>(cmp->rhs());
                    if(ext && ext->opcode()==Opcode::ZExt && ext->operand()->type()==mod.type_context().get_i1_type() && literal &&
                       ((cmp->predicate()==ICmpPred::NE && literal->value()==0) || (cmp->predicate()==ICmpPred::EQ && literal->value()==1))) {
                        cmp->replace_all_uses_with(ext->operand());remove.push_back(cmp);local=true;continue;
                    }
                    auto* add=dynamic_cast<BinaryInst*>(cmp->lhs());
                    auto* c=add ? dynamic_cast<ConstantInt*>(add->rhs()) : nullptr;
                    if(add && add->opcode()==Opcode::Add && add->has_ub_flag(UB_NSW) && c && add->lhs()==cmp->rhs() &&
                       (cmp->predicate()==ICmpPred::SGT || cmp->predicate()==ICmpPred::SLE)) {
                        bool value=cmp->predicate()==ICmpPred::SGT ? c->value()>0 : c->value()<=0;
                        cmp->replace_all_uses_with(ConstantInt::get_bool(mod.type_context(),value));remove.push_back(cmp);local=true;
                    }
                }
            }
            for(auto* i:remove) b->erase_instruction(i);
        }
        local |= ConstFoldingPass().run_on_module(mod);
        if(!local) break;
        if(!(canonical_rank(mod)<before)) throw std::runtime_error("Canonicalization failed strict rank descent");
        changed=true;
    }
    for(auto& f:mod.functions()) if(!f->is_declaration()) f->set_representation(f->level(),f->state_flags()|SF_CANONICAL);
    return changed;
}
bool LowerHIRPass::run_on_module(Module& mod) {
    for(auto& f:mod.functions()) if(!f->is_declaration()) {
        f->clear_regions();f->set_representation(IRLevel::MIR,SF_SSA);
        // Dead syntax is still checked by the frontend. It must not reserve
        // real stack space, including at O0, after semantic checking finishes.
        CFGAnalysis::eliminate_unreachable_blocks(*f);
        auto* entry=f->get_entry_block();
        std::vector<Instruction*> unused_slots;
        for(auto& i:entry->instructions()) if(i->is_alloca() && !i->has_uses()) unused_slots.push_back(i.get());
        for(auto* i:unused_slots) entry->erase_instruction(i);
    }
    return true;
}
}
