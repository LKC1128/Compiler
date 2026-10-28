#pragma once

#include "ir/user.hpp"
#include "ir/representation.hpp"
#include <vector>
#include <string>
#include <memory>
#include <utility>

namespace sysy {

class BasicBlock;
class Function;

enum class Opcode {
    // Arithmetic
    Add, Sub, Mul, SDiv, SRem, And, Or, Xor, Shl, LShr, AShr,
    FAdd, FSub, FMul, FDiv, FNeg,

    // Comparison
    ICmp, FCmp,

    // Cast
    SIToFP, FPToSI, ZExt, SExt, Trunc, BitCast, FPExt,

    // Memory
    Alloca, Load, Store, GetElementPtr,

    // Control Flow
    Branch, CondBranch, Return,

    // SSA
    Phi,

    // Misc
    Call
};

enum class ICmpPred {
    EQ, NE, SLT, SLE, SGT, SGE, ULT, ULE, UGT, UGE
};

enum class FCmpPred {
    OEQ, ONE, UNE, OLT, OLE, OGT, OGE
};

class Instruction : public User {
public:
    Instruction(Type* type, Opcode op, BasicBlock* parent = nullptr, std::string name = "");
    ~Instruction() override = default;

    Opcode opcode() const { return opcode_; }
    BasicBlock* parent() const { return parent_; }
    void set_parent(BasicBlock* bb) { parent_ = bb; }

    IRLevel level() const { return level_; }
    unsigned state_flags() const { return state_; }
    void set_representation(IRLevel level, unsigned state) { level_=level; state_=state; }
    unsigned ub_flags() const { return ub_; }
    void set_ub_flags(unsigned flags) { ub_=flags; }
    bool has_ub_flag(UbFlags flag) const { return (ub_ & flag)!=0; }
    int region_id() const { return region_; }
    void set_region_id(int region) { region_=region; }
    bool is_integer_binary() const { return opcode_>=Opcode::Add && opcode_<=Opcode::AShr; }
    bool is_terminator() const {
        return opcode_ == Opcode::Branch || opcode_ == Opcode::CondBranch || opcode_ == Opcode::Return;
    }

    bool is_binary() const {
        return opcode_ >= Opcode::Add && opcode_ <= Opcode::FDiv;
    }

    bool is_phi() const { return opcode_ == Opcode::Phi; }
    bool is_call() const { return opcode_ == Opcode::Call; }
    bool is_alloca() const { return opcode_ == Opcode::Alloca; }
    bool is_load() const { return opcode_ == Opcode::Load; }
    bool is_store() const { return opcode_ == Opcode::Store; }
    bool is_gep() const { return opcode_ == Opcode::GetElementPtr; }

    std::string get_opcode_name() const;

private:
    IRLevel level_=IRLevel::MIR;
    unsigned state_=SF_SSA, ub_=0;
    int region_=-1;
    Opcode opcode_;
    BasicBlock* parent_;
};

// Binary Instruction
class BinaryInst : public Instruction {
public:
    BinaryInst(Opcode op, Value* lhs, Value* rhs, BasicBlock* parent = nullptr, std::string name = "");

    Value* lhs() const { return get_operand(0); }
    Value* rhs() const { return get_operand(1); }
};

class FNegInst : public Instruction {
public:
    explicit FNegInst(Value* val) : Instruction(val->type(), Opcode::FNeg) { add_operand(val); }
};

// ICmp Instruction
class ICmpInst : public Instruction {
public:
    ICmpInst(ICmpPred pred, Value* lhs, Value* rhs, TypeContext& ctx, BasicBlock* parent = nullptr, std::string name = "");

    ICmpPred predicate() const { return pred_; }
    void set_predicate(ICmpPred pred) { pred_=pred; }
    std::string get_pred_name() const;

    Value* lhs() const { return get_operand(0); }
    Value* rhs() const { return get_operand(1); }

private:
    ICmpPred pred_;
};

// FCmp Instruction
class FCmpInst : public Instruction {
public:
    FCmpInst(FCmpPred pred, Value* lhs, Value* rhs, TypeContext& ctx, BasicBlock* parent = nullptr, std::string name = "");

    FCmpPred predicate() const { return pred_; }
    void set_predicate(FCmpPred pred) { pred_=pred; }
    std::string get_pred_name() const;

    Value* lhs() const { return get_operand(0); }
    Value* rhs() const { return get_operand(1); }

private:
    FCmpPred pred_;
};

// Cast Instruction
class CastInst : public Instruction {
public:
    CastInst(Opcode op, Value* val, Type* target_type, BasicBlock* parent = nullptr, std::string name = "");

    Value* operand() const { return get_operand(0); }
};

// Alloca Instruction
class AllocaInst : public Instruction {
public:
    AllocaInst(Type* allocated_type, TypeContext& ctx, BasicBlock* parent = nullptr, std::string name = "");

    Type* allocated_type() const { return allocated_type_; }

private:
    Type* allocated_type_;
};

// Load Instruction
class LoadInst : public Instruction {
public:
    LoadInst(Value* ptr, BasicBlock* parent = nullptr, std::string name = "");

    Value* pointer() const { return get_operand(0); }
};

// Store Instruction
class StoreInst : public Instruction {
public:
    StoreInst(Value* val, Value* ptr, TypeContext& ctx, BasicBlock* parent = nullptr);

    Value* value() const { return get_operand(0); }
    Value* pointer() const { return get_operand(1); }
};

// GetElementPtr Instruction
class GetElementPtrInst : public Instruction {
public:
    GetElementPtrInst(Value* base_ptr, std::vector<Value*> indices, TypeContext& ctx, BasicBlock* parent = nullptr, std::string name = "");

    Value* base_pointer() const { return get_operand(0); }
    size_t num_indices() const { return num_operands() - 1; }
    Value* index(size_t i) const { return get_operand(i + 1); }
    Type* source_element_type() const { return source_elem_ty_; }

private:
    Type* source_elem_ty_;
};

// Branch Instruction
class BranchInst : public Instruction {
public:
    // Unconditional branch
    explicit BranchInst(BasicBlock* target, TypeContext& ctx, BasicBlock* parent = nullptr);

    // Conditional branch
    BranchInst(Value* cond, BasicBlock* true_target, BasicBlock* false_target, TypeContext& ctx, BasicBlock* parent = nullptr);

    bool is_conditional() const { return num_operands() == 3; }

    Value* condition() const {
        return is_conditional() ? get_operand(0) : nullptr;
    }

    BasicBlock* target() const;
    BasicBlock* true_target() const;
    BasicBlock* false_target() const;

    void set_target(BasicBlock* bb);
    void set_true_target(BasicBlock* bb);
    void set_false_target(BasicBlock* bb);
};

// Return Instruction
class ReturnInst : public Instruction {
public:
    // Void return
    explicit ReturnInst(TypeContext& ctx, BasicBlock* parent = nullptr);

    // Non-void return
    explicit ReturnInst(Value* val, TypeContext& ctx, BasicBlock* parent = nullptr);

    bool has_return_value() const { return num_operands() == 1; }
    Value* return_value() const { return has_return_value() ? get_operand(0) : nullptr; }
};

// Call Instruction
class CallInst : public Instruction {
public:
    CallInst(Function* callee, std::vector<Value*> args, BasicBlock* parent = nullptr, std::string name = "");

    Function* callee() const;
    size_t num_args() const { return num_operands() - 1; }
    Value* arg(size_t i) const { return get_operand(i + 1); }
};

// Phi Instruction
class PhiInst : public Instruction {
public:
    PhiInst(Type* type, BasicBlock* parent = nullptr, std::string name = "");

    void add_incoming(Value* val, BasicBlock* bb);
    size_t num_incoming() const { return num_operands() / 2; }

    Value* incoming_value(size_t i) const;
    BasicBlock* incoming_block(size_t i) const;

    void set_incoming_value(size_t i, Value* val);
    void set_incoming_block(size_t i, BasicBlock* bb);

    Value* get_incoming_value_for_block(BasicBlock* bb) const;
    void remove_incoming_block(BasicBlock* bb);
};

} // namespace sysy
