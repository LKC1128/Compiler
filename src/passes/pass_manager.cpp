#include "passes/pass_manager.hpp"
#include "passes/middle_end.hpp"
#include "ir/ir_verifier.hpp"
#include <stdexcept>
namespace sysy {
void PassManager::verify(const Module& m,const std::string& where) const {
    IRVerifier verifier;std::string error;
    if(!verifier.verify(m,error)) throw std::runtime_error("IR verification "+where+":\n"+error);
}
bool PassManager::run_pass(Pass& p,Module& m) {
    verify(m,"before "+p.name());
    for(const auto& f:m.functions()) if(!f->is_declaration()) {
        if(f->level()!=p.required_level() || (f->state_flags()&p.required_state())!=p.required_state())
            throw std::runtime_error("Pass "+p.name()+" requires "+level_name(p.required_level())+" with state "+std::to_string(p.required_state()));
    }
    int index=p.mandatory() ? 0 : ++attempted_;
    if(!p.mandatory() && limit_>=0 && index>limit_) {
        if(trace_) *trace_<<"SKIP "<<index<<" "<<p.name()<<"\n";return false;
    }
    auto before=canonical_rank(m);
    bool changed=p.run_on_module(m);
    auto transformed=canonical_rank(m);
    changed |= transformed.inst_count!=before.inst_count || transformed.bad_count!=before.bad_count;
    if(changed && p.name()!="canonicalize") for(auto& f:m.functions()) if(!f->is_declaration())
        f->set_representation(f->level(),f->state_flags()&~SF_CANONICAL);
    verify(m,"after "+p.name());
    auto after=canonical_rank(m);
    if(trace_) *trace_<<(p.mandatory()?"LOWER ":"PASS ")<<index<<" "<<p.name()<<" changed="<<changed
                     <<" rank=("<<before.inst_count<<","<<before.bad_count<<")->("<<after.inst_count<<","<<after.bad_count<<")\n";
    return changed;
}
bool PassManager::run(Module& m) {bool changed=false;for(auto& p:passes_) changed|=run_pass(*p,m);return changed;}
}
