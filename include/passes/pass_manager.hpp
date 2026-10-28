#pragma once
#include "ir/module.hpp"
#include <string>
#include <vector>
#include <memory>
#include <ostream>
namespace sysy {
class Pass {
public:
    virtual ~Pass()=default;
    virtual std::string name() const=0;
    virtual bool run_on_module(Module&)=0;
    virtual IRLevel required_level() const {return IRLevel::MIR;}
    virtual unsigned required_state() const {return SF_SSA;}
    virtual bool mandatory() const {return false;}
};
class PassManager {
public:
    explicit PassManager(int bisect_limit=-1,std::ostream* trace=nullptr):limit_(bisect_limit),trace_(trace) {}
    void add_pass(std::unique_ptr<Pass> p) {passes_.push_back(std::move(p));}
    bool run(Module&);
    bool run_pass(Pass&,Module&);
    void verify(const Module&,const std::string& context) const;
    int attempted() const {return attempted_;}
private:
    int limit_=-1,attempted_=0;std::ostream* trace_=nullptr;
    std::vector<std::unique_ptr<Pass>> passes_;
};
}
