#pragma once

#include "passes/pass_manager.hpp"

namespace sysy {

class ConstFoldingPass : public Pass {
public:
    std::string name() const override { return "const_folding"; }
    bool run_on_module(Module& mod) override;
    bool run_on_function(Function& fn);

private:
    bool fold_binary(BinaryInst* bin);
    bool fold_icmp(ICmpInst* icmp);
    bool fold_fcmp(FCmpInst* fcmp);
    bool fold_cast(CastInst* cast);
    bool fold_branch(BranchInst* br, Function& fn);
};

} // namespace sysy
