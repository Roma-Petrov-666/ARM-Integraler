#include "Parser.h"
#include <stdexcept>

static void flattenOperands(std::unique_ptr<ASTNode> node, TokenType op,
                            std::vector<std::unique_ptr<ASTNode>>& out) {
    if (node->isMulti()) {
        auto& multi = node->asMulti();
        if (multi.op == op) {
            for (auto& operand : multi.operands) out.push_back(std::move(operand));
            return;
        }
    } else if (node->isBinary()) {
        auto& bin = node->asBinary();
        if (bin.op == op) {
            flattenOperands(std::move(bin.operands[0]), op, out);
            flattenOperands(std::move(bin.operands[1]), op, out);
            return;
        }
    }
    out.push_back(std::move(node));
}

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), expectOperand(true) {}

std::unique_ptr<ASTNode> Parser::parse() {
    std::vector<Token> operators;
    std::vector<std::unique_ptr<ASTNode>> nodes;
    for (size_t i = 0; i < tokens.size(); ++i) {
        Token token = tokens[i];
        if (token.type == TokenType::EOF_TOKEN) break;
        if (token.type == TokenType::NUMBER) {
            double num = std::stod(token.value);
            nodes.push_back(std::make_unique<NumberNode>(num));
        }
        else if (token.type == TokenType::IDENT) {
            nodes.push_back(std::make_unique<IdentNode>(token.value));
        }
        else if (token.type == TokenType::LPAREN) {
            operators.push_back(token);
        }
        else if (token.type == TokenType::RPAREN) {
            while (!operators.empty() && operators.back().type != TokenType::LPAREN) {
                buildNode(operators, nodes);
            }
            if (operators.empty()) throw std::runtime_error("Ошибка парсинга: лишняя ')'");
            operators.pop_back();
        }
        else if (isBinaryOperator(token.type) || isUnaryOperator(token.type)) {
            if (expectOperand && token.type == TokenType::MINUS) {
                token.type = TokenType::UNARY_MINUS;
            }
            while (!operators.empty() && operators.back().type != TokenType::LPAREN &&
                   shouldProcessBefore(operators.back().type, token.type)) {
                if (isUnaryOperator(operators.back().type) && isUnaryOperator(token.type)) break;
                buildNode(operators, nodes);
            }
            operators.push_back(token);
        }
        else {
            throw std::runtime_error("Ошибка парсинга: неизвестный токен");
        }
        if (token.type == TokenType::NUMBER || token.type == TokenType::IDENT || token.type == TokenType::RPAREN)
            expectOperand = false;
        else
            expectOperand = true;
    }
    while (!operators.empty()) {
        if (operators.back().type == TokenType::LPAREN)
            throw std::runtime_error("Ошибка парсинга: лишняя '('");
        buildNode(operators, nodes);
    }
    if (nodes.size() != 1)
        throw std::runtime_error("Ошибка парсинга: некорректное выражение");
    return std::move(nodes.back());
}

bool Parser::isBinaryOperator(TokenType type) const {
    return type == TokenType::PLUS || type == TokenType::MINUS ||
           type == TokenType::MUL  || type == TokenType::DIV ||
           type == TokenType::POW;
}

bool Parser::isUnaryOperator(TokenType type) const {
    return type == TokenType::UNARY_MINUS || type == TokenType::SIN || type == TokenType::COS ||
           type == TokenType::TG  || type == TokenType::CTG || type == TokenType::LN ||
           type == TokenType::EXP;
}

int Parser::getPriority(TokenType type) const {
    switch (type) {
    case TokenType::PLUS: case TokenType::MINUS:       return 1;
    case TokenType::MUL:  case TokenType::DIV:         return 2;
    case TokenType::UNARY_MINUS:                      return 3;
    case TokenType::SIN: case TokenType::COS: case TokenType::TG: case TokenType::CTG:
    case TokenType::LN:  case TokenType::POW: case TokenType::EXP: return 4;
    default: return 0;
    }
}

bool Parser::isRightAssociative(TokenType type) const {
    return type == TokenType::POW || type == TokenType::UNARY_MINUS;
}

bool Parser::shouldProcessBefore(TokenType top, TokenType current) const {
    if (isUnaryOperator(current)) return false;
    int topPri = getPriority(top), curPri = getPriority(current);
    if (topPri == curPri && !isRightAssociative(current)) return true;
    if (topPri > curPri) return true;
    return false;
}

void Parser::buildBinaryNode(std::vector<Token>& operators,
                             std::vector<std::unique_ptr<ASTNode>>& nodes) {
    if (operators.empty()) throw std::runtime_error("Стек операторов пуст");
    if (nodes.size() < 2) throw std::runtime_error("Недостаточно операндов");
    Token opToken = operators.back(); operators.pop_back();
    auto right = std::move(nodes.back()); nodes.pop_back();
    auto left  = std::move(nodes.back()); nodes.pop_back();
    if (opToken.type == TokenType::PLUS || opToken.type == TokenType::MUL) {
        std::vector<std::unique_ptr<ASTNode>> operands;
        flattenOperands(std::move(left), opToken.type, operands);
        flattenOperands(std::move(right), opToken.type, operands);
        nodes.push_back(std::make_unique<MultiOpNode>(opToken.type, std::move(operands)));
    } else {
        nodes.push_back(std::make_unique<BinaryOpNode>(opToken.type, std::move(left), std::move(right)));
    }
}

void Parser::buildUnaryNode(std::vector<Token>& operators,
                            std::vector<std::unique_ptr<ASTNode>>& nodes) {
    if (operators.empty()) throw std::runtime_error("Стек операторов пуст");
    if (nodes.empty()) throw std::runtime_error("Недостаточно операндов");
    Token opToken = operators.back(); operators.pop_back();
    auto operand = std::move(nodes.back()); nodes.pop_back();
    nodes.push_back(std::make_unique<UnaryOpNode>(opToken.type, std::move(operand)));
}

void Parser::buildNode(std::vector<Token>& operators,
                       std::vector<std::unique_ptr<ASTNode>>& nodes) {
    if (isBinaryOperator(operators.back().type))
        buildBinaryNode(operators, nodes);
    else if (isUnaryOperator(operators.back().type))
        buildUnaryNode(operators, nodes);
    else
        throw std::runtime_error("Неизвестный оператор в стеке");
}