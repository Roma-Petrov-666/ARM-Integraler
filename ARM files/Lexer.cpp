#include "Lexer.h"
#include <cctype>
#include <map>

Lexer::Lexer(const std::string& input) : input(input), pos(0) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) break;
        char c = currentChar();
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            tokens.push_back(readNumber());
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(readIdentifierOrFunction());
        } else {
            switch (c) {
            case '+': tokens.push_back(makeSimpleToken(TokenType::PLUS, "+")); advance(); break;
            case '-': tokens.push_back(makeSimpleToken(TokenType::MINUS, "-")); advance(); break;
            case '*': tokens.push_back(makeSimpleToken(TokenType::MUL, "*")); advance(); break;
            case '/': tokens.push_back(makeSimpleToken(TokenType::DIV, "/")); advance(); break;
            case '^': tokens.push_back(makeSimpleToken(TokenType::POW, "^")); advance(); break;
            case '(': tokens.push_back(makeSimpleToken(TokenType::LPAREN, "(")); advance(); break;
            case ')': tokens.push_back(makeSimpleToken(TokenType::RPAREN, ")")); advance(); break;
            default: tokens.push_back(makeSimpleToken(TokenType::ERROR, std::string(1, c))); advance();
            }
        }
    }
    tokens.push_back(Token{TokenType::EOF_TOKEN, "", pos});
    return tokens;
}

bool Lexer::isAtEnd() const { return pos >= input.size(); }
char Lexer::currentChar() const { return input[pos]; }
void Lexer::advance() { if (!isAtEnd()) ++pos; }
void Lexer::skipWhitespace() {
    while (!isAtEnd() && std::isspace(static_cast<unsigned char>(currentChar()))) advance();
}
Token Lexer::makeSimpleToken(TokenType type, const std::string& value) const {
    return Token{type, value, pos};
}

Token Lexer::readNumber() {
    std::size_t start = pos;
    bool hasDot = false, hasDigit = false, error = false;
    while (!isAtEnd()) {
        char c = currentChar();
        if (std::isdigit(static_cast<unsigned char>(c))) { hasDigit = true; advance(); }
        else if (c == '.') { if (hasDot) error = true; hasDot = true; advance(); }
        else break;
    }
    if (input[start] == '.' || !hasDigit) error = true;
    std::string numberStr = input.substr(start, pos - start);
    if (error) return Token{TokenType::ERROR, numberStr, start};
    else return Token{TokenType::NUMBER, numberStr, start};
}

Token Lexer::readIdentifierOrFunction() {
    std::size_t start = pos;
    while (!isAtEnd()) {
        char c = currentChar();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') advance();
        else break;
    }
    std::string text = input.substr(start, pos - start);
    static const std::map<std::string, TokenType> tokenMap = {
        {"sin", TokenType::SIN}, {"cos", TokenType::COS},
        {"tan", TokenType::TG}, {"tg", TokenType::TG},
        {"ctg", TokenType::CTG}, {"ln", TokenType::LN},
        {"exp", TokenType::EXP}
    };
    auto it = tokenMap.find(text);
    if (it != tokenMap.end()) return Token{it->second, text, start};
    return Token{TokenType::IDENT, text, start};
}