#include "passes/const_folding.hpp"
#include "analysis/cfg.hpp"
#include <vector>
#include <cmath>
#include "ir/int_math.hpp"
#include "ir/clone.hpp"
#include <limits>

namespace sysy {

bool ConstFoldingPass::run_on_module(Module& mod) {
    bool changed = false;
    for (auto& fn : mod.functions()) {
        if (!fn->is_declaration()) {
            changed |= run_on_function(*fn);
        }
    }
    return changed;
}

bool ConstFoldingPass::fold_binary(BinaryInst* bin) {
    auto& ctx = bin->parent()->parent()->parent()->type_context();
    auto* c1 = dynamic_cast<Constant*>(bin->lhs());
    auto* c2 = dynamic_cast<Constant*>(bin->rhs());

    // Both are ConstantInt
    if (auto* ci1 = dynamic_cast<ConstantInt*>(c1)) {
        if (auto* ci2 = dynamic_cast<ConstantInt*>(c2)) {
            int32_t v1 = ci1->value();
            int32_t v2 = ci2->value();
            int32_t res = 0;
            int64_t wide=0; uint64_t uwide=0;
            uint32_t u1=static_cast<uint32_t>(v1),u2=static_cast<uint32_t>(v2);
            bool invalid=false, overflow=false, uoverflow=false;
            switch(bin->opcode()) {
            case Opcode::Add: wide=int64_t(v1)+v2;uwide=uint64_t(u1)+u2;break;
            case Opcode::Sub: wide=int64_t(v1)-v2;uoverflow=u1<u2;break;
            case Opcode::Mul: wide=int64_t(v1)*v2;uwide=uint64_t(u1)*u2;break;
            case Opcode::Shl:
                invalid=u2>=32;
                if(!invalid) {wide=int64_t(v1)*(int64_t(1)<<u2);uwide=uint64_t(u1)<<u2;} break;
            case Opcode::SDiv:
                invalid=!int_div_defined(v1,v2);
                if(!invalid && bin->has_ub_flag(UB_EXACT)) invalid=v1%v2!=0;
                break;
            case Opcode::LShr: case Opcode::AShr:
                invalid=u2>=32;
                if(!invalid && bin->has_ub_flag(UB_EXACT)) invalid=(u1 & ((uint32_t(1)<<u2)-1))!=0;
                break;
            default:break;
            }
            overflow=wide<std::numeric_limits<int32_t>::min() || wide>std::numeric_limits<int32_t>::max();
            uoverflow |= uwide>std::numeric_limits<uint32_t>::max();
            if(invalid || (bin->has_ub_flag(UB_NSW)&&overflow) || (bin->has_ub_flag(UB_NUW)&&uoverflow)) {
                bin->replace_all_uses_with(PoisonValue::get(bin->type()));return true;
            }
            switch (bin->opcode()) {
                case Opcode::And: res=bit_copy<int32_t>(u1 & u2);break;
                case Opcode::Or: res=bit_copy<int32_t>(u1 | u2);break;
                case Opcode::Xor: res=bit_copy<int32_t>(u1 ^ u2);break;
                case Opcode::Shl: res=bit_copy<int32_t>(u1 << u2);break;
                case Opcode::LShr: res=bit_copy<int32_t>(u1 >> u2);break;
                case Opcode::AShr: res=bit_copy<int32_t>((u1 >> u2) | (v1<0 && u2 ? (~uint32_t(0) << (32-u2)) : 0));break;
                case Opcode::Add: res = int_add(v1, v2); break;
                case Opcode::Sub: res = int_sub(v1, v2); break;
                case Opcode::Mul: res = int_mul(v1, v2); break;
                case Opcode::SDiv: if (int_div_defined(v1, v2)) res = v1 / v2; else return false; break;
                case Opcode::SRem: if (int_div_defined(v1, v2)) res = v1 % v2; else return false; break;
                default: return false;
            }
            bin->replace_all_uses_with(ConstantInt::get(ctx, res));
            return true;
        }
    }

    // Both are ConstantFloat
    if (auto* cf1 = dynamic_cast<ConstantFloat*>(c1)) {
        if (auto* cf2 = dynamic_cast<ConstantFloat*>(c2)) {
            float v1 = cf1->value();
            float v2 = cf2->value();
            float res = 0.0f;
            switch (bin->opcode()) {
                case Opcode::FAdd: res = v1 + v2; break;
                case Opcode::FSub: res = v1 - v2; break;
                case Opcode::FMul: res = v1 * v2; break;
                case Opcode::FDiv: if (v2 != 0.0f) res = v1 / v2; else return false; break;
                default: return false;
            }
            bin->replace_all_uses_with(ConstantFloat::get(ctx, res));
            return true;
        }
    }

    // Algebraic simplifications
    if (auto* ci2 = dynamic_cast<ConstantInt*>(c2)) {
        if (bin->opcode() == Opcode::Add && ci2->value() == 0) {
            bin->replace_all_uses_with(bin->lhs());
            return true;
        }
        if (bin->opcode() == Opcode::Sub && ci2->value() == 0) {
            bin->replace_all_uses_with(bin->lhs());
            return true;
        }
        if (bin->opcode() == Opcode::Mul && ci2->value() == 1) {
            bin->replace_all_uses_with(bin->lhs());
            return true;
        }
        if (bin->opcode() == Opcode::Mul && ci2->value() == 0) {
            bin->replace_all_uses_with(ConstantInt::get(ctx, 0));
            return true;
        }
        if (bin->opcode() == Opcode::SDiv && ci2->value() == 1) {
            bin->replace_all_uses_with(bin->lhs());
            return true;
        }
    }

    if (auto* ci1 = dynamic_cast<ConstantInt*>(c1)) {
        if (bin->opcode() == Opcode::Add && ci1->value() == 0) {
            bin->replace_all_uses_with(bin->rhs());
            return true;
        }
        if (bin->opcode() == Opcode::Mul && ci1->value() == 1) {
            bin->replace_all_uses_with(bin->rhs());
            return true;
        }
        if (bin->opcode() == Opcode::Mul && ci1->value() == 0) {
            bin->replace_all_uses_with(ConstantInt::get(ctx, 0));
            return true;
        }
    }

    if (bin->opcode() == Opcode::Sub && bin->lhs() == bin->rhs()) {
        bin->replace_all_uses_with(ConstantInt::get(ctx, 0));
        return true;
    }

    return false;
}

bool ConstFoldingPass::fold_icmp(ICmpInst* icmp) {
    auto& ctx = icmp->parent()->parent()->parent()->type_context();
    auto* ci1 = dynamic_cast<ConstantInt*>(icmp->lhs());
    auto* ci2 = dynamic_cast<ConstantInt*>(icmp->rhs());
    if (!ci1 || !ci2) return false;

    unsigned bits = static_cast<IntegerType*>(ci1->type())->bits();
    uint32_t mask = bits == 32 ? UINT32_MAX : (uint32_t(1) << bits) - 1;
    uint32_t u1 = uint32_t(ci1->value()) & mask, u2 = uint32_t(ci2->value()) & mask;
    auto signed_value = [bits](uint32_t value) -> int64_t {
        return value & (uint32_t(1) << (bits - 1)) ? int64_t(value) - (int64_t(1) << bits) : value;
    };
    int64_t v1 = signed_value(u1), v2 = signed_value(u2);
    bool res = false;
    switch (icmp->predicate()) {
        case ICmpPred::ULT: res=u1<u2;break;
        case ICmpPred::ULE: res=u1<=u2;break;
        case ICmpPred::UGT: res=u1>u2;break;
        case ICmpPred::UGE: res=u1>=u2;break;
        case ICmpPred::EQ: res = (v1 == v2); break;
        case ICmpPred::NE: res = (v1 != v2); break;
        case ICmpPred::SLT: res = (v1 < v2); break;
        case ICmpPred::SLE: res = (v1 <= v2); break;
        case ICmpPred::SGT: res = (v1 > v2); break;
        case ICmpPred::SGE: res = (v1 >= v2); break;
    }
    icmp->replace_all_uses_with(ConstantInt::get_bool(ctx, res));
    return true;
}

bool ConstFoldingPass::fold_fcmp(FCmpInst* fcmp) {
    auto& ctx = fcmp->parent()->parent()->parent()->type_context();
    auto* cf1 = dynamic_cast<ConstantFloat*>(fcmp->lhs());
    auto* cf2 = dynamic_cast<ConstantFloat*>(fcmp->rhs());
    if (!cf1 || !cf2) return false;

    float v1 = cf1->value();
    float v2 = cf2->value();
    bool res = false;
    switch (fcmp->predicate()) {
        case FCmpPred::OEQ: res = (v1 == v2); break;
        case FCmpPred::ONE: res = (!std::isnan(v1) && !std::isnan(v2) && v1 != v2); break;
        case FCmpPred::UNE: res = (v1 != v2); break;
        case FCmpPred::OLT: res = (v1 < v2); break;
        case FCmpPred::OLE: res = (v1 <= v2); break;
        case FCmpPred::OGT: res = (v1 > v2); break;
        case FCmpPred::OGE: res = (v1 >= v2); break;
    }
    fcmp->replace_all_uses_with(ConstantInt::get_bool(ctx, res));
    return true;
}

bool ConstFoldingPass::fold_cast(CastInst* cast) {
    auto& ctx = cast->parent()->parent()->parent()->type_context();
    if (cast->opcode()==Opcode::Trunc || cast->opcode()==Opcode::SExt || cast->opcode()==Opcode::ZExt) {
        if(auto* ci=dynamic_cast<ConstantInt*>(cast->operand())) {
            unsigned from=static_cast<IntegerType*>(ci->type())->bits();
            unsigned to=static_cast<IntegerType*>(cast->type())->bits();
            uint32_t value=uint32_t(ci->value());
            if(from<32) value &= (uint32_t(1)<<from)-1;
            if(cast->opcode()==Opcode::SExt && from<32 && (value & (uint32_t(1)<<(from-1)))) value |= ~((uint32_t(1)<<from)-1);
            ConstantInt* result=to==1 ? ConstantInt::get_bool(ctx,(value&1)!=0) : to==8 ? ConstantInt::get_byte(ctx,uint8_t(value)) : ConstantInt::get(ctx,bit_copy<int32_t>(value));
            cast->replace_all_uses_with(result);return true;
        }
    } else if (cast->opcode() == Opcode::SIToFP) {
        // ConstantFloat stores binary32 only. Keep binary64 conversions as
        // instructions rather than replacing a double with a float value.
        if(cast->type()->size_in_bytes()!=4) return false;
        if (auto* ci = dynamic_cast<ConstantInt*>(cast->operand())) {
            cast->replace_all_uses_with(ConstantFloat::get(ctx, static_cast<float>(ci->value())));
            return true;
        }
    } else if (cast->opcode() == Opcode::FPToSI) {
        if (auto* cf = dynamic_cast<ConstantFloat*>(cast->operand())) {
            if (!std::isfinite(cf->value()) || cf->value() < -2147483648.0 || cf->value() >= 2147483648.0) return false;
            cast->replace_all_uses_with(ConstantInt::get(ctx, static_cast<int32_t>(cf->value())));
            return true;
        }
    }
    return false;
}

bool ConstFoldingPass::fold_branch(BranchInst* br, Function& fn) {
    if (!br->is_conditional()) return false;
    auto* ci = dynamic_cast<ConstantInt*>(br->condition());
    if (!ci) return false;

    auto& ctx = fn.parent()->type_context();
    auto* parent_bb = br->parent();
    bool cond_true = (ci->value() != 0);

    BasicBlock* taken = cond_true ? br->true_target() : br->false_target();
    BasicBlock* not_taken = cond_true ? br->false_target() : br->true_target();

    // Replace conditional branch with unconditional branch
    auto new_br = std::make_unique<BranchInst>(taken, ctx);
    parent_bb->insert_before(br, std::move(new_br));

    // Remove incoming in phis of not_taken
    if (taken != not_taken) for (auto& inst : not_taken->instructions()) {
        if (auto* phi = dynamic_cast<PhiInst*>(inst.get())) {
            phi->remove_incoming_block(parent_bb);
        }
    }

    br->drop_all_operands();
    parent_bb->erase_instruction(br);
    return true;
}

bool ConstFoldingPass::run_on_function(Function& fn) {
    bool changed = false;
    bool local_changed = true;
    bool cfg_changed = false;

    while (local_changed) {
        local_changed = false;
        std::vector<Instruction*> to_erase;

        for (auto& bb : fn.blocks()) {
            for (auto& inst : bb->instructions()) {
                bool poison=false;
                if(is_pure_expression(*inst)) for(size_t k=0;k<inst->num_operands();++k)
                    poison |= dynamic_cast<PoisonValue*>(inst->get_operand(k))!=nullptr;
                if(poison) {
                    inst->replace_all_uses_with(PoisonValue::get(inst->type()));
                    to_erase.push_back(inst.get());local_changed=true;
                } else if (auto* bin = dynamic_cast<BinaryInst*>(inst.get())) {
                    if (fold_binary(bin)) {
                        to_erase.push_back(bin);
                        local_changed = true;
                    }
                } else if (inst->opcode() == Opcode::FNeg) {
                    if (auto* v = dynamic_cast<ConstantFloat*>(inst->get_operand(0))) {
                        inst->replace_all_uses_with(ConstantFloat::get(fn.parent()->type_context(), -v->value()));
                        to_erase.push_back(inst.get());
                        local_changed = true;
                    }
                } else if (auto* icmp = dynamic_cast<ICmpInst*>(inst.get())) {
                    if (fold_icmp(icmp)) {
                        to_erase.push_back(icmp);
                        local_changed = true;
                    }
                } else if (auto* fcmp = dynamic_cast<FCmpInst*>(inst.get())) {
                    if (fold_fcmp(fcmp)) {
                        to_erase.push_back(fcmp);
                        local_changed = true;
                    }
                } else if (auto* cast = dynamic_cast<CastInst*>(inst.get())) {
                    if (fold_cast(cast)) {
                        to_erase.push_back(cast);
                        local_changed = true;
                    }
                } else if (auto* br = dynamic_cast<BranchInst*>(inst.get())) {
                    if (fold_branch(br, fn)) {
                        local_changed = true;
                        cfg_changed = true;
                        break;
                    }
                }
            }
        }

        for (auto* inst : to_erase) {
            inst->drop_all_operands();
            inst->parent()->erase_instruction(inst);
        }

        changed |= local_changed;
    }

    if (cfg_changed) {
        CFGAnalysis::compute_cfg(fn);
    }

    return changed;
}

} // namespace sysy
