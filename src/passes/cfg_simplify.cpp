#include "passes/cfg_simplify.hpp"
#include "analysis/cfg.hpp"
#include <vector>

namespace sysy {

bool CFGSimplifyPass::run_on_module(Module& mod) {
    bool changed = false;
    for (auto& fn : mod.functions()) {
        if (!fn->is_declaration()) {
            changed |= run_on_function(*fn);
        }
    }
    return changed;
}

bool CFGSimplifyPass::merge_blocks(Function& fn) {
    CFGAnalysis::compute_cfg(fn);
    auto* entry = fn.get_entry_block();

    for (auto& bb : fn.blocks()) {
        if (bb->successors().size() == 1) {
            auto* succ = bb->successors()[0];
            if (succ != bb.get() && succ != entry && succ->predecessors().size() == 1) {
                // Check that succ has no phis (or if it does, single incoming phi can be simplified)
                bool has_phi = false;
                for (const auto& inst : succ->instructions()) {
                    if (inst->is_phi()) {
                        has_phi = true;
                        break;
                    }
                }
                if (has_phi) continue;

                // Erase bb's terminator
                auto* term_a = bb->get_terminator();
                if (term_a) {
                    term_a->drop_all_operands();
                    bb->erase_instruction(term_a);
                }

                // Move all instructions from succ into bb
                while (!succ->instructions().empty()) {
                    auto inst = succ->remove_instruction(succ->instructions().front().get());
                    bb->append_instruction(std::move(inst));
                }

                // Update phis in succ's successors: replace succ with bb
                for (auto* next_succ : succ->successors()) {
                    for (auto& inst : next_succ->instructions()) {
                        if (auto* phi = dynamic_cast<PhiInst*>(inst.get())) {
                            for (size_t i = 0; i < phi->num_incoming(); ++i) {
                                if (phi->incoming_block(i) == succ) {
                                    phi->set_incoming_block(i, bb.get());
                                }
                            }
                        }
                    }
                }

                fn.erase_block(succ);
                CFGAnalysis::compute_cfg(fn);
                return true;
            }
        }
    }
    return false;
}

bool CFGSimplifyPass::run_on_function(Function& fn) {
    bool changed = false;
    CFGAnalysis::eliminate_unreachable_blocks(fn);

    while (merge_blocks(fn)) {
        changed = true;
    }

    CFGAnalysis::eliminate_unreachable_blocks(fn);
    return changed;
}

} // namespace sysy
