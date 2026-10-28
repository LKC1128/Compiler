#include "ir/instruction.hpp"
#include "ir/basic_block.hpp"
#include "ir/function.hpp"
#include <cassert>

namespace sysy {

Instruction::Instruction(Type* type, Opcode op, BasicBlock* parent, std::string name)
    : User(type, ValueKind::Instruction, std::move(name)), opcode_(op), parent_(parent) {}

std::string Instruction::get_opcode_name() const {
    switch (opcode_) {
        case Opcode::Add: return "add";
        case Opcode::Sub: return "sub";
        case Opcode::Mul: return "mul";
        case Opcode::SDiv: return "sdiv";
        case Opcode::SRem: return "srem";
        case Opcode::And: return "and";
        case Opcode::Or: return "or";
        case Opcode::Xor: return "xor";
        case Opcode::Shl: return "shl";
        case Opcode::LShr: return "lshr";
        case Opcode::AShr: return "ashr";
        case Opcode::SExt: return "sext";
        case Opcode::Trunc: return "trunc";
        case Opcode::FAdd: return "fadd";
        case Opcode::FSub: return "fsub";
        case Opcode::FMul: return "fmul";
        case Opcode::FDiv: return "fdiv";
        case Opcode::FNeg: return "fneg";
        case Opcode::ICmp: return "icmp";
        case Opcode::FCmp: return "fcmp";
        case Opcode::SIToFP: return "sitofp";
        case Opcode::FPToSI: return "fptosi";
        case Opcode::ZExt: return "zext";
        case Opcode::BitCast: return "bitcast";
        case Opcode::FPExt: return "fpext";
        case Opcode::Alloca: return "alloca";
        case Opcode::Load: return "load";
        case Opcode::Store: return "store";
        case Opcode::GetElementPtr: return "getelementptr";
        case Opcode::Branch: return "br";
        case Opcode::CondBranch: return "br";
        case Opcode::Return: return "ret";
        case Opcode::Phi: return "phi";
        case Opcode::Call: return "call";
    }
    return "unknown";
}

BinaryInst::BinaryInst(Opcode op, Value* lhs, Value* rhs, BasicBlock* parent, std::string name)
    : Instruction(lhs->type(), op, parent, std::move(name)) {
    add_operand(lhs);
    add_operand(rhs);
}

ICmpInst::ICmpInst(ICmpPred pred, Value* lhs, Value* rhs, TypeContext& ctx, BasicBlock* parent, std::string name)
    : Instruction(ctx.get_i1_type(), Opcode::ICmp, parent, std::move(name)), pred_(pred) {
    add_operand(lhs);
    add_operand(rhs);
}

std::string ICmpInst::get_pred_name() const {
    switch (pred_) {
        case ICmpPred::ULT: return "ult";
        case ICmpPred::ULE: return "ule";
        case ICmpPred::UGT: return "ugt";
        case ICmpPred::UGE: return "uge";
        case ICmpPred::EQ: return "eq";
        case ICmpPred::NE: return "ne";
        case ICmpPred::SLT: return "slt";
        case ICmpPred::SLE: return "sle";
        case ICmpPred::SGT: return "sgt";
        case ICmpPred::SGE: return "sge";
    }
    return "";
}

FCmpInst::FCmpInst(FCmpPred pred, Value* lhs, Value* rhs, TypeContext& ctx, BasicBlock* parent, std::string name)
    : Instruction(ctx.get_i1_type(), Opcode::FCmp, parent, std::move(name)), pred_(pred) {
    add_operand(lhs);
    add_operand(rhs);
}

std::string FCmpInst::get_pred_name() const {
    switch (pred_) {
        case FCmpPred::OEQ: return "oeq";
        case FCmpPred::ONE: return "one";
        case FCmpPred::UNE: return "une";
        case FCmpPred::OLT: return "olt";
        case FCmpPred::OLE: return "ole";
        case FCmpPred::OGT: return "ogt";
        case FCmpPred::OGE: return "oge";
    }
    return "";
}

CastInst::CastInst(Opcode op, Value* val, Type* target_type, BasicBlock* parent, std::string name)
    : Instruction(target_type, op, parent, std::move(name)) {
    add_operand(val);
}

AllocaInst::AllocaInst(Type* allocated_type, TypeContext& ctx, BasicBlock* parent, std::string name)
    : Instruction(ctx.get_pointer_type(allocated_type), Opcode::Alloca, parent, std::move(name)),
      allocated_type_(allocated_type) {}

LoadInst::LoadInst(Value* ptr, BasicBlock* parent, std::string name)
    : Instruction(static_cast<PointerType*>(ptr->type())->pointee(), Opcode::Load, parent, std::move(name)) {
    add_operand(ptr);
}

StoreInst::StoreInst(Value* val, Value* ptr, TypeContext& ctx, BasicBlock* parent)
    : Instruction(ctx.get_void_type(), Opcode::Store, parent, "") {
    add_operand(val);
    add_operand(ptr);
}

GetElementPtrInst::GetElementPtrInst(Value* base_ptr, std::vector<Value*> indices, TypeContext& ctx, BasicBlock* parent, std::string name)
    : Instruction(nullptr, Opcode::GetElementPtr, parent, std::move(name)) {
    assert(base_ptr->type()->is_pointer());
    add_operand(base_ptr);

    Type* curr = static_cast<PointerType*>(base_ptr->type())->pointee();
    source_elem_ty_ = curr;

    for (size_t i = 0; i < indices.size(); ++i) {
        add_operand(indices[i]);
        if (i == 0) {
            // First index dereferences the pointer
            continue;
        }
        if (curr->is_array()) {
            curr = static_cast<ArrayType*>(curr)->element_type();
        }
    }
    set_type(ctx.get_pointer_type(curr));
}

BranchInst::BranchInst(BasicBlock* target, TypeContext& ctx, BasicBlock* parent)
    : Instruction(ctx.get_void_type(), Opcode::Branch, parent, "") {
    add_operand(target);
}

BranchInst::BranchInst(Value* cond, BasicBlock* true_target, BasicBlock* false_target, TypeContext& ctx, BasicBlock* parent)
    : Instruction(ctx.get_void_type(), Opcode::CondBranch, parent, "") {
    add_operand(cond);
    add_operand(true_target);
    add_operand(false_target);
}

BasicBlock* BranchInst::target() const {
    assert(!is_conditional());
    return static_cast<BasicBlock*>(get_operand(0));
}

BasicBlock* BranchInst::true_target() const {
    assert(is_conditional());
    return static_cast<BasicBlock*>(get_operand(1));
}

BasicBlock* BranchInst::false_target() const {
    assert(is_conditional());
    return static_cast<BasicBlock*>(get_operand(2));
}

void BranchInst::set_target(BasicBlock* bb) {
    assert(!is_conditional());
    set_operand(0, bb);
}

void BranchInst::set_true_target(BasicBlock* bb) {
    assert(is_conditional());
    set_operand(1, bb);
}

void BranchInst::set_false_target(BasicBlock* bb) {
    assert(is_conditional());
    set_operand(2, bb);
}

ReturnInst::ReturnInst(TypeContext& ctx, BasicBlock* parent)
    : Instruction(ctx.get_void_type(), Opcode::Return, parent, "") {}

ReturnInst::ReturnInst(Value* val, TypeContext& ctx, BasicBlock* parent)
    : Instruction(ctx.get_void_type(), Opcode::Return, parent, "") {
    add_operand(val);
}

CallInst::CallInst(Function* callee, std::vector<Value*> args, BasicBlock* parent, std::string name)
    : Instruction(callee->return_type(), Opcode::Call, parent, std::move(name)) {
    add_operand(callee);
    for (auto* arg : args) {
        add_operand(arg);
    }
}

Function* CallInst::callee() const {
    return static_cast<Function*>(get_operand(0));
}

PhiInst::PhiInst(Type* type, BasicBlock* parent, std::string name)
    : Instruction(type, Opcode::Phi, parent, std::move(name)) {}

void PhiInst::add_incoming(Value* val, BasicBlock* bb) {
    add_operand(val);
    add_operand(bb);
}

Value* PhiInst::incoming_value(size_t i) const {
    return get_operand(i * 2);
}

BasicBlock* PhiInst::incoming_block(size_t i) const {
    return static_cast<BasicBlock*>(get_operand(i * 2 + 1));
}

void PhiInst::set_incoming_value(size_t i, Value* val) {
    set_operand(i * 2, val);
}

void PhiInst::set_incoming_block(size_t i, BasicBlock* bb) {
    set_operand(i * 2 + 1, bb);
}

Value* PhiInst::get_incoming_value_for_block(BasicBlock* bb) const {
    for (size_t i = 0; i < num_incoming(); ++i) {
        if (incoming_block(i) == bb) {
            return incoming_value(i);
        }
    }
    return nullptr;
}

void PhiInst::remove_incoming_block(BasicBlock* bb) {
    std::vector<std::pair<Value*, BasicBlock*>> remaining;
    for (size_t i = 0; i < num_incoming(); ++i) {
        if (incoming_block(i) != bb) {
            remaining.push_back({incoming_value(i), incoming_block(i)});
        }
    }
    drop_all_operands();
    for (auto& [v, b] : remaining) {
        add_operand(v);
        add_operand(b);
    }
}

} // namespace sysy
