#pragma once
#include <vector>
#include <memory>
#include "Ast.h"
#include "Token.h"

class Parser {
public:
    Parser(const std::vector<Token>& tokens);
    std::unique_ptr<ASTNode> parse();
private:
    const std::vector<Token>& tokens;
    bool expectOperand;
    bool isBinaryOperator(TokenType type) const;
    bool isUnaryOperator(TokenType type) const;
    int getPriority(TokenType type) const;
    bool isRightAssociative(TokenType type) const;
    bool shouldProcessBefore(TokenType top, TokenType current) const;
    void buildBinaryNode(std::vector<Token>& operators, std::vector<std::unique_ptr<ASTNode>>& nodes);
    void buildUnaryNode(std::vector<Token>& operators, std::vector<std::unique_ptr<ASTNode>>& nodes);
    void buildNode(std::vector<Token>& operators, std::vector<std::unique_ptr<ASTNode>>& nodes);
};