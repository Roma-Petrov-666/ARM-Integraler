#pragma once
#include <string>

enum class TokenType {
    NUMBER, IDENT,
    PLUS, MINUS, MUL, DIV, POW,
    UNARY_MINUS,
    SIN, COS, TG, CTG, LN, EXP,
    ARCTAN, ARCSIN, SQRT,
    LPAREN, RPAREN,
    ERROR, EOF_TOKEN
};

struct Token {
    TokenType type = TokenType::ERROR;
    std::string value{};
    std::size_t position = 0;
};