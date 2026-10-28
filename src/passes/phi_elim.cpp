#include "passes/phi_elim.hpp"
#include "analysis/cfg.hpp"
#include <vector>
#include <iostream>

namespace sysy {

void PhiEliminationPass::split_critical_edges(Function& fn) {
    auto& ctx = fn.parent()->type_context();
    CFGAnalysis::compute_cfg(fn);

    std::vector<std::pair<BasicBlock*, BasicBlock*>> critical_edges;
    for (auto& bb : fn.blocks()) {
        if (bb->successors().size() > 1) {
            for (auto* succ : bb->successors()) {
                if (succ->predecessors().size() > 1) {
                    // Check if succ has phi instructions
                    bool has_phi = false;
                    for (const auto& inst : succ->instructions()) {
                        if (inst->is_phi()) {
                            has_phi = true;
                            break;
                        }
                    }
                    if (has_phi) {
                        critical_edges.push_back({bb.get(), succ});
                    }
                }
            }
        }
    }

    for (auto& [pred, succ] : critical_edges) {
        auto* split_bb = fn.create_block(ctx, "crit_edge");
        split_bb->append_instruction(std::make_unique<BranchInst>(succ, ctx));

        // Update pred terminator
        auto* term = pred->get_terminator();
        if (auto* br = dynamic_cast<BranchInst*>(term)) {
            if (br->is_conditional()) {
                if (br->true_target() == succ) br->set_true_target(split_bb);
                if (br->false_target() == succ) br->set_false_target(split_bb);
            } else {
                if (br->target() == succ) br->set_target(split_bb);
            }
        }

        // Update Phi nodes in succ
        for (auto& inst : succ->instructions()) {
            if (auto* phi = dynamic_cast<PhiInst*>(inst.get())) {
                for (size_t i = 0; i < phi->num_incoming(); ++i) {
                    if (phi->incoming_block(i) == pred) {
                        phi->set_incoming_block(i, split_bb);
                    }
                }
            }
        }
    }

    if (!critical_edges.empty()) {
        CFGAnalysis::compute_cfg(fn);
    }
}

bool PhiEliminationPass::run_on_module(Module& mod) {
    bool changed = false;
    for (auto& fn : mod.functions()) {
        if (!fn->is_declaration()) {
            changed |= run_on_function(*fn);
        }
    }
    return changed;
}

bool PhiEliminationPass::run_on_function(Function& fn) {
    bool has_any_phi = false;
    for (auto& bb : fn.blocks()) {
        for (auto& inst : bb->instructions()) {
            if (inst->is_phi()) {
                has_any_phi = true;
                break;
            }
        }
        if (has_any_phi) break;
    }
    if (!has_any_phi) return false;

    split_critical_edges(fn);

    auto& ctx = fn.parent()->type_context();
    auto* entry = fn.get_entry_block();

    for (auto& bb : fn.blocks()) {
        std::vector<PhiInst*> phis;
        for (auto& inst : bb->instructions()) {
            if (auto* phi = dynamic_cast<PhiInst*>(inst.get())) {
                phis.push_back(phi);
            }
        }

        if (phis.empty()) continue;

        // Find position of first non-phi instruction in bb
        Instruction* first_non_phi = nullptr;
        for (auto& inst : bb->instructions()) {
            if (!inst->is_phi()) {
                first_non_phi = inst.get();
                break;
            }
        }

        for (auto* phi : phis) {
            // 1. Create a stack slot for this phi in entry block
            auto alloca_inst = std::make_unique<AllocaInst>(phi->type(), ctx, entry, phi->name() + "_slot");
            auto* slot = entry->insert_front(std::move(alloca_inst));

            // 2. In each predecessor, store incoming value into slot right before terminator
            for (size_t i = 0; i < phi->num_incoming(); ++i) {
                auto* in_val = phi->incoming_value(i);
                auto* in_bb = phi->incoming_block(i);
                auto* term = in_bb->get_terminator();
                if (term) {
                    in_bb->insert_before(term, std::make_unique<StoreInst>(in_val, slot, ctx));
                }
            }

            // 3. In bb, load from slot
            auto load_inst = std::make_unique<LoadInst>(slot, bb.get(), phi->name() + "_val");
            Instruction* ld_ptr = nullptr;
            if (first_non_phi) {
                ld_ptr = bb->insert_before(first_non_phi, std::move(load_inst));
            } else {
                ld_ptr = bb->append_instruction(std::move(load_inst));
            }

            // 4. Replace all uses of phi with load
            phi->replace_all_uses_with(ld_ptr);

            // 5. Erase phi
            phi->drop_all_operands();
            bb->erase_instruction(phi);
        }
    }

    CFGAnalysis::compute_cfg(fn);
    return true;
}

} // namespace sysy
