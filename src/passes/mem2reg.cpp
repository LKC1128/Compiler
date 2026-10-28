#include "passes/mem2reg.hpp"
#include "analysis/cfg.hpp"
#include "analysis/dominator.hpp"
#include "ir/ir_verifier.hpp"
#include <iostream>

namespace sysy {

bool Mem2RegPass::is_promotable(const AllocaInst* alloca_inst) const {
    Type* ty = alloca_inst->allocated_type();
    // Only promote scalar int or float
    if (!ty->is_integer() && !ty->is_float() && !ty->is_pointer()) {
        return false;
    }

    // Check all uses: must only be direct LoadInst or direct StoreInst as pointer
    for (const auto* use : alloca_inst->uses()) {
        auto* user = use->user();
        if (auto* ld = dynamic_cast<const LoadInst*>(user)) {
            if (ld->pointer() != alloca_inst) return false;
        } else if (auto* st = dynamic_cast<const StoreInst*>(user)) {
            if (st->pointer() != alloca_inst) return false;
            if (st->value() == alloca_inst) return false; // storing address escapes
        } else {
            // Any other use (GEP, Call, etc.) means it cannot be promoted
            return false;
        }
    }
    return true;
}

void Mem2RegPass::collect_promotable_allocas(Function& fn, std::vector<AllocaInst*>& allocas) const {
    auto* entry = fn.get_entry_block();
    if (!entry) return;

    for (const auto& inst : entry->instructions()) {
        if (auto* alloca_inst = dynamic_cast<AllocaInst*>(inst.get())) {
            if (is_promotable(alloca_inst)) {
                allocas.push_back(alloca_inst);
            }
        }
    }
}

bool Mem2RegPass::run_on_module(Module& mod) {
    bool changed = false;
    for (auto& fn : mod.functions()) {
        if (!fn->is_declaration()) {
            changed |= run_on_function(*fn);
        }
    }
    return changed;
}

bool Mem2RegPass::run_on_function(Function& fn) {
    std::vector<AllocaInst*> allocas;
    collect_promotable_allocas(fn, allocas);
    if (allocas.empty()) return false;

    // 1. Build CFG and Dominator Tree
    CFGAnalysis::eliminate_unreachable_blocks(fn);
    DominatorTree dom_tree(fn);

    // 2. Identify def blocks and place Phis using IDF
    // Map: [BB][AllocaInst*] -> PhiInst*
    std::unordered_map<BasicBlock*, std::unordered_map<AllocaInst*, PhiInst*>> bb_phis;

    for (auto* alloca_inst : allocas) {
        std::unordered_set<BasicBlock*> def_blocks;
        for (const auto* use : alloca_inst->uses()) {
            if (auto* st = dynamic_cast<StoreInst*>(use->user())) {
                if (st->pointer() == alloca_inst && st->parent()) {
                    def_blocks.insert(st->parent());
                }
            }
        }

        // Compute IDF
        auto idf = dom_tree.compute_idf(def_blocks);
        for (auto* bb : idf) {
            auto phi_inst = std::make_unique<PhiInst>(alloca_inst->allocated_type(), bb, alloca_inst->name() + "_phi");
            auto* phi_ptr = phi_inst.get();
            bb->insert_front(std::move(phi_inst));
            bb_phis[bb][alloca_inst] = phi_ptr;
        }
    }

    // 3. SSA Renaming via Dominator Tree DFS
    std::unordered_map<AllocaInst*, std::vector<Value*>> stacks;
    std::unordered_set<Instruction*> dead_instructions;

    auto rename_dfs = [&](auto& self, BasicBlock* bb) -> void {
        std::unordered_map<AllocaInst*, int> push_counts;

        // 3.1 Phi definitions in current block
        auto it_phis = bb_phis.find(bb);
        if (it_phis != bb_phis.end()) {
            for (auto& [alloca_inst, phi_inst] : it_phis->second) {
                stacks[alloca_inst].push_back(phi_inst);
                push_counts[alloca_inst]++;
            }
        }

        // 3.2 Process instructions in current block
        for (auto& inst : bb->instructions()) {
            if (auto* ld = dynamic_cast<LoadInst*>(inst.get())) {
                if (auto* alloca_inst = dynamic_cast<AllocaInst*>(ld->pointer())) {
                    if (std::find(allocas.begin(), allocas.end(), alloca_inst) != allocas.end()) {
                        Value* reaching_def = nullptr;
                        if (!stacks[alloca_inst].empty()) {
                            reaching_def = stacks[alloca_inst].back();
                        } else {
                            reaching_def = UndefValue::get(alloca_inst->allocated_type());
                        }
                        ld->replace_all_uses_with(reaching_def);
                        dead_instructions.insert(ld);
                    }
                }
            } else if (auto* st = dynamic_cast<StoreInst*>(inst.get())) {
                if (auto* alloca_inst = dynamic_cast<AllocaInst*>(st->pointer())) {
                    if (std::find(allocas.begin(), allocas.end(), alloca_inst) != allocas.end()) {
                        stacks[alloca_inst].push_back(st->value());
                        push_counts[alloca_inst]++;
                        dead_instructions.insert(st);
                    }
                }
            }
        }

        // 3.3 Update phi incoming values in CFG successors
        for (auto* succ : bb->successors()) {
            auto it_succ_phis = bb_phis.find(succ);
            if (it_succ_phis != bb_phis.end()) {
                for (auto& [alloca_inst, phi_inst] : it_succ_phis->second) {
                    Value* incoming_val = nullptr;
                    if (!stacks[alloca_inst].empty()) {
                        incoming_val = stacks[alloca_inst].back();
                    } else {
                        incoming_val = UndefValue::get(alloca_inst->allocated_type());
                    }
                    phi_inst->add_incoming(incoming_val, bb);
                }
            }
        }

        // 3.4 Recurse for all children in Dominator Tree
        for (auto* child : dom_tree.get_children(bb)) {
            self(self, child);
        }

        // 3.5 Backtrack: pop variable version stacks
        for (auto& [alloca_inst, count] : push_counts) {
            for (int k = 0; k < count; ++k) {
                stacks[alloca_inst].pop_back();
            }
        }
    };

    auto* entry = fn.get_entry_block();
    if (entry) {
        rename_dfs(rename_dfs, entry);
    }

    // 4. Remove dead loads, stores, and promoted allocas
    for (auto* dead_inst : dead_instructions) {
        dead_inst->drop_all_operands();
        dead_inst->parent()->erase_instruction(dead_inst);
    }

    for (auto* alloca_inst : allocas) {
        alloca_inst->drop_all_operands();
        alloca_inst->parent()->erase_instruction(alloca_inst);
    }

    return true;
}

} // namespace sysy
