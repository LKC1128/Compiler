#pragma once

#include "ir/function.hpp"
#include "ir/module.hpp"

namespace sysy {

class PhiEliminationPass {
public:
    PhiEliminationPass() = default;

    bool run_on_function(Function& fn);
    bool run_on_module(Module& mod);

private:
    void split_critical_edges(Function& fn);
};

} // namespace sysy
