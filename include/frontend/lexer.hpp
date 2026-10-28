#pragma once

#include "frontend/token.hpp"
#include <string>
#include <vector>

namespace sysy {

class Lexer {
public:
    explicit Lexer(std::string source);

    Token next_token();
    const std::string& error_message() const { return err_msg_; }

private:
    char peek() const;
    char advance();
    bool match(char expected);
    bool is_at_end() const;

    void skip_whitespace_and_comments();
    Token lex_identifier_or_keyword();
    Token lex_number();
    Token lex_string();

    std::string src_;
    size_t cursor_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::string err_msg_;
};

} // namespace sysy
