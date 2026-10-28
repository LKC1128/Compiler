#pragma once

#include "frontend/lexer.hpp"
#include "frontend/ast.hpp"
#include <vector>
#include <string>
#include <memory>

namespace sysy {

class Parser {
public:
    explicit Parser(Lexer& lexer);

    std::unique_ptr<CompUnitAST> parse_comp_unit();
    bool has_error() const { return !error_msg_.empty(); }
    const std::string& error_message() const { return error_msg_; }

private:
    const Token& current_token() const;
    const Token& peek_token(size_t ahead = 1);
    void advance();
    bool match(TokenKind kind);
    bool check(TokenKind kind) const;
    bool expect(TokenKind kind, const std::string& msg);

    void error(const std::string& msg);

    // Parsing methods
    std::unique_ptr<DeclAST> parse_decl();
    std::unique_ptr<DeclAST> parse_const_decl();
    std::unique_ptr<DeclAST> parse_var_decl();
    std::unique_ptr<VarDefAST> parse_var_def(bool is_const);
    std::unique_ptr<InitValAST> parse_init_val();

    std::unique_ptr<FuncDefAST> parse_func_def();
    std::unique_ptr<FuncFParamAST> parse_func_fparam();

    std::unique_ptr<BlockAST> parse_block();
    std::unique_ptr<StmtAST> parse_stmt();

    std::unique_ptr<ExprAST> parse_exp();
    std::unique_ptr<ExprAST> parse_cond();
    std::unique_ptr<ExprAST> parse_lor_exp();
    std::unique_ptr<ExprAST> parse_land_exp();
    std::unique_ptr<ExprAST> parse_eq_exp();
    std::unique_ptr<ExprAST> parse_rel_exp();
    std::unique_ptr<ExprAST> parse_add_exp();
    std::unique_ptr<ExprAST> parse_mul_exp();
    std::unique_ptr<ExprAST> parse_unary_exp();
    std::unique_ptr<ExprAST> parse_primary_exp();
    std::unique_ptr<LValAST> parse_lval();

    Lexer& lexer_;
    std::vector<Token> token_buffer_;
    size_t token_idx_ = 0;
    std::string error_msg_;
};

} // namespace sysy
