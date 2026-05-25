#pragma once
#include <string>
#include <vector>
#include "Token.h"

class Lexer {
public:
    Lexer(const std::string& input);
    std::vector<Token> tokenize();
private:
    const std::string input;
    std::size_t pos;
    bool isAtEnd() const;
    char currentChar() const;
    void advance();
    void skipWhitespace();
    Token makeSimpleToken(TokenType type, const std::string& value) const;
    Token readNumber();
    Token readIdentifierOrFunction();
};