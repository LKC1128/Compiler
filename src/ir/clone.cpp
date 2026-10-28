#include "ir/clone.hpp"
#include <stdexcept>
namespace sysy {
bool is_pure_expression(const Instruction& i) {
    return i.is_binary() || i.opcode()==Opcode::FNeg || i.opcode()==Opcode::ICmp || i.opcode()==Opcode::FCmp ||
        dynamic_cast<const CastInst*>(&i) || i.is_gep();
}
std::unique_ptr<Instruction> clone_instruction(const Instruction& i,TypeContext& ctx) {
    std::unique_ptr<Instruction> n;
    if(auto* p=dynamic_cast<const BinaryInst*>(&i)) n=std::make_unique<BinaryInst>(p->opcode(),p->lhs(),p->rhs());
    else if(auto* p=dynamic_cast<const ICmpInst*>(&i)) n=std::make_unique<ICmpInst>(p->predicate(),p->lhs(),p->rhs(),ctx);
    else if(auto* p=dynamic_cast<const FCmpInst*>(&i)) n=std::make_unique<FCmpInst>(p->predicate(),p->lhs(),p->rhs(),ctx);
    else if(auto* p=dynamic_cast<const CastInst*>(&i)) n=std::make_unique<CastInst>(p->opcode(),p->operand(),p->type());
    else if(i.opcode()==Opcode::FNeg) n=std::make_unique<FNegInst>(i.get_operand(0));
    else if(auto* p=dynamic_cast<const AllocaInst*>(&i)) n=std::make_unique<AllocaInst>(p->allocated_type(),ctx);
    else if(auto* p=dynamic_cast<const LoadInst*>(&i)) n=std::make_unique<LoadInst>(p->pointer());
    else if(auto* p=dynamic_cast<const StoreInst*>(&i)) n=std::make_unique<StoreInst>(p->value(),p->pointer(),ctx);
    else if(auto* p=dynamic_cast<const GetElementPtrInst*>(&i)) {
        std::vector<Value*> indices;for(size_t k=0;k<p->num_indices();++k) indices.push_back(p->index(k));
        n=std::make_unique<GetElementPtrInst>(p->base_pointer(),indices,ctx);
    } else if(auto* p=dynamic_cast<const BranchInst*>(&i)) {
        if(p->is_conditional()) n=std::make_unique<BranchInst>(p->condition(),p->true_target(),p->false_target(),ctx);
        else n=std::make_unique<BranchInst>(p->target(),ctx);
    } else if(auto* p=dynamic_cast<const ReturnInst*>(&i)) {
        if(p->has_return_value()) n=std::make_unique<ReturnInst>(p->return_value(),ctx);
        else n=std::make_unique<ReturnInst>(ctx);
    } else if(auto* p=dynamic_cast<const CallInst*>(&i)) {
        std::vector<Value*> args;for(size_t k=0;k<p->num_args();++k) args.push_back(p->arg(k));
        n=std::make_unique<CallInst>(p->callee(),args);
    } else if(auto* p=dynamic_cast<const PhiInst*>(&i)) {
        auto phi=std::make_unique<PhiInst>(p->type());
        for(size_t k=0;k<p->num_incoming();++k) phi->add_incoming(p->incoming_value(k),p->incoming_block(k));
        n=std::move(phi);
    } else throw std::runtime_error("Cannot clone instruction "+i.get_opcode_name());
    n->set_ub_flags(i.ub_flags());n->set_name(i.name());return n;
}
}
