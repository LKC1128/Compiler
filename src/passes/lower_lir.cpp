#include "passes/middle_end.hpp"
#include "passes/phi_elim.hpp"
#include "backend/location_assignment.hpp"
#include "ir/ir_verifier.hpp"
#include <stdexcept>
namespace sysy {
bool LowerLIRPass::run_on_module(Module& m) {
    PhiEliminationPass().run_on_module(m);
    IRVerifier verifier;std::string error;
    if(!verifier.verify(m,error))throw std::runtime_error("IR verification after phi_elim:\n"+error);
    for(auto& f:m.functions())if(!f->is_declaration()) {
        assign_machine_locations(*f,allocate_);f->set_representation(IRLevel::LIR,SF_REGALLOC);
    }
    return true;
}
}
