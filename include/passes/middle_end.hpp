#pragma once
#include "passes/pass_manager.hpp"
namespace sysy {
struct CanonicalRank {
    size_t inst_count=0,bad_count=0;
    bool operator<(const CanonicalRank& r) const {return inst_count<r.inst_count || (inst_count==r.inst_count && bad_count<r.bad_count);}
};
CanonicalRank canonical_rank(const Module& mod);
#define SYSY_MIDDLE_PASS(CLASS,NAME) class CLASS : public Pass {public: std::string name() const override {return NAME;} bool run_on_module(Module&) override;};
SYSY_MIDDLE_PASS(CanonicalizePass,"canonicalize")
SYSY_MIDDLE_PASS(GVNPass,"gvn")
SYSY_MIDDLE_PASS(InlinePass,"inline")
SYSY_MIDDLE_PASS(StrengthReducePass,"strength_reduce")
SYSY_MIDDLE_PASS(LICMPass,"licm")
SYSY_MIDDLE_PASS(InductionPass,"induction")
SYSY_MIDDLE_PASS(UnrollPass,"unroll")
#undef SYSY_MIDDLE_PASS
class LowerHIRPass : public Pass {
public: std::string name() const override {return "lower_hir";} bool run_on_module(Module&) override;
    bool mandatory() const override {return true;} IRLevel required_level() const override {return IRLevel::HIR;}
};
class LowerLIRPass : public Pass {
    bool allocate_;
public: explicit LowerLIRPass(bool allocate=true):allocate_(allocate) {}
    std::string name() const override {return "lower_lir";} bool run_on_module(Module&) override;
    bool mandatory() const override {return true;}
};
}
