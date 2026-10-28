#include "frontend/lexer.hpp"
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <cerrno>
#include <cmath>
#include <limits>
#include "ir/bit_utils.hpp"

namespace sysy {

std::string token_kind_to_string(TokenKind kind) {
    switch (kind) {
        case TokenKind::Eof: return "EOF";
        case TokenKind::Invalid: return "Invalid";
        case TokenKind::KwConst: return "const";
        case TokenKind::KwInt: return "int";
        case TokenKind::KwFloat: return "float";
        case TokenKind::KwVoid: return "void";
        case TokenKind::KwIf: return "if";
        case TokenKind::KwElse: return "else";
        case TokenKind::KwWhile: return "while";
        case TokenKind::KwBreak: return "break";
        case TokenKind::KwContinue: return "continue";
        case TokenKind::KwReturn: return "return";
        case TokenKind::Ident: return "Identifier";
        case TokenKind::IntLiteral: return "IntLiteral";
        case TokenKind::FloatLiteral: return "FloatLiteral";
        case TokenKind::StringLiteral: return "StringLiteral";
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::Assign: return "=";
        case TokenKind::Eq: return "==";
        case TokenKind::Ne: return "!=";
        case TokenKind::Lt: return "<";
        case TokenKind::Le: return "<=";
        case TokenKind::Gt: return ">";
        case TokenKind::Ge: return ">=";
        case TokenKind::AndAnd: return "&&";
        case TokenKind::OrOr: return "||";
        case TokenKind::Bang: return "!";
        case TokenKind::Lparen: return "(";
        case TokenKind::Rparen: return ")";
        case TokenKind::Lbracket: return "[";
        case TokenKind::Rbracket: return "]";
        case TokenKind::Lbrace: return "{";
        case TokenKind::Rbrace: return "}";
        case TokenKind::Comma: return ",";
        case TokenKind::Semicolon: return ";";
        case TokenKind::KwTensor: return "tensor";
        case TokenKind::At: return "@";
    }
    return "Unknown";
}

Lexer::Lexer(std::string source) : src_(std::move(source)) {}

char Lexer::peek() const {
    if (cursor_ >= src_.size()) return '\0';
    return src_[cursor_];
}

char Lexer::advance() {
    if (cursor_ >= src_.size()) return '\0';
    char c = src_[cursor_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

bool Lexer::is_at_end() const {
    return cursor_ >= src_.size();
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/') {
            if (cursor_ + 1 < src_.size()) {
                if (src_[cursor_ + 1] == '/') {
                    // Line comment
                    advance(); // skip /
                    advance(); // skip /
                    while (!is_at_end() && peek() != '\n') {
                        advance();
                    }
                } else if (src_[cursor_ + 1] == '*') {
                    // Block comment
                    advance(); // skip /
                    advance(); // skip *
                    bool closed = false;
                    while (!is_at_end()) {
                        if (peek() == '*' && cursor_ + 1 < src_.size() && src_[cursor_ + 1] == '/') {
                            advance(); // skip *
                            advance(); // skip /
                            closed = true;
                            break;
                        }
                        advance();
                    }
                    if (!closed) err_msg_ = "Unterminated block comment";
                } else {
                    break;
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

Token Lexer::lex_identifier_or_keyword() {
    int start_col = col_;
    int start_line = line_;
    size_t start_idx = cursor_;

    while (std::isalnum(peek()) || peek() == '_') {
        advance();
    }

    std::string text = src_.substr(start_idx, cursor_ - start_idx);
    Token tok;
    tok.line = start_line;
    tok.col = start_col;
    tok.text = text;

    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"const", TokenKind::KwConst},
        {"int", TokenKind::KwInt},
        {"float", TokenKind::KwFloat},
        {"void", TokenKind::KwVoid},
        {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},
        {"while", TokenKind::KwWhile},
        {"break", TokenKind::KwBreak},
        {"continue", TokenKind::KwContinue},
        {"return", TokenKind::KwReturn},
        {"tensor", TokenKind::KwTensor}
    };

    auto it = keywords.find(text);
    if (it != keywords.end()) {
        tok.kind = it->second;
    } else {
        tok.kind = TokenKind::Ident;
    }
    return tok;
}

Token Lexer::lex_number() {
    int start_col = col_;
    int start_line = line_;
    size_t start_idx = cursor_;

    bool is_float = false;

    if (peek() == '0') {
        advance();
        if (peek() == 'x' || peek() == 'X') {
            advance();
            while (std::isxdigit(peek())) {
                advance();
            }
            if (peek() == '.') {
                is_float = true;
                advance();
                while (std::isxdigit(peek())) {
                    advance();
                }
            }
            if (peek() == 'p' || peek() == 'P') {
                is_float = true;
                advance();
                if (peek() == '+' || peek() == '-') {
                    advance();
                }
                while (std::isdigit(peek())) {
                    advance();
                }
            }
        } else {
            // Octal integer or float starting with 0
            while (std::isdigit(peek())) {
                advance();
            }
            if (peek() == '.') {
                is_float = true;
                advance();
                while (std::isdigit(peek())) {
                    advance();
                }
            }
            if (peek() == 'e' || peek() == 'E') {
                is_float = true;
                advance();
                if (peek() == '+' || peek() == '-') {
                    advance();
                }
                while (std::isdigit(peek())) {
                    advance();
                }
            }
        }
    } else if (peek() == '.') {
        is_float = true;
        advance();
        while (std::isdigit(peek())) {
            advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            while (std::isdigit(peek())) {
                advance();
            }
        }
    } else {
        while (std::isdigit(peek())) {
            advance();
        }
        if (peek() == '.') {
            is_float = true;
            advance();
            while (std::isdigit(peek())) {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            while (std::isdigit(peek())) {
                advance();
            }
        }
    }

    std::string text = src_.substr(start_idx, cursor_ - start_idx);
    Token tok;
    tok.line = start_line;
    tok.col = start_col;
    tok.text = text;

    if (is_float) {
        tok.kind = TokenKind::FloatLiteral;
        char* endptr = nullptr;
        errno = 0;
        tok.float_val = std::strtof(text.c_str(), &endptr);
        bool missing_hex_exponent = text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') && text.find_first_of("pP") == std::string::npos;
        if (endptr != text.c_str() + text.size() || missing_hex_exponent || (errno == ERANGE && !std::isfinite(tok.float_val))) {
            tok.kind = TokenKind::Invalid;
            err_msg_ = "Invalid floating literal: " + text;
        }
    } else {
        tok.kind = TokenKind::IntLiteral;
        char* endptr = nullptr;
        errno = 0;
        unsigned long long n = std::strtoull(text.c_str(), &endptr, 0);
        if (endptr != text.c_str() + text.size() || errno == ERANGE || n > UINT32_MAX) {
            tok.kind = TokenKind::Invalid;
            err_msg_ = "Invalid integer literal: " + text;
        } else tok.int_val = bit_copy<int32_t>(static_cast<uint32_t>(n));
    }
    return tok;
}

Token Lexer::lex_string() {
    Token tok; tok.kind=TokenKind::StringLiteral;tok.line=line_;tok.col=col_;
    advance();
    while (!is_at_end() && peek() != '"') {
        char c=advance();
        if(c=='\n' || c=='\r') {err_msg_="Newline in string literal";tok.kind=TokenKind::Invalid;return tok;}
        if(c!='\\') {tok.text.push_back(c);continue;}
        if(is_at_end()) break;
        c=advance();
        switch(c) {
        case 'n': tok.text.push_back('\n');break;
        case 'r': tok.text.push_back('\r');break;
        case 't': tok.text.push_back('\t');break;
        case 'a': tok.text.push_back('\a');break;
        case 'b': tok.text.push_back('\b');break;
        case 'f': tok.text.push_back('\f');break;
        case 'v': tok.text.push_back('\v');break;
        case '\\': case '"': case '\'': tok.text.push_back(c);break;
        default:
            if(c>='0' && c<='7') {
                unsigned n=c-'0';int digits=1;
                while(digits<3 && peek()>='0' && peek()<='7') {n=n*8+(advance()-'0');++digits;}
                tok.text.push_back(static_cast<char>(n & 255));
            } else if(c=='x') {
                if(!std::isxdigit(static_cast<unsigned char>(peek()))) {err_msg_="Empty hex string escape";tok.kind=TokenKind::Invalid;return tok;}
                unsigned n=0;
                while(std::isxdigit(static_cast<unsigned char>(peek()))) {
                    char d=advance();n=((n*16)+(d<='9'?d-'0':std::tolower(static_cast<unsigned char>(d))-'a'+10)) & 255;
                }
                tok.text.push_back(static_cast<char>(n));
            } else {err_msg_="Invalid string escape";tok.kind=TokenKind::Invalid;return tok;}
        }
    }
    if(!match('"')) {err_msg_="Unterminated string literal";tok.kind=TokenKind::Invalid;}
    return tok;
}

Token Lexer::next_token() {
    skip_whitespace_and_comments();

    if (is_at_end()) {
        Token tok;
        tok.kind = TokenKind::Eof;
        tok.line = line_;
        tok.col = col_;
        return tok;
    }

    int start_line = line_;
    int start_col = col_;
    char c = peek();

    if (c == '"') return lex_string();

    if (std::isalpha(c) || c == '_') {
        return lex_identifier_or_keyword();
    }

    if (std::isdigit(c) || (c == '.' && cursor_ + 1 < src_.size() && std::isdigit(src_[cursor_ + 1]))) {
        return lex_number();
    }

    // Punctuation & operators
    advance();
    Token tok;
    tok.line = start_line;
    tok.col = start_col;

    switch (c) {
        case '+': tok.kind = TokenKind::Plus; tok.text = "+"; break;
        case '-': tok.kind = TokenKind::Minus; tok.text = "-"; break;
        case '*': tok.kind = TokenKind::Star; tok.text = "*"; break;
        case '/': tok.kind = TokenKind::Slash; tok.text = "/"; break;
        case '%': tok.kind = TokenKind::Percent; tok.text = "%"; break;
        case '@': tok.kind = TokenKind::At; tok.text = "@"; break;
        case '(': tok.kind = TokenKind::Lparen; tok.text = "("; break;
        case ')': tok.kind = TokenKind::Rparen; tok.text = ")"; break;
        case '[': tok.kind = TokenKind::Lbracket; tok.text = "["; break;
        case ']': tok.kind = TokenKind::Rbracket; tok.text = "]"; break;
        case '{': tok.kind = TokenKind::Lbrace; tok.text = "{"; break;
        case '}': tok.kind = TokenKind::Rbrace; tok.text = "}"; break;
        case ',': tok.kind = TokenKind::Comma; tok.text = ","; break;
        case ';': tok.kind = TokenKind::Semicolon; tok.text = ";"; break;
        case '!':
            if (match('=')) {
                tok.kind = TokenKind::Ne;
                tok.text = "!=";
            } else {
                tok.kind = TokenKind::Bang;
                tok.text = "!";
            }
            break;
        case '=':
            if (match('=')) {
                tok.kind = TokenKind::Eq;
                tok.text = "==";
            } else {
                tok.kind = TokenKind::Assign;
                tok.text = "=";
            }
            break;
        case '<':
            if (match('=')) {
                tok.kind = TokenKind::Le;
                tok.text = "<=";
            } else {
                tok.kind = TokenKind::Lt;
                tok.text = "<";
            }
            break;
        case '>':
            if (match('=')) {
                tok.kind = TokenKind::Ge;
                tok.text = ">=";
            } else {
                tok.kind = TokenKind::Gt;
                tok.text = ">";
            }
            break;
        case '&':
            if (match('&')) {
                tok.kind = TokenKind::AndAnd;
                tok.text = "&&";
            } else {
                tok.kind = TokenKind::Invalid;
                tok.text = "&";
            }
            break;
        case '|':
            if (match('|')) {
                tok.kind = TokenKind::OrOr;
                tok.text = "||";
            } else {
                tok.kind = TokenKind::Invalid;
                tok.text = "|";
            }
            break;
        default:
            tok.kind = TokenKind::Invalid;
            tok.text = std::string(1, c);
            break;
    }

    return tok;
}

} // namespace sysy
