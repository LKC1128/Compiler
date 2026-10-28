#include "analysis/cfg.hpp"
#include "ir/constant.hpp"
#include <queue>
#include <algorithm>

namespace sysy {

void CFGAnalysis::compute_cfg(Function& fn) {
    // Clear all existing preds and succs
    for (auto& bb : fn.blocks()) {
        bb->clear_successors();
        bb->clear_predecessors();
    }

    // Rebuild from terminators
    for (auto& bb : fn.blocks()) {
        auto* term = bb->get_terminator();
        if (!term) continue;

        if (auto* br = dynamic_cast<BranchInst*>(term)) {
            if (br->is_conditional()) {
                auto* t = br->true_target();
                auto* f = br->false_target();
                bb->add_successor(t);
                bb->add_successor(f);
                t->add_predecessor(bb.get());
                f->add_predecessor(bb.get());
            } else {
                auto* t = br->target();
                bb->add_successor(t);
                t->add_predecessor(bb.get());
            }
        }
    }
}

std::unordered_set<BasicBlock*> CFGAnalysis::compute_reachable(Function& fn) {
    std::unordered_set<BasicBlock*> reachable;
    auto* entry = fn.get_entry_block();
    if (!entry) return reachable;

    std::queue<BasicBlock*> worklist;
    worklist.push(entry);
    reachable.insert(entry);

    while (!worklist.empty()) {
        auto* curr = worklist.front();
        worklist.pop();

        for (auto* succ : curr->successors()) {
            if (reachable.insert(succ).second) {
                worklist.push(succ);
            }
        }
    }
    return reachable;
}

void CFGAnalysis::eliminate_unreachable_blocks(Function& fn) {
    compute_cfg(fn);
    auto reachable = compute_reachable(fn);

    std::vector<BasicBlock*> unreachable;
    std::unordered_set<BasicBlock*> unreachable_set;
    for (auto& bb : fn.blocks()) {
        if (reachable.find(bb.get()) == reachable.end()) {
            unreachable.push_back(bb.get());
            unreachable_set.insert(bb.get());
        }
    }

    if (unreachable.empty()) return;

    // 1. Replace uses of instructions in unreachable blocks with UndefValue
    for (auto* bb : unreachable) {
        for (auto& inst : bb->instructions()) {
            if (!inst->type()->is_void()) {
                inst->replace_all_uses_with(UndefValue::get(inst->type()));
            }
        }
    }

    // 2. Disconnect from reachable successors
    for (auto* bb : unreachable) {
        for (auto* succ : bb->successors()) {
            if (unreachable_set.find(succ) == unreachable_set.end()) {
                // succ is reachable: update its predecessors and phis
                succ->remove_predecessor(bb);
                for (auto& inst : succ->instructions()) {
                    if (auto* phi = dynamic_cast<PhiInst*>(inst.get())) {
                        phi->remove_incoming_block(bb);
                    }
                }
            }
        }
    }

    // 3. Drop all operands inside all unreachable blocks
    for (auto* bb : unreachable) {
        for (auto& inst : bb->instructions()) {
            inst->drop_all_operands();
        }
        bb->clear_successors();
    }

    // 4. Erase all unreachable blocks from function
    for (auto* bb : unreachable) {
        fn.erase_block(bb);
    }

    compute_cfg(fn);
}

void CFGAnalysis::postorder_dfs(BasicBlock* bb, std::unordered_set<BasicBlock*>& visited, std::vector<BasicBlock*>& order) {
    visited.insert(bb);
    for (auto* succ : bb->successors()) {
        if (visited.find(succ) == visited.end()) {
            postorder_dfs(succ, visited, order);
        }
    }
    order.push_back(bb);
}

std::vector<BasicBlock*> CFGAnalysis::get_postorder(Function& fn) {
    std::vector<BasicBlock*> order;
    std::unordered_set<BasicBlock*> visited;
    auto* entry = fn.get_entry_block();
    if (entry) {
        postorder_dfs(entry, visited, order);
    }
    return order;
}

std::vector<BasicBlock*> CFGAnalysis::get_reverse_postorder(Function& fn) {
    auto order = get_postorder(fn);
    std::reverse(order.begin(), order.end());
    return order;
}

} // namespace sysy
