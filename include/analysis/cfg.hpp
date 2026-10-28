#pragma once

#include "ir/function.hpp"
#include <vector>
#include <unordered_set>

namespace sysy {

class CFGAnalysis {
public:
    static void compute_cfg(Function& fn);
    static std::unordered_set<BasicBlock*> compute_reachable(Function& fn);
    static void eliminate_unreachable_blocks(Function& fn);
    static std::vector<BasicBlock*> get_postorder(Function& fn);
    static std::vector<BasicBlock*> get_reverse_postorder(Function& fn);

private:
    static void postorder_dfs(BasicBlock* bb, std::unordered_set<BasicBlock*>& visited, std::vector<BasicBlock*>& order);
};

} // namespace sysy
