#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cassert>
#include "Token.h"

enum class NodeType {
    Number, Ident, UnaryOp, BinaryOp, MultiOp
};

inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
    case TokenType::PLUS:   return "+";
    case TokenType::MINUS:  return "-";
    case TokenType::MUL:    return "*";
    case TokenType::DIV:    return "/";
    case TokenType::POW:    return "^";
    case TokenType::SIN:    return "sin";
    case TokenType::COS:    return "cos";
    case TokenType::TG:     return "tg";
    case TokenType::CTG:    return "ctg";
    case TokenType::LN:     return "ln";
    case TokenType::EXP:    return "exp";
    case TokenType::UNARY_MINUS: return "-";
    case TokenType::ARCTAN: return "arctan";
    case TokenType::ARCSIN: return "arcsin";
    case TokenType::SQRT:   return "sqrt";
    default:                return "?";
    }
}

inline std::string formatNumber(double value) {
    std::string str = std::to_string(value);
    str.erase(str.find_last_not_of('0') + 1, std::string::npos);
    str.erase(str.find_last_not_of('.') + 1, std::string::npos);
    return str;
}

class ASTNode {
public:
    NodeType type;
    ASTNode(NodeType t) : type(t) {}
    virtual ~ASTNode() = default;
    virtual std::string print() const = 0;
    virtual std::unique_ptr<ASTNode> clone() const = 0;

    bool isNumber()  const { return type == NodeType::Number; }
    bool isIdent()   const { return type == NodeType::Ident; }
    bool isUnary()   const { return type == NodeType::UnaryOp; }
    bool isBinary()  const { return type == NodeType::BinaryOp; }
    bool isMulti()   const { return type == NodeType::MultiOp; }
    bool isOpNode()  const { return isUnary() || isBinary() || isMulti(); }

    const class NumberNode&   asNumber()  const;
    const class IdentNode&    asIdent()   const;
    const class OpNode&       asOp()      const;
    const class UnaryOpNode&  asUnary()   const;
    const class BinaryOpNode& asBinary()  const;
    const class MultiOpNode&  asMulti()   const;

    class NumberNode&   asNumber();
    class IdentNode&    asIdent();
    class OpNode&       asOp();
    class UnaryOpNode&  asUnary();
    class BinaryOpNode& asBinary();
    class MultiOpNode&  asMulti();
};

class NumberNode : public ASTNode {
public:
    double value;
    NumberNode(double value = 0.0) : ASTNode(NodeType::Number), value(value) {}
    std::string print() const override { return formatNumber(value); }
    std::unique_ptr<ASTNode> clone() const override { return std::make_unique<NumberNode>(value); }
};

class IdentNode : public ASTNode {
public:
    std::string value;
    IdentNode(std::string value = "") : ASTNode(NodeType::Ident), value(std::move(value)) {}
    std::string print() const override { return value; }
    std::unique_ptr<ASTNode> clone() const override { return std::make_unique<IdentNode>(value); }
};

class OpNode : public ASTNode {
public:
    TokenType op;
    std::vector<std::unique_ptr<ASTNode>> operands;
    OpNode(NodeType t, TokenType op, std::vector<std::unique_ptr<ASTNode>> ops)
        : ASTNode(t), op(op), operands(std::move(ops)) {}
    ASTNode* operand(size_t i) { return operands.at(i).get(); }
    const ASTNode* operand(size_t i) const { return operands.at(i).get(); }
    size_t operandCount() const { return operands.size(); }
};

class BinaryOpNode : public OpNode {
public:
    BinaryOpNode(TokenType op, std::unique_ptr<ASTNode> left, std::unique_ptr<ASTNode> right)
        : OpNode(NodeType::BinaryOp, op, {}) {
        operands.reserve(2);
        operands.push_back(std::move(left));
        operands.push_back(std::move(right));
    }
    std::string print() const override {
        std::string leftStr = operands[0]->print();
        std::string rightStr = operands[1]->print();
        if (op == TokenType::POW) {
            if (operands[0]->isBinary() || operands[0]->isMulti() || operands[0]->isUnary())
                leftStr = "(" + leftStr + ")";
            if (operands[1]->isBinary() || operands[1]->isMulti() || operands[1]->isUnary())
                rightStr = "(" + rightStr + ")";
            return leftStr + "^" + rightStr;
        } else if (op == TokenType::DIV) {
            return "(" + leftStr + ")/(" + rightStr + ")";
        } else {
            return "(" + leftStr + " " + tokenTypeToString(op) + " " + rightStr + ")";
        }
    }
    std::unique_ptr<ASTNode> clone() const override {
        return std::make_unique<BinaryOpNode>(op, operands[0]->clone(), operands[1]->clone());
    }
};

class UnaryOpNode : public OpNode {
public:
    UnaryOpNode(TokenType op, std::unique_ptr<ASTNode> operand)
        : OpNode(NodeType::UnaryOp, op, {}) {
        operands.push_back(std::move(operand));
    }
    std::string print() const override {
        std::string operandStr = operands[0]->print();
        if (op == TokenType::UNARY_MINUS) {
            if (operands[0]->isNumber() || operands[0]->isIdent())
                return "-" + operandStr;
            else
                return "-(" + operandStr + ")";
        } else {
            return tokenTypeToString(op) + "(" + operandStr + ")";
        }
    }
    std::unique_ptr<ASTNode> clone() const override {
        return std::make_unique<UnaryOpNode>(op, operands[0]->clone());
    }
};

class MultiOpNode : public OpNode {
public:
    MultiOpNode(TokenType op, std::vector<std::unique_ptr<ASTNode>> ops)
        : OpNode(NodeType::MultiOp, op, std::move(ops)) {}
    std::string print() const override {
        std::string result;
        for (size_t i = 0; i < operands.size(); ++i) {
            if (i != 0) {
                if (op == TokenType::PLUS) result += " + ";
                else if (op == TokenType::MUL) result += " * ";
                else result += " " + tokenTypeToString(op) + " ";
            }
            result += operands[i]->print();
        }
        return result;
    }
    std::unique_ptr<ASTNode> clone() const override {
        std::vector<std::unique_ptr<ASTNode>> clonedOps;
        for (const auto& opnd : operands) clonedOps.push_back(opnd->clone());
        return std::make_unique<MultiOpNode>(op, std::move(clonedOps));
    }
};

inline const NumberNode& ASTNode::asNumber() const { assert(isNumber()); return static_cast<const NumberNode&>(*this); }
inline const IdentNode& ASTNode::asIdent() const { assert(isIdent()); return static_cast<const IdentNode&>(*this); }
inline const OpNode& ASTNode::asOp() const { assert(isOpNode()); return static_cast<const OpNode&>(*this); }
inline const UnaryOpNode& ASTNode::asUnary() const { assert(isUnary()); return static_cast<const UnaryOpNode&>(*this); }
inline const BinaryOpNode& ASTNode::asBinary() const { assert(isBinary()); return static_cast<const BinaryOpNode&>(*this); }
inline const MultiOpNode& ASTNode::asMulti() const { assert(isMulti()); return static_cast<const MultiOpNode&>(*this); }
inline NumberNode& ASTNode::asNumber() { assert(isNumber()); return static_cast<NumberNode&>(*this); }
inline IdentNode& ASTNode::asIdent() { assert(isIdent()); return static_cast<IdentNode&>(*this); }
inline OpNode& ASTNode::asOp() { assert(isOpNode()); return static_cast<OpNode&>(*this); }
inline UnaryOpNode& ASTNode::asUnary() { assert(isUnary()); return static_cast<UnaryOpNode&>(*this); }
inline BinaryOpNode& ASTNode::asBinary() { assert(isBinary()); return static_cast<BinaryOpNode&>(*this); }
inline MultiOpNode& ASTNode::asMulti() { assert(isMulti()); return static_cast<MultiOpNode&>(*this); }