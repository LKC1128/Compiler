#include "passes/cse.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace sysy {

namespace {

struct ExprKey {
    Opcode op;
    Type* ty;
    int predicate;
    unsigned ub;
    std::vector<Value*> ops;

    bool operator==(const ExprKey& o) const {
        if (op != o.op || ty != o.ty || predicate != o.predicate || ub != o.ub) return false;
        if (ops.size() != o.ops.size()) return false;
        for (size_t i = 0; i < ops.size(); ++i) {
            if (ops[i] != o.ops[i]) return false;
        }
        return true;
    }
};

struct ExprKeyHash {
    size_t operator()(const ExprKey& k) const {
        size_t h = static_cast<size_t>(k.op) ^ (reinterpret_cast<uintptr_t>(k.ty) << 3) ^ static_cast<size_t>(k.predicate);
        for (auto* v : k.ops) {
            h ^= reinterpret_cast<uintptr_t>(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

ExprKey get_expr_key(const Instruction* inst) {
    ExprKey k;
    k.op = inst->opcode();
    k.ty = inst->type();
    k.predicate = 0;
    k.ub=inst->ub_flags();

    if (inst->opcode() == Opcode::ICmp) {
        auto* cmp = static_cast<const ICmpInst*>(inst);
        k.predicate = static_cast<int>(cmp->predicate());
        Value* op0 = inst->get_operand(0);
        Value* op1 = inst->get_operand(1);
        if ((cmp->predicate() == ICmpPred::EQ || cmp->predicate() == ICmpPred::NE) && op0 > op1) {
            std::swap(op0, op1);
        }
        k.ops = {op0, op1};
    } else if (inst->opcode() == Opcode::FCmp) {
        auto* cmp = static_cast<const FCmpInst*>(inst);
        k.predicate = static_cast<int>(cmp->predicate());
        Value* op0 = inst->get_operand(0);
        Value* op1 = inst->get_operand(1);
        if ((cmp->predicate() == FCmpPred::OEQ || cmp->predicate() == FCmpPred::ONE || cmp->predicate() == FCmpPred::UNE) && op0 > op1) {
            std::swap(op0, op1);
        }
        k.ops = {op0, op1};
    } else if (inst->is_binary()) {
        Value* op0 = inst->get_operand(0);
        Value* op1 = inst->get_operand(1);
        if ((inst->opcode() == Opcode::Add || inst->opcode() == Opcode::Mul ||
             inst->opcode() == Opcode::And || inst->opcode() == Opcode::Or || inst->opcode() == Opcode::Xor) && op0 > op1) {
            std::swap(op0, op1);
        }
        k.ops = {op0, op1};
    } else {
        for (size_t i = 0; i < inst->num_operands(); ++i) {
            k.ops.push_back(inst->get_operand(i));
        }
    }
    return k;
}

} // namespace

bool CSEPass::is_pure_expression(const Instruction* inst) const {
    if (inst->is_binary() || inst->opcode() == Opcode::FNeg) return true;
    if (inst->opcode() == Opcode::ICmp || inst->opcode() == Opcode::FCmp) return true;
    if (inst->opcode() == Opcode::SIToFP || inst->opcode() == Opcode::FPToSI || inst->opcode() == Opcode::ZExt) return true;
    if (inst->opcode() == Opcode::GetElementPtr) return true;
    return false;
}

bool CSEPass::are_expressions_equivalent(const Instruction* a, const Instruction* b) const {
    return get_expr_key(a) == get_expr_key(b);
}

bool CSEPass::run_on_module(Module& mod) {
    bool changed = false;
    for (auto& fn : mod.functions()) {
        if (!fn->is_declaration()) {
            changed |= run_on_function(*fn);
        }
    }
    return changed;
}

bool CSEPass::run_on_function(Function& fn) {
    bool changed = false;

    for (auto& bb : fn.blocks()) {
        std::unordered_map<ExprKey, Instruction*, ExprKeyHash> expr_map;
        auto& inst_list = bb->instructions();

        for (auto it = inst_list.begin(); it != inst_list.end(); ) {
            Instruction* inst = it->get();
            if (!is_pure_expression(inst)) {
                ++it;
                continue;
            }

            ExprKey key = get_expr_key(inst);
            auto map_it = expr_map.find(key);
            if (map_it != expr_map.end()) {
                inst->replace_all_uses_with(map_it->second);
                inst->drop_all_operands();
                it = inst_list.erase(it);
                changed = true;
            } else {
                expr_map.emplace(std::move(key), inst);
                ++it;
            }
        }
    }

    return changed;
}

} // namespace sysy
