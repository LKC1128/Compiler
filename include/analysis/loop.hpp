#pragma once
#include "analysis/dominator.hpp"
namespace sysy {
struct NaturalLoop {
    BasicBlock* header=nullptr;
    std::unordered_set<BasicBlock*> blocks;
    std::vector<BasicBlock*> latches;
};
std::vector<NaturalLoop> find_natural_loops(Function& fn);
// Redirect outside edges and merge their phi values. Returns nullptr for a
// loop without an outside entry. All CFG/SSA edges are updated together.
BasicBlock* ensure_preheader(Function& fn,const NaturalLoop& loop,TypeContext& ctx);
}
