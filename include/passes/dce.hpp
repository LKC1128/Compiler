#pragma once

#include "passes/pass_manager.hpp"

namespace sysy {

class DCEPass : public Pass {
public:
    std::string name() const override { return "dce"; }
    bool run_on_module(Module& mod) override;
    bool run_on_function(Function& fn);

private:
    bool is_instruction_dead(const Instruction* inst) const;
};

} // namespace sysy
