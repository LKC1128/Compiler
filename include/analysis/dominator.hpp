#pragma once

#include "ir/function.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sysy {

class DominatorTree {
public:
    explicit DominatorTree(Function& fn);

    BasicBlock* get_idom(BasicBlock* bb) const;
    const std::vector<BasicBlock*>& get_children(BasicBlock* bb) const;
    const std::unordered_set<BasicBlock*>& get_dominance_frontier(BasicBlock* bb) const;

    bool dominates(BasicBlock* a, BasicBlock* b) const;
    std::unordered_set<BasicBlock*> compute_idf(const std::unordered_set<BasicBlock*>& def_blocks) const;

private:
    void compute_idom(Function& fn);
    void build_tree(Function& fn);
    void compute_df(Function& fn);

    BasicBlock* intersect(BasicBlock* b1, BasicBlock* b2) const;

    std::unordered_map<BasicBlock*, BasicBlock*> idoms_;
    std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> dom_tree_children_;
    std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>> df_;
    std::unordered_map<BasicBlock*, int> rpo_indices_;
    std::vector<BasicBlock*> rpo_blocks_;

    static const std::vector<BasicBlock*> empty_children_;
    static const std::unordered_set<BasicBlock*> empty_df_;
};

} // namespace sysy
