#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace sysy {

enum class DataType {
    Void,
    Int,
    Float
};

enum class UnaryOp {
    Plus,
    Minus,
    Not
};

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod, MatMul,
    Lt, Le, Gt, Ge, Eq, Ne,
    And, Or
};

class ASTNode {
public:
    int line = 1;
    int col = 1;
    virtual ~ASTNode() = default;
};

class ExprAST : public ASTNode {};

class IntLiteralAST : public ExprAST {
public:
    int32_t value;
    explicit IntLiteralAST(int32_t val) : value(val) {}
};

class FloatLiteralAST : public ExprAST {
public:
    float value;
    explicit FloatLiteralAST(float val) : value(val) {}
};

class StringLiteralAST : public ExprAST {
public:
    std::string value;
    explicit StringLiteralAST(std::string s) : value(std::move(s)) {}
};

class LValAST : public ExprAST {
public:
    std::string name;
    std::vector<std::unique_ptr<ExprAST>> indices;
    explicit LValAST(std::string n) : name(std::move(n)) {}
};

class UnaryExprAST : public ExprAST {
public:
    UnaryOp op;
    std::unique_ptr<ExprAST> expr;
    UnaryExprAST(UnaryOp op, std::unique_ptr<ExprAST> expr)
        : op(op), expr(std::move(expr)) {}
};

class BinaryExprAST : public ExprAST {
public:
    BinaryOp op;
    std::unique_ptr<ExprAST> lhs;
    std::unique_ptr<ExprAST> rhs;
    BinaryExprAST(BinaryOp op, std::unique_ptr<ExprAST> lhs, std::unique_ptr<ExprAST> rhs)
        : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

class CallExprAST : public ExprAST {
public:
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;
    explicit CallExprAST(std::string name) : callee(std::move(name)) {}
};

class InitValAST : public ASTNode {
public:
    bool is_list = false;
    std::unique_ptr<ExprAST> expr;
    std::vector<std::unique_ptr<InitValAST>> list;
};

class VarDefAST : public ASTNode {
public:
    std::string name;
    std::vector<std::unique_ptr<ExprAST>> dims;
    std::unique_ptr<InitValAST> init;
    explicit VarDefAST(std::string n) : name(std::move(n)) {}
};

class DeclAST : public ASTNode {
public:
    DataType type;
    bool is_const = false;
    bool is_tensor = false;
    std::vector<std::unique_ptr<VarDefAST>> defs;
    DeclAST(DataType t, bool is_const) : type(t), is_const(is_const) {}
};

class StmtAST : public ASTNode {};

class AssignStmtAST : public StmtAST {
public:
    std::unique_ptr<LValAST> lval;
    std::unique_ptr<ExprAST> expr;
    AssignStmtAST(std::unique_ptr<LValAST> lv, std::unique_ptr<ExprAST> e)
        : lval(std::move(lv)), expr(std::move(e)) {}
};

class ExprStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> expr;
    explicit ExprStmtAST(std::unique_ptr<ExprAST> e = nullptr) : expr(std::move(e)) {}
};

class BlockAST : public StmtAST {
public:
    std::vector<std::unique_ptr<ASTNode>> items; // DeclAST or StmtAST
};

class IfStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<StmtAST> then_stmt;
    std::unique_ptr<StmtAST> else_stmt;
    IfStmtAST(std::unique_ptr<ExprAST> c, std::unique_ptr<StmtAST> t, std::unique_ptr<StmtAST> e = nullptr)
        : cond(std::move(c)), then_stmt(std::move(t)), else_stmt(std::move(e)) {}
};

class WhileStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> cond;
    std::unique_ptr<StmtAST> body;
    WhileStmtAST(std::unique_ptr<ExprAST> c, std::unique_ptr<StmtAST> b)
        : cond(std::move(c)), body(std::move(b)) {}
};

class BreakStmtAST : public StmtAST {};
class ContinueStmtAST : public StmtAST {};

class ReturnStmtAST : public StmtAST {
public:
    std::unique_ptr<ExprAST> expr;
    explicit ReturnStmtAST(std::unique_ptr<ExprAST> e = nullptr) : expr(std::move(e)) {}
};

class FuncFParamAST : public ASTNode {
public:
    DataType type;
    std::string name;
    bool is_array = false;
    bool is_tensor = false;
    std::vector<std::unique_ptr<ExprAST>> dims;
    FuncFParamAST(DataType t, std::string n, bool arr = false)
        : type(t), name(std::move(n)), is_array(arr) {}
};

class FuncDefAST : public ASTNode {
public:
    DataType return_type;
    std::string name;
    bool is_tensor_return = false;
    std::vector<std::unique_ptr<FuncFParamAST>> params;
    std::unique_ptr<BlockAST> body;
    FuncDefAST(DataType rt, std::string n) : return_type(rt), name(std::move(n)) {}
};

class CompUnitAST : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> items; // DeclAST or FuncDefAST
};

} // namespace sysy
