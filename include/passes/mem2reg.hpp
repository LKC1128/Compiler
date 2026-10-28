#pragma once

#include "ir/function.hpp"
#include "passes/pass_manager.hpp"
#include "ir/module.hpp"
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace sysy {

class Mem2RegPass : public Pass {
public:
    Mem2RegPass() = default;
    std::string name() const override {return "mem2reg";}

    bool run_on_function(Function& fn);
    bool run_on_module(Module& mod) override;

private:
    bool is_promotable(const AllocaInst* alloca_inst) const;
    void collect_promotable_allocas(Function& fn, std::vector<AllocaInst*>& allocas) const;
};

} // namespace sysy
