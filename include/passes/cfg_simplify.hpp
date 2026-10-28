#pragma once

#include "passes/pass_manager.hpp"

namespace sysy {

class CFGSimplifyPass : public Pass {
public:
    std::string name() const override { return "cfg_simplify"; }
    bool run_on_module(Module& mod) override;
    bool run_on_function(Function& fn);

private:
    bool merge_blocks(Function& fn);
};

} // namespace sysy
