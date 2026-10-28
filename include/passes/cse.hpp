#pragma once

#include "passes/pass_manager.hpp"

namespace sysy {

class CSEPass : public Pass {
public:
    std::string name() const override { return "cse"; }
    bool run_on_module(Module& mod) override;
    bool run_on_function(Function& fn);

private:
    bool is_pure_expression(const Instruction* inst) const;
    bool are_expressions_equivalent(const Instruction* a, const Instruction* b) const;
};

} // namespace sysy
