#include "Normalizer.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <functional>

double evaluateUnary(TokenType op, double value) {
    switch (op) {
    case TokenType::UNARY_MINUS: return -value;
    case TokenType::SIN: return std::sin(value);
    case TokenType::COS: return std::cos(value);
    case TokenType::TG: return std::tan(value);
    case TokenType::CTG: return 1.0 / std::tan(value);
    case TokenType::LN: return std::log(value);
    case TokenType::EXP: return std::exp(value);
    default: return 0.0;
    }
}

double evaluateBinary(TokenType op, double left, double right) {
    switch (op) {
    case TokenType::PLUS: return left + right;
    case TokenType::MINUS: return left - right;
    case TokenType::MUL: return left * right;
    case TokenType::DIV: return left / right;
    case TokenType::POW: return std::pow(left, right);
    default: return 0.0;
    }
}

double evaluateMulti(TokenType op, const std::vector<double>& values) {
    if (values.empty()) return (op == TokenType::PLUS) ? 0.0 : 1.0;
    double result = (op == TokenType::PLUS) ? 0.0 : 1.0;
    for (double v : values) result = (op == TokenType::PLUS) ? result + v : result * v;
    return result;
}

static std::unique_ptr<ASTNode> tryEvaluateConstant(const ASTNode* node) {
    if (!node) return nullptr;
    if (node->isNumber()) return node->clone();
    if (node->isIdent()) return nullptr;
    if (node->isUnary()) {
        auto& u = node->asUnary();
        auto argVal = tryEvaluateConstant(u.operand(0));
        if (argVal && argVal->isNumber())
            return std::make_unique<NumberNode>(evaluateUnary(u.op, argVal->asNumber().value));
        return nullptr;
    }
    if (node->isBinary()) {
        auto& b = node->asBinary();
        auto leftVal = tryEvaluateConstant(b.operand(0));
        auto rightVal = tryEvaluateConstant(b.operand(1));
        if (leftVal && leftVal->isNumber() && rightVal && rightVal->isNumber())
            return std::make_unique<NumberNode>(evaluateBinary(b.op, leftVal->asNumber().value, rightVal->asNumber().value));
        return nullptr;
    }
    if (node->isMulti()) {
        auto& m = node->asMulti();
        std::vector<double> values;
        for (size_t i = 0; i < m.operandCount(); ++i) {
            auto val = tryEvaluateConstant(m.operand(i));
            if (val && val->isNumber()) values.push_back(val->asNumber().value);
            else return nullptr;
        }
        return std::make_unique<NumberNode>(evaluateMulti(m.op, values));
    }
    return nullptr;
}

static std::unique_ptr<ASTNode> simplifySigns(std::unique_ptr<ASTNode> node) {
    if (!node) return nullptr;

    if (node->isUnary()) {
        auto& u = node->asUnary();
        if (u.operand(0)) u.operands[0] = simplifySigns(std::move(u.operands[0]));
    } else if (node->isBinary()) {
        auto& b = node->asBinary();
        if (b.operand(0)) b.operands[0] = simplifySigns(std::move(b.operands[0]));
        if (b.operand(1)) b.operands[1] = simplifySigns(std::move(b.operands[1]));
    } else if (node->isMulti()) {
        auto& m = node->asMulti();
        for (size_t i = 0; i < m.operandCount(); ++i)
            if (m.operand(i)) m.operands[i] = simplifySigns(std::move(m.operands[i]));
    }

    if (node->isUnary()) {
        auto& u = node->asUnary();
        if (u.op == TokenType::UNARY_MINUS) {
            if (u.operand(0)->isUnary() && u.operand(0)->asUnary().op == TokenType::UNARY_MINUS) {
                auto inner = std::move(u.operands[0]);
                auto innerUnary = std::move(inner->asUnary().operands[0]);
                return simplifySigns(std::move(innerUnary));
            }
        }
    }

    if (node->isMulti() && node->asMulti().op == TokenType::PLUS) {
        auto& m = node->asMulti();
        std::vector<std::unique_ptr<ASTNode>> newOps;
        for (auto& operand : m.operands) {
            newOps.push_back(std::move(operand));
        }
        if (newOps.size() == 1 && newOps[0]->isUnary() && newOps[0]->asUnary().op == TokenType::UNARY_MINUS) {
            return std::move(newOps[0]);
        }
        m.operands = std::move(newOps);
    }

    if (node->isBinary() && node->asBinary().op == TokenType::MINUS) {
        auto& b = node->asBinary();
        if (b.operand(1)->isUnary() && b.operand(1)->asUnary().op == TokenType::UNARY_MINUS) {
            auto rightInner = std::move(b.operands[1]->asUnary().operands[0]);
            return std::make_unique<BinaryOpNode>(TokenType::PLUS, std::move(b.operands[0]), std::move(rightInner));
        }
    }

    if (node->isBinary() && node->asBinary().op == TokenType::PLUS) {
        auto& b = node->asBinary();
        if (b.operand(0)->isUnary() && b.operand(0)->asUnary().op == TokenType::UNARY_MINUS) {
            auto leftInner = std::move(b.operands[0]->asUnary().operands[0]);
            return std::make_unique<BinaryOpNode>(TokenType::MINUS, std::move(leftInner), std::move(b.operands[1]));
        }
        if (b.operand(1)->isUnary() && b.operand(1)->asUnary().op == TokenType::UNARY_MINUS) {
            auto rightInner = std::move(b.operands[1]->asUnary().operands[0]);
            return std::make_unique<BinaryOpNode>(TokenType::MINUS, std::move(b.operands[0]), std::move(rightInner));
        }
    }

    return node;
}

static std::unique_ptr<ASTNode> flattenAssociative(std::unique_ptr<ASTNode> node) {
    if (!node) return nullptr;

    if (node->isUnary()) {
        auto& u = node->asUnary();
        if (u.operand(0)) u.operands[0] = flattenAssociative(std::move(u.operands[0]));
    } else if (node->isBinary()) {
        auto& b = node->asBinary();
        if (b.operand(0)) b.operands[0] = flattenAssociative(std::move(b.operands[0]));
        if (b.operand(1)) b.operands[1] = flattenAssociative(std::move(b.operands[1]));

        if (b.op == TokenType::PLUS || b.op == TokenType::MUL) {
            std::vector<std::unique_ptr<ASTNode>> flattened;
            std::function<void(std::unique_ptr<ASTNode>)> collect = [&](std::unique_ptr<ASTNode> n) {
                if (n->isBinary() && n->asBinary().op == b.op) {
                    auto& bn = n->asBinary();
                    collect(std::move(bn.operands[0]));
                    collect(std::move(bn.operands[1]));
                } else if (n->isMulti() && n->asMulti().op == b.op) {
                    auto& mn = n->asMulti();
                    for (auto& op : mn.operands) collect(std::move(op));
                } else {
                    flattened.push_back(std::move(n));
                }
            };
            collect(std::move(node));
            if (flattened.empty()) return std::make_unique<NumberNode>(b.op == TokenType::PLUS ? 0.0 : 1.0);
            if (flattened.size() == 1) return std::move(flattened[0]);
            return std::make_unique<MultiOpNode>(b.op, std::move(flattened));
        }
    } else if (node->isMulti()) {
        auto& m = node->asMulti();
        for (size_t i = 0; i < m.operandCount(); ++i)
            if (m.operand(i)) m.operands[i] = flattenAssociative(std::move(m.operands[i]));
    }

    return node;
}

std::unique_ptr<ASTNode> normalize(std::unique_ptr<ASTNode> node) {
    if (!node) return nullptr;

    node = simplifySigns(std::move(node));
    node = flattenAssociative(std::move(node));

    if (node->isUnary()) {
        auto& u = node->asUnary();
        if (u.operand(0)) u.operands[0] = normalize(std::move(u.operands[0]));
    } else if (node->isBinary()) {
        auto& b = node->asBinary();
        if (b.operand(0)) b.operands[0] = normalize(std::move(b.operands[0]));
        if (b.operand(1)) b.operands[1] = normalize(std::move(b.operands[1]));
    } else if (node->isMulti()) {
        auto& m = node->asMulti();
        for (size_t i = 0; i < m.operandCount(); ++i)
            if (m.operand(i)) m.operands[i] = normalize(std::move(m.operands[i]));
    }

    auto constVal = tryEvaluateConstant(node.get());
    if (constVal) return constVal;

    if (node->isMulti() && node->asMulti().op == TokenType::MUL) {
        auto& m = node->asMulti();
        std::vector<std::unique_ptr<ASTNode>> newOps;
        bool hasZero = false;
        for (auto& op : m.operands) {
            if (op->isNumber() && std::abs(op->asNumber().value) < 1e-12) { hasZero = true; break; }
            if (op->isNumber() && std::abs(op->asNumber().value - 1.0) < 1e-12) continue;
            newOps.push_back(std::move(op));
        }
        if (hasZero) return std::make_unique<NumberNode>(0.0);
        if (newOps.empty()) return std::make_unique<NumberNode>(1.0);
        if (newOps.size() == 1) return std::move(newOps[0]);
        m.operands = std::move(newOps);
    }
    if (node->isMulti() && node->asMulti().op == TokenType::PLUS) {
        auto& m = node->asMulti();
        std::vector<std::unique_ptr<ASTNode>> newOps;
        for (auto& op : m.operands) {
            if (op->isNumber() && std::abs(op->asNumber().value) < 1e-12) continue;
            newOps.push_back(std::move(op));
        }
        if (newOps.empty()) return std::make_unique<NumberNode>(0.0);
        if (newOps.size() == 1) return std::move(newOps[0]);
        m.operands = std::move(newOps);
    }

    return node;
}