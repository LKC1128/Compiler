#pragma once

#include <string>
#include <cstdint>

namespace sysy {

enum class TokenKind {
    Eof,
    Invalid,

    // Keywords
    KwConst,
    KwInt,
    KwFloat,
    KwVoid,
    KwIf,
    KwElse,
    KwWhile,
    KwBreak,
    KwContinue,
    KwReturn,
    KwTensor,

    // Identifiers & Literals
    Ident,
    IntLiteral,
    FloatLiteral,
    StringLiteral,

    // Operators
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    Percent,    // %
    At,         // @ (matrix multiplication)
    Assign,     // =
    Eq,         // ==
    Ne,         // !=
    Lt,         // <
    Le,         // <=
    Gt,         // >
    Ge,         // >=
    AndAnd,     // &&
    OrOr,       // ||
    Bang,       // !

    // Delimiters
    Lparen,     // (
    Rparen,     // )
    Lbracket,   // [
    Rbracket,   // ]
    Lbrace,     // {
    Rbrace,     // }
    Comma,      // ,
    Semicolon   // ;
};

struct Token {
    TokenKind kind = TokenKind::Invalid;
    std::string text;
    int32_t int_val = 0;
    float float_val = 0.0f;
    int line = 1;
    int col = 1;
};

std::string token_kind_to_string(TokenKind kind);

} // namespace sysy
