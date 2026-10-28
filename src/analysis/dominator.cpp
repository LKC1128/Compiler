#include "analysis/dominator.hpp"
#include "analysis/cfg.hpp"
#include <queue>
#include <cassert>

namespace sysy {

const std::vector<BasicBlock*> DominatorTree::empty_children_;
const std::unordered_set<BasicBlock*> DominatorTree::empty_df_;

DominatorTree::DominatorTree(Function& fn) {
    CFGAnalysis::eliminate_unreachable_blocks(fn);
    compute_idom(fn);
    build_tree(fn);
    compute_df(fn);
}

BasicBlock* DominatorTree::intersect(BasicBlock* b1, BasicBlock* b2) const {
    auto finger1 = b1;
    auto finger2 = b2;

    while (finger1 != finger2) {
        while (rpo_indices_.at(finger1) > rpo_indices_.at(finger2)) {
            finger1 = idoms_.at(finger1);
        }
        while (rpo_indices_.at(finger2) > rpo_indices_.at(finger1)) {
            finger2 = idoms_.at(finger2);
        }
    }
    return finger1;
}

void DominatorTree::compute_idom(Function& fn) {
    auto* entry = fn.get_entry_block();
    if (!entry) return;

    rpo_blocks_ = CFGAnalysis::get_reverse_postorder(fn);
    for (size_t i = 0; i < rpo_blocks_.size(); ++i) {
        rpo_indices_[rpo_blocks_[i]] = static_cast<int>(i);
    }

    idoms_[entry] = entry;

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 1; i < rpo_blocks_.size(); ++i) {
            auto* b = rpo_blocks_[i];
            BasicBlock* new_idom = nullptr;

            // Find first processed predecessor
            for (auto* p : b->predecessors()) {
                if (idoms_.find(p) != idoms_.end()) {
                    new_idom = p;
                    break;
                }
            }

            if (!new_idom) continue;

            for (auto* p : b->predecessors()) {
                if (p != new_idom && idoms_.find(p) != idoms_.end()) {
                    new_idom = intersect(p, new_idom);
                }
            }

            if (idoms_.find(b) == idoms_.end() || idoms_[b] != new_idom) {
                idoms_[b] = new_idom;
                changed = true;
            }
        }
    }
}

void DominatorTree::build_tree(Function& fn) {
    auto* entry = fn.get_entry_block();
    for (auto* b : rpo_blocks_) {
        if (b != entry) {
            auto* idom = idoms_[b];
            if (idom) {
                dom_tree_children_[idom].push_back(b);
            }
        }
    }
}

void DominatorTree::compute_df(Function& fn) {
    auto* entry = fn.get_entry_block();
    for (auto* b : rpo_blocks_) {
        if (b->predecessors().size() >= 2) {
            auto* idom_b = (b == entry) ? nullptr : idoms_[b];
            for (auto* p : b->predecessors()) {
                auto* runner = p;
                while (runner && runner != idom_b) {
                    df_[runner].insert(b);
                    if (runner == entry) break;
                    runner = idoms_[runner];
                }
            }
        }
    }
}

BasicBlock* DominatorTree::get_idom(BasicBlock* bb) const {
    auto it = idoms_.find(bb);
    return (it != idoms_.end()) ? it->second : nullptr;
}

const std::vector<BasicBlock*>& DominatorTree::get_children(BasicBlock* bb) const {
    auto it = dom_tree_children_.find(bb);
    return (it != dom_tree_children_.end()) ? it->second : empty_children_;
}

const std::unordered_set<BasicBlock*>& DominatorTree::get_dominance_frontier(BasicBlock* bb) const {
    auto it = df_.find(bb);
    return (it != df_.end()) ? it->second : empty_df_;
}

bool DominatorTree::dominates(BasicBlock* a, BasicBlock* b) const {
    if (!a || !b) return false;
    if (a == b) return true;

    auto* curr = b;
    while (curr && curr != idoms_.at(curr)) {
        if (curr == a) return true;
        curr = idoms_.at(curr);
    }
    return curr == a;
}

std::unordered_set<BasicBlock*> DominatorTree::compute_idf(const std::unordered_set<BasicBlock*>& def_blocks) const {
    std::unordered_set<BasicBlock*> idf;
    std::queue<BasicBlock*> worklist;

    for (auto* b : def_blocks) {
        worklist.push(b);
    }

    while (!worklist.empty()) {
        auto* curr = worklist.front();
        worklist.pop();

        for (auto* frontier_block : get_dominance_frontier(curr)) {
            if (idf.insert(frontier_block).second) {
                worklist.push(frontier_block);
            }
        }
    }

    return idf;
}

} // namespace sysy
