#include "frontend/parser.hpp"
#include <iostream>

namespace sysy {

Parser::Parser(Lexer& lexer) : lexer_(lexer) {
    // Prime the first token
    token_buffer_.push_back(lexer_.next_token());
}

const Token& Parser::current_token() const {
    return token_buffer_[token_idx_];
}

const Token& Parser::peek_token(size_t ahead) {
    while (token_buffer_.size() <= token_idx_ + ahead) {
        token_buffer_.push_back(lexer_.next_token());
    }
    return token_buffer_[token_idx_ + ahead];
}

void Parser::advance() {
    token_idx_++;
    if (token_idx_ >= token_buffer_.size()) {
        token_buffer_.push_back(lexer_.next_token());
    }
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::check(TokenKind kind) const {
    return current_token().kind == kind;
}

bool Parser::expect(TokenKind kind, const std::string& msg) {
    if (check(kind)) {
        advance();
        return true;
    }
    error(msg + " (found: '" + current_token().text + "')");
    return false;
}

void Parser::error(const std::string& msg) {
    if (error_msg_.empty()) {
        error_msg_ = "Syntax error at line " + std::to_string(current_token().line) +
                     ", col " + std::to_string(current_token().col) + ": " + msg;
    }
}

std::unique_ptr<CompUnitAST> Parser::parse_comp_unit() {
    auto comp_unit = std::make_unique<CompUnitAST>();
    comp_unit->line = current_token().line;
    comp_unit->col = current_token().col;

    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::KwConst)) {
            comp_unit->items.push_back(parse_decl());
        } else if (check(TokenKind::KwVoid)) {
            comp_unit->items.push_back(parse_func_def());
        } else if (check(TokenKind::KwTensor)) {
            if (peek_token(2).kind == TokenKind::Ident && peek_token(3).kind == TokenKind::Lparen) {
                comp_unit->items.push_back(parse_func_def());
            } else {
                comp_unit->items.push_back(parse_decl());
            }
        } else if (check(TokenKind::KwInt) || check(TokenKind::KwFloat)) {
            // Lookahead 2 tokens: if token after IDENT is '(', it's a FuncDef, else VarDecl
            if (peek_token(1).kind == TokenKind::Ident && peek_token(2).kind == TokenKind::Lparen) {
                comp_unit->items.push_back(parse_func_def());
            } else {
                comp_unit->items.push_back(parse_decl());
            }
        } else {
            error("Unexpected token in CompUnit: " + current_token().text);
            advance();
        }
        if (has_error()) break;
    }
    return comp_unit;
}

std::unique_ptr<DeclAST> Parser::parse_decl() {
    if (check(TokenKind::KwConst)) {
        return parse_const_decl();
    }
    return parse_var_decl();
}

std::unique_ptr<DeclAST> Parser::parse_const_decl() {
    int line = current_token().line;
    int col = current_token().col;
    expect(TokenKind::KwConst, "Expected 'const'");
    bool is_tensor = match(TokenKind::KwTensor);

    DataType type = DataType::Int;
    if (match(TokenKind::KwInt)) {
        type = DataType::Int;
    } else if (match(TokenKind::KwFloat)) {
        type = DataType::Float;
    } else {
        error("Expected 'int' or 'float' after 'const'");
    }

    auto decl = std::make_unique<DeclAST>(type, true);
    decl->is_tensor = is_tensor;
    decl->line = line;
    decl->col = col;

    do {
        decl->defs.push_back(parse_var_def(true));
    } while (match(TokenKind::Comma));

    expect(TokenKind::Semicolon, "Expected ';' after const declaration");
    return decl;
}

std::unique_ptr<DeclAST> Parser::parse_var_decl() {
    int line = current_token().line;
    int col = current_token().col;

    bool is_tensor = false;
    if (match(TokenKind::KwTensor)) {
        is_tensor = true;
    }

    DataType type = DataType::Int;
    if (match(TokenKind::KwInt)) {
        type = DataType::Int;
    } else if (match(TokenKind::KwFloat)) {
        type = DataType::Float;
    } else {
        error("Expected 'int' or 'float'");
    }

    auto decl = std::make_unique<DeclAST>(type, false);
    decl->is_tensor = is_tensor;
    decl->line = line;
    decl->col = col;

    do {
        decl->defs.push_back(parse_var_def(false));
    } while (match(TokenKind::Comma));

    expect(TokenKind::Semicolon, "Expected ';' after variable declaration");
    return decl;
}

std::unique_ptr<VarDefAST> Parser::parse_var_def(bool is_const) {
    if (!check(TokenKind::Ident)) {
        error("Expected identifier in variable definition");
        return nullptr;
    }
    auto var_def = std::make_unique<VarDefAST>(current_token().text);
    var_def->line = current_token().line;
    var_def->col = current_token().col;
    advance();

    // Dimensions
    while (match(TokenKind::Lbracket)) {
        var_def->dims.push_back(parse_exp());
        expect(TokenKind::Rbracket, "Expected ']'");
    }

    if (match(TokenKind::Assign)) {
        var_def->init = parse_init_val();
    } else if (is_const) {
        error("Constant variable must have an initializer");
    }

    return var_def;
}

std::unique_ptr<InitValAST> Parser::parse_init_val() {
    auto init = std::make_unique<InitValAST>();
    init->line = current_token().line;
    init->col = current_token().col;

    if (match(TokenKind::Lbrace)) {
        init->is_list = true;
        if (!check(TokenKind::Rbrace)) {
            do {
                init->list.push_back(parse_init_val());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::Rbrace, "Expected '}' in initializer list");
    } else {
        init->is_list = false;
        init->expr = parse_exp();
    }
    return init;
}

std::unique_ptr<FuncDefAST> Parser::parse_func_def() {
    int line = current_token().line;
    int col = current_token().col;

    bool is_tensor = false;
    if (match(TokenKind::KwTensor)) {
        is_tensor = true;
    }

    DataType rt = DataType::Void;
    if (match(TokenKind::KwVoid)) {
        rt = DataType::Void;
    } else if (match(TokenKind::KwInt)) {
        rt = DataType::Int;
    } else if (match(TokenKind::KwFloat)) {
        rt = DataType::Float;
    }

    if (!check(TokenKind::Ident)) {
        error("Expected function name");
        return nullptr;
    }

    std::string name = current_token().text;
    advance();

    auto func_def = std::make_unique<FuncDefAST>(rt, name);
    func_def->is_tensor_return = is_tensor;
    func_def->line = line;
    func_def->col = col;

    expect(TokenKind::Lparen, "Expected '(' in function definition");

    if (!check(TokenKind::Rparen)) {
        do {
            func_def->params.push_back(parse_func_fparam());
        } while (match(TokenKind::Comma));
    }

    expect(TokenKind::Rparen, "Expected ')' after function parameters");
    func_def->body = parse_block();
    return func_def;
}

std::unique_ptr<FuncFParamAST> Parser::parse_func_fparam() {
    bool is_tensor = false;
    if (match(TokenKind::KwTensor)) {
        is_tensor = true;
    }

    DataType type = DataType::Int;
    if (match(TokenKind::KwInt)) {
        type = DataType::Int;
    } else if (match(TokenKind::KwFloat)) {
        type = DataType::Float;
    } else {
        error("Expected 'int' or 'float' in parameter");
    }

    if (!check(TokenKind::Ident)) {
        error("Expected parameter name");
        return nullptr;
    }

    std::string name = current_token().text;
    advance();

    bool is_arr = false;
    auto param = std::make_unique<FuncFParamAST>(type, name, is_arr);
    param->is_tensor = is_tensor;

    if (match(TokenKind::Lbracket)) {
        param->is_array = true;
        expect(TokenKind::Rbracket, "Expected ']' for array parameter first dimension");
        while (match(TokenKind::Lbracket)) {
            param->dims.push_back(parse_exp());
            expect(TokenKind::Rbracket, "Expected ']' in array parameter dimension");
        }
    }

    return param;
}

std::unique_ptr<BlockAST> Parser::parse_block() {
    int line = current_token().line;
    int col = current_token().col;

    expect(TokenKind::Lbrace, "Expected '{'");
    auto block = std::make_unique<BlockAST>();
    block->line = line;
    block->col = col;

    while (!check(TokenKind::Rbrace) && !check(TokenKind::Eof)) {
        if (check(TokenKind::KwConst) || check(TokenKind::KwTensor) || check(TokenKind::KwInt) || check(TokenKind::KwFloat)) {
            block->items.push_back(parse_decl());
        } else {
            block->items.push_back(parse_stmt());
        }
        if (has_error()) break;
    }

    expect(TokenKind::Rbrace, "Expected '}'");
    return block;
}

std::unique_ptr<StmtAST> Parser::parse_stmt() {
    int line = current_token().line;
    int col = current_token().col;

    if (check(TokenKind::Lbrace)) {
        return parse_block();
    }
    if (match(TokenKind::KwIf)) {
        expect(TokenKind::Lparen, "Expected '(' after 'if'");
        auto cond = parse_cond();
        expect(TokenKind::Rparen, "Expected ')' after if condition");
        auto then_stmt = parse_stmt();
        std::unique_ptr<StmtAST> else_stmt = nullptr;
        if (match(TokenKind::KwElse)) {
            else_stmt = parse_stmt();
        }
        auto s = std::make_unique<IfStmtAST>(std::move(cond), std::move(then_stmt), std::move(else_stmt));
        s->line = line;
        s->col = col;
        return s;
    }
    if (match(TokenKind::KwWhile)) {
        expect(TokenKind::Lparen, "Expected '(' after 'while'");
        auto cond = parse_cond();
        expect(TokenKind::Rparen, "Expected ')' after while condition");
        auto body = parse_stmt();
        auto s = std::make_unique<WhileStmtAST>(std::move(cond), std::move(body));
        s->line = line;
        s->col = col;
        return s;
    }
    if (match(TokenKind::KwBreak)) {
        expect(TokenKind::Semicolon, "Expected ';' after 'break'");
        auto s = std::make_unique<BreakStmtAST>();
        s->line = line;
        s->col = col;
        return s;
    }
    if (match(TokenKind::KwContinue)) {
        expect(TokenKind::Semicolon, "Expected ';' after 'continue'");
        auto s = std::make_unique<ContinueStmtAST>();
        s->line = line;
        s->col = col;
        return s;
    }
    if (match(TokenKind::KwReturn)) {
        std::unique_ptr<ExprAST> ret_expr = nullptr;
        if (!check(TokenKind::Semicolon)) {
            ret_expr = parse_exp();
        }
        expect(TokenKind::Semicolon, "Expected ';' after 'return'");
        auto s = std::make_unique<ReturnStmtAST>(std::move(ret_expr));
        s->line = line;
        s->col = col;
        return s;
    }
    if (match(TokenKind::Semicolon)) {
        auto s = std::make_unique<ExprStmtAST>(nullptr);
        s->line = line;
        s->col = col;
        return s;
    }

    // Must be either assignment or expression statement
    // First, check if it looks like LVal = Exp ;
    // We parse an expression. If it is an LValAST and next token is '=', treat as assign!
    auto expr = parse_exp();
    if (match(TokenKind::Assign)) {
        auto* lval = dynamic_cast<LValAST*>(expr.get());
        if (!lval) {
            error("Left-hand side of assignment must be an lvalue");
            return nullptr;
        }
        // Release ownership from expr to lval
        std::unique_ptr<LValAST> lval_ptr(static_cast<LValAST*>(expr.release()));
        auto rhs = parse_exp();
        expect(TokenKind::Semicolon, "Expected ';' after assignment");
        auto s = std::make_unique<AssignStmtAST>(std::move(lval_ptr), std::move(rhs));
        s->line = line;
        s->col = col;
        return s;
    }

    expect(TokenKind::Semicolon, "Expected ';' after expression statement");
    auto s = std::make_unique<ExprStmtAST>(std::move(expr));
    s->line = line;
    s->col = col;
    return s;
}

std::unique_ptr<ExprAST> Parser::parse_exp() {
    return parse_add_exp();
}

std::unique_ptr<ExprAST> Parser::parse_cond() {
    return parse_lor_exp();
}

std::unique_ptr<ExprAST> Parser::parse_lor_exp() {
    auto lhs = parse_land_exp();
    while (match(TokenKind::OrOr)) {
        int line = current_token().line;
        int col = current_token().col;
        auto rhs = parse_land_exp();
        auto bin = std::make_unique<BinaryExprAST>(BinaryOp::Or, std::move(lhs), std::move(rhs));
        bin->line = line;
        bin->col = col;
        lhs = std::move(bin);
    }
    return lhs;
}

std::unique_ptr<ExprAST> Parser::parse_land_exp() {
    auto lhs = parse_eq_exp();
    while (match(TokenKind::AndAnd)) {
        int line = current_token().line;
        int col = current_token().col;
        auto rhs = parse_eq_exp();
        auto bin = std::make_unique<BinaryExprAST>(BinaryOp::And, std::move(lhs), std::move(rhs));
        bin->line = line;
        bin->col = col;
        lhs = std::move(bin);
    }
    return lhs;
}

std::unique_ptr<ExprAST> Parser::parse_eq_exp() {
    auto lhs = parse_rel_exp();
    while (check(TokenKind::Eq) || check(TokenKind::Ne)) {
        BinaryOp op = check(TokenKind::Eq) ? BinaryOp::Eq : BinaryOp::Ne;
        int line = current_token().line;
        int col = current_token().col;
        advance();
        auto rhs = parse_rel_exp();
        auto bin = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
        bin->line = line;
        bin->col = col;
        lhs = std::move(bin);
    }
    return lhs;
}

std::unique_ptr<ExprAST> Parser::parse_rel_exp() {
    auto lhs = parse_add_exp();
    while (check(TokenKind::Lt) || check(TokenKind::Le) || check(TokenKind::Gt) || check(TokenKind::Ge)) {
        BinaryOp op = BinaryOp::Lt;
        if (check(TokenKind::Lt)) op = BinaryOp::Lt;
        else if (check(TokenKind::Le)) op = BinaryOp::Le;
        else if (check(TokenKind::Gt)) op = BinaryOp::Gt;
        else if (check(TokenKind::Ge)) op = BinaryOp::Ge;

        int line = current_token().line;
        int col = current_token().col;
        advance();
        auto rhs = parse_add_exp();
        auto bin = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
        bin->line = line;
        bin->col = col;
        lhs = std::move(bin);
    }
    return lhs;
}

std::unique_ptr<ExprAST> Parser::parse_add_exp() {
    auto lhs = parse_mul_exp();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        BinaryOp op = check(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        int line = current_token().line;
        int col = current_token().col;
        advance();
        auto rhs = parse_mul_exp();
        auto bin = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
        bin->line = line;
        bin->col = col;
        lhs = std::move(bin);
    }
    return lhs;
}

std::unique_ptr<ExprAST> Parser::parse_mul_exp() {
    auto lhs = parse_unary_exp();
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent) || check(TokenKind::At)) {
        BinaryOp op = BinaryOp::Mul;
        if (check(TokenKind::Star)) op = BinaryOp::Mul;
        else if (check(TokenKind::Slash)) op = BinaryOp::Div;
        else if (check(TokenKind::Percent)) op = BinaryOp::Mod;
        else if (check(TokenKind::At)) op = BinaryOp::MatMul;

        int line = current_token().line;
        int col = current_token().col;
        advance();
        auto rhs = parse_unary_exp();
        auto bin = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
        bin->line = line;
        bin->col = col;
        lhs = std::move(bin);
    }
    return lhs;
}

std::unique_ptr<ExprAST> Parser::parse_unary_exp() {
    int line = current_token().line;
    int col = current_token().col;

    if (check(TokenKind::Plus) || check(TokenKind::Minus) || check(TokenKind::Bang)) {
        UnaryOp op = UnaryOp::Plus;
        if (check(TokenKind::Plus)) op = UnaryOp::Plus;
        else if (check(TokenKind::Minus)) op = UnaryOp::Minus;
        else if (check(TokenKind::Bang)) op = UnaryOp::Not;

        advance();
        auto sub = parse_unary_exp();
        auto u = std::make_unique<UnaryExprAST>(op, std::move(sub));
        u->line = line;
        u->col = col;
        return u;
    }

    // Function call: IDENT '(' [ FuncRParams ] ')'
    if (check(TokenKind::Ident) && peek_token(1).kind == TokenKind::Lparen) {
        std::string name = current_token().text;
        advance(); // consume ident
        advance(); // consume '('

        auto call = std::make_unique<CallExprAST>(name);
        call->line = line;
        call->col = col;

        if (!check(TokenKind::Rparen)) {
            do {
                call->args.push_back(parse_exp());
            } while (match(TokenKind::Comma));
        }

        expect(TokenKind::Rparen, "Expected ')' after function arguments");
        return call;
    }

    return parse_primary_exp();
}

std::unique_ptr<ExprAST> Parser::parse_primary_exp() {
    int line = current_token().line;
    int col = current_token().col;

    if (match(TokenKind::Lparen)) {
        auto e = parse_exp();
        expect(TokenKind::Rparen, "Expected ')'");
        return e;
    }

    if (check(TokenKind::IntLiteral)) {
        auto lit = std::make_unique<IntLiteralAST>(current_token().int_val);
        lit->line = line;
        lit->col = col;
        advance();
        return lit;
    }

    if (check(TokenKind::StringLiteral)) {
        std::string s;
        while (check(TokenKind::StringLiteral)) { s += current_token().text; advance(); }
        auto lit = std::make_unique<StringLiteralAST>(std::move(s));
        lit->line=line;lit->col=col;
        return lit;
    }

    if (check(TokenKind::FloatLiteral)) {
        auto lit = std::make_unique<FloatLiteralAST>(current_token().float_val);
        lit->line = line;
        lit->col = col;
        advance();
        return lit;
    }

    if (check(TokenKind::Ident)) {
        return parse_lval();
    }

    error("Expected primary expression, got '" + current_token().text + "'");
    advance();
    return nullptr;
}

std::unique_ptr<LValAST> Parser::parse_lval() {
    int line = current_token().line;
    int col = current_token().col;

    std::string name = current_token().text;
    advance();

    auto lval = std::make_unique<LValAST>(name);
    lval->line = line;
    lval->col = col;

    while (match(TokenKind::Lbracket)) {
        lval->indices.push_back(parse_exp());
        expect(TokenKind::Rbracket, "Expected ']'");
    }

    return lval;
}

} // namespace sysy
