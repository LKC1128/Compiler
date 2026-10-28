#include "passes/dce.hpp"
#include <vector>

namespace sysy {

bool DCEPass::is_instruction_dead(const Instruction* inst) const {
    if (inst->is_terminator()) return false;
    if (inst->is_store()) return false;
    if (inst->is_call()) return false; // conservatively keep calls

    // Non-void instruction with no uses is dead
    if (!inst->type()->is_void() && !inst->has_uses()) {
        return true;
    }

    return false;
}

bool DCEPass::run_on_module(Module& mod) {
    bool changed = false;
    for (auto& fn : mod.functions()) {
        if (!fn->is_declaration()) {
            changed |= run_on_function(*fn);
        }
    }
    return changed;
}

bool DCEPass::run_on_function(Function& fn) {
    bool changed = false;
    bool local_changed = true;

    while (local_changed) {
        local_changed = false;
        std::vector<Instruction*> dead_instructions;

        for (auto& bb : fn.blocks()) {
            for (auto& inst : bb->instructions()) {
                if (is_instruction_dead(inst.get())) {
                    dead_instructions.push_back(inst.get());
                }
            }
        }

        for (auto* inst : dead_instructions) {
            inst->drop_all_operands();
            inst->parent()->erase_instruction(inst);
            local_changed = true;
        }

        changed |= local_changed;
    }

    return changed;
}

} // namespace sysy
