#include "passes/phi_simplify.hpp"
#include <vector>

namespace sysy {

bool PhiSimplifyPass::run_on_module(Module& mod) {
    bool changed = false;
    for (auto& fn : mod.functions()) {
        if (!fn->is_declaration()) {
            changed |= run_on_function(*fn);
        }
    }
    return changed;
}

bool PhiSimplifyPass::run_on_function(Function& fn) {
    bool changed = false;
    bool local_changed = true;

    while (local_changed) {
        local_changed = false;
        std::vector<PhiInst*> to_erase;

        for (auto& bb : fn.blocks()) {
            for (auto& inst : bb->instructions()) {
                if (auto* phi = dynamic_cast<PhiInst*>(inst.get())) {
                    if (phi->num_incoming() == 0) {
                        phi->replace_all_uses_with(UndefValue::get(phi->type()));
                        to_erase.push_back(phi);
                        local_changed = true;
                        continue;
                    }
                    if (phi->num_incoming() == 1) {
                        auto* incoming = phi->incoming_value(0);
                        // An unreachable self-loop may contain a self-only phi.
                        // RAUW(self) is a no-op and would leave dangling users.
                        phi->replace_all_uses_with(incoming == phi ? UndefValue::get(phi->type()) : incoming);
                        to_erase.push_back(phi);
                        local_changed = true;
                        continue;
                    }

                    // Check if all non-self incoming values are the same
                    Value* candidate = nullptr;
                    bool can_simplify = true;
                    for (size_t i = 0; i < phi->num_incoming(); ++i) {
                        auto* in_val = phi->incoming_value(i);
                        if (in_val == phi) continue; // ignore self-reference
                        if (!candidate) {
                            candidate = in_val;
                        } else if (candidate != in_val) {
                            can_simplify = false;
                            break;
                        }
                    }

                    if (can_simplify && candidate) {
                        phi->replace_all_uses_with(candidate);
                        to_erase.push_back(phi);
                        local_changed = true;
                    }
                }
            }
        }

        for (auto* phi : to_erase) {
            phi->drop_all_operands();
            phi->parent()->erase_instruction(phi);
        }

        changed |= local_changed;
    }

    return changed;
}

} // namespace sysy
