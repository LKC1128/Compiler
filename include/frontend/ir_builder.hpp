#pragma once

#include "frontend/ast.hpp"
#include "frontend/symbol_table.hpp"
#include "ir/module.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace sysy {

struct LoopContext {
    BasicBlock* cond_bb;
    BasicBlock* exit_bb;
};

class IRBuilder {
public:
    explicit IRBuilder(Module& module);

    bool build(const CompUnitAST& comp_unit);
    void set_assume_signed_no_overflow(bool enabled) { assume_signed_no_overflow_=enabled; }
    const std::string& error_message() const { return error_msg_; }

private:
    Module& module_;
    TypeContext& ctx_;
    SymbolTable sym_table_;
    Function* cur_func_ = nullptr;
    BasicBlock* cur_bb_ = nullptr;
    std::vector<LoopContext> loop_stack_;
    std::string error_msg_;
    size_t string_counter_ = 0;
    bool assume_signed_no_overflow_=false;
    Value* emit_source_integer(Opcode op,Value* lhs,Value* rhs);

    const ASTNode* diagnostic_node_ = nullptr;
    struct DiagnosticScope {
        IRBuilder& builder;
        const ASTNode* previous;
        DiagnosticScope(IRBuilder& owner,const ASTNode* node):builder(owner),previous(owner.diagnostic_node_) {
            if(node) builder.diagnostic_node_=node;
        }
        ~DiagnosticScope() {builder.diagnostic_node_=previous;}
    };
    void report_error(const std::string& msg, int line = 0, int col = 0);

    // Helpers
    Instruction* emit(std::unique_ptr<Instruction> inst);
    Value* coerce(Value* val, Type* target_type);
    Type* get_ir_type(DataType dt);

    struct ConstValue {
        bool floating;
        int32_t integer;
        float real;
        float as_float() const { return floating ? real : static_cast<float>(integer); }
        bool truth() const { return floating ? real != 0.0f : integer != 0; }
    };
    std::optional<ConstValue> try_eval_const(const ExprAST* expr);

    const ExprAST* scalar_initializer(const InitValAST* initializer);

    // Constant evaluation
    int32_t eval_const_int(const ExprAST* expr);
    float eval_const_float(const ExprAST* expr);

    // AST visitors
    void build_decl(const DeclAST& decl);
    void build_var_def(const VarDefAST& def, DataType type, bool is_const, bool is_tensor = false);
    void build_func_def(const FuncDefAST& func);
    void build_block(const BlockAST& block);
    void build_stmt(const StmtAST& stmt);

    Value* build_expr(const ExprAST* expr);
    Value* build_lval(const LValAST& lval, bool as_lvalue = false);
    Value* build_binary_expr(const BinaryExprAST& bin);
    Value* build_unary_expr(const UnaryExprAST& un);
    Value* build_call_expr(const CallExprAST& call);

    void build_condition_branch(const ExprAST* cond, BasicBlock* true_bb, BasicBlock* false_bb);

    struct TensorValue {
        Value* data;
        DataType element_type;
        std::vector<Value*> dims;
        Value* owner = nullptr;
    };
    std::unordered_map<std::string, const FuncDefAST*> function_definitions_;
    std::unordered_map<std::string, size_t> tensor_return_ranks_;
    std::unordered_map<std::string, std::vector<std::vector<int>>> parameter_shapes_;
    std::vector<Value*> tensor_allocations_;
    size_t tensor_return_rank(const std::string& function);
    TensorValue tensor_symbol(const Symbol& symbol);
    TensorValue tensor_lval(const LValAST& lval);
    TensorValue tensor_value(const ExprAST* expression);
    TensorValue allocate_tensor(const std::vector<Value*>& dims, DataType type);
    Value* tensor_element_count(const std::vector<Value*>& dims);
    void require_tensor_condition(Value* condition, const std::string& message);
    void require_tensor_shape(const std::vector<Value*>& left, const std::vector<Value*>& right);
    void release_tensor_allocations(Value* keep = nullptr);

    // Tensor operations
    bool is_tensor_expr(const ExprAST* expr);
    Value* get_tensor_base_ptr(const Symbol& sym);
    Value* build_tensor_expr(const ExprAST* expr, Value* dest_ptr, const Symbol& dest_sym);
    void emit_tensor_binary_loop(Value* dest_ptr, Value* lhs_ptr, Value* rhs_ptr,
                                 bool lhs_is_scalar, bool rhs_is_scalar,
                                 BinaryOp op, Value* total_elems, DataType type);
    void emit_tensor_unary_loop(Value* dest_ptr, Value* src_ptr, UnaryOp op, Value* total_elems, DataType type);
    void emit_tensor_matmul(Value* dest_ptr, Value* a_ptr, Value* b_ptr, Value* M, Value* K, Value* N, DataType type);
    void emit_tensor_copy_loop(Value* dest_ptr, Value* src_ptr, Value* total_elems, DataType type, bool broadcast = false);

    struct InitEntry {
        size_t index;
        size_t zero_count;
        const ExprAST* expression;
    };
    void plan_initializer(const InitValAST* initializer, const std::vector<int>& dims,
                          size_t dimension, size_t base, std::vector<InitEntry>& entries);

    // Constant array initialization uses the same brace layout as runtime arrays.
    void flatten_const_init_val(const InitValAST* init, const std::vector<int>& dims,
                                size_t dim_idx, std::vector<Constant*>& flat_consts, DataType elem_type);
};

} // namespace sysy
