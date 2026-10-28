#pragma once

#include "passes/pass_manager.hpp"

namespace sysy {

class PhiSimplifyPass : public Pass {
public:
    std::string name() const override { return "phi_simplify"; }
    bool run_on_module(Module& mod) override;
    bool run_on_function(Function& fn);
};

} // namespace sysy
