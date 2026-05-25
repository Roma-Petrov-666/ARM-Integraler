#include "IntegralClassifier.h"
#include "Normalizer.h"
#include <cmath>
#include <set>
#include <sstream>
#include <functional>

static int recursionDepth = 0;
static const int MAX_RECURSION_DEPTH = 100;

static std::string normalizedString(const ASTNode* node) {
    if (!node) return "";
    auto copy = node->clone();
    auto norm = normalize(std::move(copy));
    return norm ? norm->print() : "";
}

static bool dependsOnVar(const ASTNode* node, const std::string& var) {
    if (!node) return false;
    if (node->isIdent()) return node->asIdent().value == var;
    if (node->isNumber()) return false;
    if (node->isUnary())
        return dependsOnVar(node->asUnary().operand(0), var);
    if (node->isBinary())
        return dependsOnVar(node->asBinary().operand(0), var) ||
               dependsOnVar(node->asBinary().operand(1), var);
    if (node->isMulti()) {
        for (size_t i = 0; i < node->asMulti().operandCount(); ++i)
            if (dependsOnVar(node->asMulti().operand(i), var)) return true;
        return false;
    }
    return false;
}

static bool isConstant(const ASTNode* node, const std::string& var) {
    return !dependsOnVar(node, var);
}

static bool getLinearCoeff(const ASTNode* node, const std::string& var, double& a, double& b) {
    if (!node) return false;
    if (node->isIdent() && node->asIdent().value == var) {
        a = 1.0; b = 0.0;
        return true;
    }
    if (node->isNumber()) {
        a = 0.0; b = node->asNumber().value;
        return true;
    }
    if (node->isBinary() && node->asBinary().op == TokenType::PLUS) {
        const auto& bin = node->asBinary();
        double a1, b1, a2, b2;
        if (getLinearCoeff(bin.operand(0), var, a1, b1) &&
            getLinearCoeff(bin.operand(1), var, a2, b2)) {
            a = a1 + a2; b = b1 + b2;
            return true;
        }
        return false;
    }
    if (node->isMulti() && node->asMulti().op == TokenType::PLUS) {
        const auto& multi = node->asMulti();
        a = 0.0; b = 0.0;
        for (size_t i = 0; i < multi.operandCount(); ++i) {
            double ai, bi;
            if (!getLinearCoeff(multi.operand(i), var, ai, bi)) return false;
            a += ai; b += bi;
        }
        return true;
    }
    if (node->isBinary() && node->asBinary().op == TokenType::MINUS) {
        const auto& bin = node->asBinary();
        double a1, b1, a2, b2;
        if (getLinearCoeff(bin.operand(0), var, a1, b1) &&
            getLinearCoeff(bin.operand(1), var, a2, b2)) {
            a = a1 - a2; b = b1 - b2;
            return true;
        }
        return false;
    }
    if (node->isBinary() && node->asBinary().op == TokenType::MUL) {
        const auto& bin = node->asBinary();
        if (bin.operand(0)->isNumber() && bin.operand(1)->isIdent() && bin.operand(1)->asIdent().value == var) {
            a = bin.operand(0)->asNumber().value; b = 0.0;
            return true;
        }
        if (bin.operand(1)->isNumber() && bin.operand(0)->isIdent() && bin.operand(0)->asIdent().value == var) {
            a = bin.operand(1)->asNumber().value; b = 0.0;
            return true;
        }
        return false;
    }
    if (node->isMulti() && node->asMulti().op == TokenType::MUL) {
        const auto& multi = node->asMulti();
        double factor = 1.0;
        bool foundVar = false;
        for (size_t i = 0; i < multi.operandCount(); ++i) {
            const ASTNode* op = multi.operand(i);
            if (op->isNumber()) {
                factor *= op->asNumber().value;
            } else if (op->isIdent() && op->asIdent().value == var) {
                if (foundVar) return false;
                foundVar = true;
            } else {
                return false;
            }
        }
        if (foundVar) {
            a = factor; b = 0.0;
            return true;
        }
        return false;
    }
    if (node->isUnary() && node->asUnary().op == TokenType::UNARY_MINUS) {
        double a1, b1;
        if (getLinearCoeff(node->asUnary().operand(0), var, a1, b1)) {
            a = -a1; b = -b1;
            return true;
        }
        return false;
    }
    return false;
}

static std::unique_ptr<ASTNode> derivative(const ASTNode* expr, const std::string& var) {
    if (!expr) return nullptr;
    if (expr->isNumber()) return std::make_unique<NumberNode>(0.0);
    if (expr->isIdent()) {
        if (expr->asIdent().value == var) return std::make_unique<NumberNode>(1.0);
        else return std::make_unique<NumberNode>(0.0);
    }
    if (expr->isUnary()) {
        const auto& u = expr->asUnary();
        auto argDer = derivative(u.operand(0), var);
        if (!argDer) return nullptr;
        if (u.op == TokenType::UNARY_MINUS) {
            return std::make_unique<UnaryOpNode>(TokenType::UNARY_MINUS, std::move(argDer));
        } else if (u.op == TokenType::SIN) {
            auto cosArg = std::make_unique<UnaryOpNode>(TokenType::COS, u.operand(0)->clone());
            return std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(cosArg), std::move(argDer));
        } else if (u.op == TokenType::COS) {
            auto sinArg = std::make_unique<UnaryOpNode>(TokenType::SIN, u.operand(0)->clone());
            auto negSin = std::make_unique<UnaryOpNode>(TokenType::UNARY_MINUS, std::move(sinArg));
            return std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(negSin), std::move(argDer));
        } else if (u.op == TokenType::TG) {
            auto tgArg = std::make_unique<UnaryOpNode>(TokenType::TG, u.operand(0)->clone());
            auto tgSq = std::make_unique<BinaryOpNode>(TokenType::POW, std::move(tgArg), std::make_unique<NumberNode>(2.0));
            auto one = std::make_unique<NumberNode>(1.0);
            auto factor = std::make_unique<BinaryOpNode>(TokenType::PLUS, std::move(one), std::move(tgSq));
            return std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(factor), std::move(argDer));
        } else if (u.op == TokenType::CTG) {
            auto sinArg = std::make_unique<UnaryOpNode>(TokenType::SIN, u.operand(0)->clone());
            auto sinSq = std::make_unique<BinaryOpNode>(TokenType::POW, std::move(sinArg), std::make_unique<NumberNode>(2.0));
            auto one = std::make_unique<NumberNode>(1.0);
            auto factor = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(one), std::move(sinSq));
            auto negFactor = std::make_unique<UnaryOpNode>(TokenType::UNARY_MINUS, std::move(factor));
            return std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(negFactor), std::move(argDer));
        } else if (u.op == TokenType::LN) {
            auto uClone = u.operand(0)->clone();
            return std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(argDer), std::move(uClone));
        } else if (u.op == TokenType::EXP) {
            auto expClone = expr->clone();
            return std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(expClone), std::move(argDer));
        } else if (u.op == TokenType::ARCSIN) {
            auto argClone = u.operand(0)->clone();
            auto one = std::make_unique<NumberNode>(1.0);
            auto argSq = std::make_unique<BinaryOpNode>(TokenType::POW, argClone->clone(), std::make_unique<NumberNode>(2.0));
            auto sqrtArg = std::make_unique<BinaryOpNode>(TokenType::MINUS, std::move(one), std::move(argSq));
            auto sqrtNode = std::make_unique<UnaryOpNode>(TokenType::SQRT, std::move(sqrtArg));
            auto denom = sqrtNode->clone();
            auto der = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(argDer), std::move(denom));
            return der;
        } else if (u.op == TokenType::ARCTAN) {
            auto argClone = u.operand(0)->clone();
            auto one = std::make_unique<NumberNode>(1.0);
            auto argSq = std::make_unique<BinaryOpNode>(TokenType::POW, argClone->clone(), std::make_unique<NumberNode>(2.0));
            auto denom = std::make_unique<BinaryOpNode>(TokenType::PLUS, std::move(one), std::move(argSq));
            auto der = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(argDer), std::move(denom));
            return der;
        }
        return nullptr;
    }
    if (expr->isBinary()) {
        const auto& b = expr->asBinary();
        auto left = b.operand(0);
        auto right = b.operand(1);
        auto leftDer = derivative(left, var);
        auto rightDer = derivative(right, var);
        if (!leftDer || !rightDer) return nullptr;
        if (b.op == TokenType::PLUS) {
            return std::make_unique<BinaryOpNode>(TokenType::PLUS, std::move(leftDer), std::move(rightDer));
        } else if (b.op == TokenType::MINUS) {
            return std::make_unique<BinaryOpNode>(TokenType::MINUS, std::move(leftDer), std::move(rightDer));
        } else if (b.op == TokenType::MUL) {
            auto u = left->clone();
            auto v = right->clone();
            auto term1 = std::make_unique<BinaryOpNode>(TokenType::MUL, leftDer->clone(), std::move(v));
            auto term2 = std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(u), rightDer->clone());
            return std::make_unique<BinaryOpNode>(TokenType::PLUS, std::move(term1), std::move(term2));
        } else if (b.op == TokenType::DIV) {
            auto u = left->clone();
            auto v = right->clone();
            auto uDer = std::move(leftDer);
            auto vDer = std::move(rightDer);
            auto term1 = std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(uDer), v->clone());
            auto term2 = std::make_unique<BinaryOpNode>(TokenType::MUL, u->clone(), std::move(vDer));
            auto num = std::make_unique<BinaryOpNode>(TokenType::MINUS, std::move(term1), std::move(term2));
            auto denom = std::make_unique<BinaryOpNode>(TokenType::POW, std::move(v), std::make_unique<NumberNode>(2.0));
            return std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(num), std::move(denom));
        } else if (b.op == TokenType::POW) {
            auto u = left->clone();
            auto v = right->clone();
            auto uDer = derivative(left, var);
            auto vDer = derivative(right, var);
            if (!uDer || !vDer) return nullptr;
            auto powUV = std::make_unique<BinaryOpNode>(TokenType::POW, u->clone(), v->clone());
            auto lnU = std::make_unique<UnaryOpNode>(TokenType::LN, u->clone());
            auto term1 = std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(vDer), std::move(lnU));
            auto term2 = std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(v),
                                                        std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(uDer), u->clone()));
            auto sum = std::make_unique<BinaryOpNode>(TokenType::PLUS, std::move(term1), std::move(term2));
            return std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(powUV), std::move(sum));
        }
        return nullptr;
    }
    if (expr->isMulti()) {
        const auto& m = expr->asMulti();
        if (m.op == TokenType::PLUS) {
            std::vector<std::unique_ptr<ASTNode>> terms;
            for (size_t i = 0; i < m.operandCount(); ++i) {
                auto termDer = derivative(m.operand(i), var);
                if (!termDer) return nullptr;
                terms.push_back(std::move(termDer));
            }
            return std::make_unique<MultiOpNode>(TokenType::PLUS, std::move(terms));
        } else if (m.op == TokenType::MUL) {
            std::vector<std::unique_ptr<ASTNode>> terms;
            for (size_t i = 0; i < m.operandCount(); ++i) {
                auto der = derivative(m.operand(i), var);
                if (!der) return nullptr;
                std::vector<std::unique_ptr<ASTNode>> factors;
                for (size_t j = 0; j < m.operandCount(); ++j) {
                    if (i == j) factors.push_back(std::move(der));
                    else factors.push_back(m.operand(j)->clone());
                }
                terms.push_back(std::make_unique<MultiOpNode>(TokenType::MUL, std::move(factors)));
            }
            if (terms.empty()) return std::make_unique<NumberNode>(0.0);
            if (terms.size() == 1) return std::move(terms[0]);
            return std::make_unique<MultiOpNode>(TokenType::PLUS, std::move(terms));
        }
        return nullptr;
    }
    return nullptr;
}

static bool equalTrees(const ASTNode* a, const ASTNode* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    if (a->isNumber()) return std::abs(a->asNumber().value - b->asNumber().value) < 1e-12;
    if (a->isIdent()) return a->asIdent().value == b->asIdent().value;
    if (a->isUnary()) {
        if (a->asUnary().op != b->asUnary().op) return false;
        return equalTrees(a->asUnary().operand(0), b->asUnary().operand(0));
    }
    if (a->isBinary()) {
        if (a->asBinary().op != b->asBinary().op) return false;
        return equalTrees(a->asBinary().operand(0), b->asBinary().operand(0)) &&
               equalTrees(a->asBinary().operand(1), b->asBinary().operand(1));
    }
    if (a->isMulti()) {
        if (a->asMulti().op != b->asMulti().op) return false;
        if (a->asMulti().operandCount() != b->asMulti().operandCount()) return false;
        for (size_t i = 0; i < a->asMulti().operandCount(); ++i)
            if (!equalTrees(a->asMulti().operand(i), b->asMulti().operand(i))) return false;
        return true;
    }
    return false;
}

static std::unique_ptr<ASTNode> replaceSubtree(const ASTNode* node,
                                               const ASTNode* pattern,
                                               const ASTNode* replacement) {
    if (!node) return nullptr;
    if (equalTrees(node, pattern)) return replacement->clone();
    if (node->isNumber() || node->isIdent()) return node->clone();
    if (node->isUnary()) {
        auto newArg = replaceSubtree(node->asUnary().operand(0), pattern, replacement);
        if (!newArg) return nullptr;
        return std::make_unique<UnaryOpNode>(node->asUnary().op, std::move(newArg));
    }
    if (node->isBinary()) {
        auto left = replaceSubtree(node->asBinary().operand(0), pattern, replacement);
        auto right = replaceSubtree(node->asBinary().operand(1), pattern, replacement);
        if (!left || !right) return nullptr;
        return std::make_unique<BinaryOpNode>(node->asBinary().op, std::move(left), std::move(right));
    }
    if (node->isMulti()) {
        std::vector<std::unique_ptr<ASTNode>> newOps;
        for (size_t i = 0; i < node->asMulti().operandCount(); ++i) {
            auto sub = replaceSubtree(node->asMulti().operand(i), pattern, replacement);
            if (!sub) return nullptr;
            newOps.push_back(std::move(sub));
        }
        return std::make_unique<MultiOpNode>(node->asMulti().op, std::move(newOps));
    }
    return node->clone();
}

static std::unique_ptr<ASTNode> simplifyDivision(const ASTNode* node, const std::string& var) {
    if (!node) return nullptr;
    if (node->isBinary() && node->asBinary().op == TokenType::DIV) {
        const auto& bin = node->asBinary();
        auto left = bin.operand(0);
        auto right = bin.operand(1);
        if (dependsOnVar(right, var)) {
            auto negPower = std::make_unique<BinaryOpNode>(TokenType::POW, right->clone(), std::make_unique<NumberNode>(-1.0));
            auto result = std::make_unique<BinaryOpNode>(TokenType::MUL, left->clone(), std::move(negPower));
            return normalize(std::move(result));
        } else {
            return node->clone();
        }
    }
    if (node->isUnary()) {
        auto arg = simplifyDivision(node->asUnary().operand(0), var);
        if (!arg) return nullptr;
        return std::make_unique<UnaryOpNode>(node->asUnary().op, std::move(arg));
    }
    if (node->isBinary()) {
        auto left = simplifyDivision(node->asBinary().operand(0), var);
        auto right = simplifyDivision(node->asBinary().operand(1), var);
        if (!left || !right) return nullptr;
        return std::make_unique<BinaryOpNode>(node->asBinary().op, std::move(left), std::move(right));
    }
    if (node->isMulti()) {
        std::vector<std::unique_ptr<ASTNode>> newOps;
        for (size_t i = 0; i < node->asMulti().operandCount(); ++i) {
            auto op = simplifyDivision(node->asMulti().operand(i), var);
            if (!op) return nullptr;
            newOps.push_back(std::move(op));
        }
        return std::make_unique<MultiOpNode>(node->asMulti().op, std::move(newOps));
    }
    return node->clone();
}

static std::pair<std::string, std::unique_ptr<ASTNode>> tryTableIntegralWithDesc(const ASTNode* expr, const std::string& var) {
    if (expr->isBinary() && expr->asBinary().op == TokenType::POW) {
        const auto& bin = expr->asBinary();
        const ASTNode* base = bin.operand(0);
        const ASTNode* exponent = bin.operand(1);
        if (exponent->isNumber()) {
            double n = exponent->asNumber().value;
            double a, b;
            if (getLinearCoeff(base, var, a, b) && std::abs(a) > 1e-12) {
                if (std::abs(n + 1.0) > 1e-12) {
                    auto linear = base->clone();
                    auto powNode = std::make_unique<BinaryOpNode>(TokenType::POW, std::move(linear), std::make_unique<NumberNode>(n + 1.0));
                    auto denom = std::make_unique<NumberNode>(a * (n + 1.0));
                    std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(powNode), std::move(denom));
                    std::ostringstream desc;
                    desc << "∫ (" << base->print() << ")^" << n << " dx = (" << base->print() << ")^{" << n+1 << "} / (" << a << "·(" << n+1 << "))";
                    return {desc.str(), std::move(res)};
                } else {
                    auto linear = base->clone();
                    auto lnNode = std::make_unique<UnaryOpNode>(TokenType::LN, std::move(linear));
                    std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(lnNode), std::make_unique<NumberNode>(a));
                    std::ostringstream desc;
                    desc << "∫ 1/(" << base->print() << ") dx = ln|" << base->print() << "| / " << a;
                    return {desc.str(), std::move(res)};
                }
            }
        }
    }

    if (isConstant(expr, var)) {
        std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::MUL, expr->clone(), std::make_unique<IdentNode>(var));
        std::string desc = "∫ " + expr->print() + " dx = " + expr->print() + "·x";
        return {desc, std::move(res)};
    }

    if (expr->isBinary() && expr->asBinary().op == TokenType::POW) {
        const auto& bin = expr->asBinary();
        if (bin.operand(0)->isNumber() && bin.operand(1)->isIdent() && bin.operand(1)->asIdent().value == var) {
            double a = bin.operand(0)->asNumber().value;
            if (a > 0 && std::abs(a-1.0) > 1e-12) {
                auto powNode = expr->clone();
                auto lnA = std::make_unique<UnaryOpNode>(TokenType::LN, bin.operand(0)->clone());
                std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(powNode), std::move(lnA));
                std::ostringstream desc;
                desc << "∫ " << a << "^x dx = " << a << "^x / ln(" << a << ")";
                return {desc.str(), std::move(res)};
            }
        }
    }

    if (expr->isUnary() && expr->asUnary().op == TokenType::EXP) {
        const auto& u = expr->asUnary();
        double a, b;
        if (getLinearCoeff(u.operand(0), var, a, b) && std::abs(a) > 1e-12) {
            std::unique_ptr<ASTNode> res = expr->clone();
            if (std::abs(a-1.0) > 1e-12) {
                res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(res), std::make_unique<NumberNode>(a));
            }
            std::ostringstream desc;
            desc << "∫ e^{" << u.operand(0)->print() << "} dx = e^{" << u.operand(0)->print() << "} / " << a;
            return {desc.str(), std::move(res)};
        }
    }

    if (expr->isUnary() && expr->asUnary().op == TokenType::LN) {
        const auto& u = expr->asUnary();
        double a, b;
        if (getLinearCoeff(u.operand(0), var, a, b) && std::abs(a) > 1e-12) {
            auto linear = u.operand(0)->clone();
            auto term1 = std::make_unique<BinaryOpNode>(TokenType::MUL, linear->clone(), expr->clone());
            auto term2 = linear->clone();
            auto sub = std::make_unique<BinaryOpNode>(TokenType::MINUS, std::move(term1), std::move(term2));
            std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(sub), std::make_unique<NumberNode>(a));
            std::ostringstream desc;
            desc << "∫ ln(" << linear->print() << ") dx = ((" << linear->print() << ")·ln(" << linear->print() << ") - (" << linear->print() << "))/" << a;
            return {desc.str(), std::move(res)};
        }
    }

    if (expr->isUnary() && expr->asUnary().op == TokenType::SIN) {
        const auto& u = expr->asUnary();
        double a, b;
        if (getLinearCoeff(u.operand(0), var, a, b) && std::abs(a) > 1e-12) {
            auto arg = u.operand(0)->clone();
            auto cosArg = std::make_unique<UnaryOpNode>(TokenType::COS, std::move(arg));
            std::unique_ptr<ASTNode> res = std::make_unique<UnaryOpNode>(TokenType::UNARY_MINUS, std::move(cosArg));
            if (std::abs(a-1.0) > 1e-12) {
                res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(res), std::make_unique<NumberNode>(a));
            }
            std::ostringstream desc;
            desc << "∫ sin(" << u.operand(0)->print() << ") dx = -cos(" << u.operand(0)->print() << ")/" << a;
            return {desc.str(), std::move(res)};
        }
    }

    if (expr->isUnary() && expr->asUnary().op == TokenType::COS) {
        const auto& u = expr->asUnary();
        double a, b;
        if (getLinearCoeff(u.operand(0), var, a, b) && std::abs(a) > 1e-12) {
            auto arg = u.operand(0)->clone();
            std::unique_ptr<ASTNode> res = std::make_unique<UnaryOpNode>(TokenType::SIN, std::move(arg));
            if (std::abs(a-1.0) > 1e-12) {
                res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(res), std::make_unique<NumberNode>(a));
            }
            std::ostringstream desc;
            desc << "∫ cos(" << u.operand(0)->print() << ") dx = sin(" << u.operand(0)->print() << ")/" << a;
            return {desc.str(), std::move(res)};
        }
    }

    if (expr->isUnary() && expr->asUnary().op == TokenType::TG) {
        const auto& u = expr->asUnary();
        double a, b;
        if (getLinearCoeff(u.operand(0), var, a, b) && std::abs(a) > 1e-12) {
            auto arg = u.operand(0)->clone();
            auto cosArg = std::make_unique<UnaryOpNode>(TokenType::COS, std::move(arg));
            auto lnCos = std::make_unique<UnaryOpNode>(TokenType::LN, std::move(cosArg));
            std::unique_ptr<ASTNode> res = std::make_unique<UnaryOpNode>(TokenType::UNARY_MINUS, std::move(lnCos));
            if (std::abs(a-1.0) > 1e-12) {
                res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(res), std::make_unique<NumberNode>(a));
            }
            std::ostringstream desc;
            desc << "∫ tg(" << u.operand(0)->print() << ") dx = -ln|cos(" << u.operand(0)->print() << ")|/" << a;
            return {desc.str(), std::move(res)};
        }
    }

    if (expr->isUnary() && expr->asUnary().op == TokenType::CTG) {
        const auto& u = expr->asUnary();
        double a, b;
        if (getLinearCoeff(u.operand(0), var, a, b) && std::abs(a) > 1e-12) {
            auto arg = u.operand(0)->clone();
            auto sinArg = std::make_unique<UnaryOpNode>(TokenType::SIN, std::move(arg));
            std::unique_ptr<ASTNode> res = std::make_unique<UnaryOpNode>(TokenType::LN, std::move(sinArg));
            if (std::abs(a-1.0) > 1e-12) {
                res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(res), std::make_unique<NumberNode>(a));
            }
            std::ostringstream desc;
            desc << "∫ ctg(" << u.operand(0)->print() << ") dx = ln|sin(" << u.operand(0)->print() << ")|/" << a;
            return {desc.str(), std::move(res)};
        }
    }

    if (expr->isBinary() && expr->asBinary().op == TokenType::DIV) {
        const auto& bin = expr->asBinary();
        if (bin.operand(0)->isNumber() && std::abs(bin.operand(0)->asNumber().value - 1.0) < 1e-12) {
            auto denom = bin.operand(1);
            if (denom->isBinary() && denom->asBinary().op == TokenType::PLUS) {
                const auto& sum = denom->asBinary();
                if (sum.operand(1)->isNumber() && std::abs(sum.operand(1)->asNumber().value - 1.0) < 1e-12) {
                    auto squareTerm = sum.operand(0);
                    if (squareTerm->isBinary() && squareTerm->asBinary().op == TokenType::POW &&
                        squareTerm->asBinary().operand(1)->isNumber() &&
                        std::abs(squareTerm->asBinary().operand(1)->asNumber().value - 2.0) < 1e-12) {
                        const ASTNode* inner = squareTerm->asBinary().operand(0);
                        double a_lin, b_lin;
                        if (getLinearCoeff(inner, var, a_lin, b_lin) && std::abs(a_lin) > 1e-12) {
                            auto linear = inner->clone();
                            auto arctanArg = std::make_unique<BinaryOpNode>(TokenType::DIV, linear->clone(), std::make_unique<NumberNode>(1.0));
                            std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::make_unique<UnaryOpNode>(TokenType::ARCTAN, std::move(arctanArg)), std::make_unique<NumberNode>(a_lin));
                            std::ostringstream desc;
                            desc << "∫ 1/((" << linear->print() << ")²+1) dx = arctan(" << linear->print() << ")/" << a_lin;
                            return {desc.str(), std::move(res)};
                        }
                    }
                }
            }
        }
    }

    if (expr->isBinary() && expr->asBinary().op == TokenType::DIV) {
        const auto& bin = expr->asBinary();
        if (bin.operand(0)->isNumber() && std::abs(bin.operand(0)->asNumber().value - 1.0) < 1e-12 &&
            bin.operand(1)->isUnary() && bin.operand(1)->asUnary().op == TokenType::SQRT) {
            auto sqrtArg = bin.operand(1)->asUnary().operand(0);
            if (sqrtArg->isBinary() && sqrtArg->asBinary().op == TokenType::MINUS) {
                const auto& sub = sqrtArg->asBinary();
                if (sub.operand(0)->isNumber() && std::abs(sub.operand(0)->asNumber().value - 1.0) < 1e-12) {
                    auto squareTerm = sub.operand(1);
                    if (squareTerm->isBinary() && squareTerm->asBinary().op == TokenType::POW &&
                        squareTerm->asBinary().operand(1)->isNumber() &&
                        std::abs(squareTerm->asBinary().operand(1)->asNumber().value - 2.0) < 1e-12) {
                        const ASTNode* inner = squareTerm->asBinary().operand(0);
                        double a_lin, b_lin;
                        if (getLinearCoeff(inner, var, a_lin, b_lin) && std::abs(a_lin) > 1e-12) {
                            auto linear = inner->clone();
                            auto arcsinArg = std::make_unique<BinaryOpNode>(TokenType::DIV, linear->clone(), std::make_unique<NumberNode>(1.0));
                            std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::make_unique<UnaryOpNode>(TokenType::ARCSIN, std::move(arcsinArg)), std::make_unique<NumberNode>(a_lin));
                            std::ostringstream desc;
                            desc << "∫ 1/√(1-(" << linear->print() << ")²) dx = arcsin(" << linear->print() << ")/" << a_lin;
                            return {desc.str(), std::move(res)};
                        }
                    }
                }
            }
        }
    }

    if (expr->isIdent() && expr->asIdent().value == var) {
        auto powNode = std::make_unique<BinaryOpNode>(TokenType::POW, std::make_unique<IdentNode>(var), std::make_unique<NumberNode>(2.0));
        std::unique_ptr<ASTNode> res = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(powNode), std::make_unique<NumberNode>(2.0));
        return {"∫ x dx = x²/2", std::move(res)};
    }

    return {"", nullptr};
}

static std::pair<std::string, std::unique_ptr<ASTNode>> integrateSubstitutionWithDesc(const ASTNode* expr,
                                                                                      const std::string& var,
                                                                                      const ASTNode* u_pattern,
                                                                                      const ASTNode* du_pattern,
                                                                                      const ASTNode* f_of_t,
                                                                                      const std::string& substDesc) {
    std::unique_ptr<ASTNode> expr_rest = nullptr;
    double constFactor = 1.0;

    if (equalTrees(expr, du_pattern)) {
        expr_rest = std::make_unique<NumberNode>(1.0);
    } else if (expr->isMulti() && expr->asMulti().op == TokenType::MUL) {
        const auto& mul = expr->asMulti();
        bool found = false;
        std::vector<std::unique_ptr<ASTNode>> rest;
        for (size_t i = 0; i < mul.operandCount(); ++i) {
            auto* op = mul.operand(i);
            if (equalTrees(op, du_pattern)) {
                found = true;
            } else if (op->isNumber()) {
                constFactor *= op->asNumber().value;
            } else {
                rest.push_back(op->clone());
            }
        }
        if (found) {
            if (rest.empty()) expr_rest = std::make_unique<NumberNode>(1.0);
            else if (rest.size() == 1) expr_rest = std::move(rest[0]);
            else expr_rest = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(rest));
        }
    } else if (expr->isBinary() && expr->asBinary().op == TokenType::MUL) {
        const auto& bin = expr->asBinary();
        if (equalTrees(bin.operand(0), du_pattern)) {
            expr_rest = bin.operand(1)->clone();
        } else if (equalTrees(bin.operand(1), du_pattern)) {
            expr_rest = bin.operand(0)->clone();
        }
    }

    if (!expr_rest) return {"", nullptr};

    std::unique_ptr<ASTNode> f_of_t_ptr;
    if (f_of_t) {
        f_of_t_ptr = f_of_t->clone();
    } else {
        auto t = std::make_unique<IdentNode>("t");
        f_of_t_ptr = replaceSubtree(expr_rest.get(), u_pattern, t.get());
    }
    if (!f_of_t_ptr) return {"", nullptr};
    if (dependsOnVar(f_of_t_ptr.get(), var)) return {"", nullptr};

    auto t = std::make_unique<IdentNode>("t");
    auto int_f_vec = directIntegrate(f_of_t_ptr.get(), "t");
    if (int_f_vec.empty()) return {"", nullptr};
    auto F_of_t = normalize(std::move(int_f_vec[0]));

    auto result = replaceSubtree(F_of_t.get(), t.get(), u_pattern);
    if (!result) return {"", nullptr};

    if (std::abs(constFactor - 1.0) > 1e-12) {
        result = std::make_unique<BinaryOpNode>(TokenType::MUL, std::make_unique<NumberNode>(constFactor), std::move(result));
        result = normalize(std::move(result));
    }
    return {substDesc, std::move(result)};
}

static std::pair<std::string, std::unique_ptr<ASTNode>> tryExponentialSubstitutionWithDesc(const ASTNode* expr, const std::string& var) {
    auto findExp = [&](const ASTNode* node, double& a, double& b) -> bool {
        if (node->isUnary() && node->asUnary().op == TokenType::EXP) {
            const auto& u = node->asUnary();
            return getLinearCoeff(u.operand(0), var, a, b);
        }
        return false;
    };
    double a, b;
    if (findExp(expr, a, b)) {
        auto innerPattern = expr->clone();
        auto duPattern = derivative(innerPattern.get(), var);
        if (duPattern) {
            std::ostringstream desc;
            desc << "Замена t = e^{" << expr->asUnary().operand(0)->print() << "}, dt = " << duPattern->print() << " dx → ∫ dt = t";
            return integrateSubstitutionWithDesc(expr, var, innerPattern.get(), duPattern.get(), nullptr, desc.str());
        }
    }
    if (expr->isMulti() || expr->isBinary()) {
        std::vector<const ASTNode*> factors;
        if (expr->isMulti() && expr->asMulti().op == TokenType::MUL) {
            for (size_t i = 0; i < expr->asMulti().operandCount(); ++i)
                factors.push_back(expr->asMulti().operand(i));
        } else if (expr->isBinary() && expr->asBinary().op == TokenType::MUL) {
            factors.push_back(expr->asBinary().operand(0));
            factors.push_back(expr->asBinary().operand(1));
        }
        for (size_t i = 0; i < factors.size(); ++i) {
            if (findExp(factors[i], a, b)) {
                auto innerPattern = factors[i]->clone();
                auto duPattern = derivative(innerPattern.get(), var);
                if (duPattern) {
                    std::vector<std::unique_ptr<ASTNode>> rest;
                    for (size_t j = 0; j < factors.size(); ++j)
                        if (j != i) rest.push_back(factors[j]->clone());
                    std::unique_ptr<ASTNode> restNode;
                    if (rest.empty()) restNode = std::make_unique<NumberNode>(1.0);
                    else if (rest.size() == 1) restNode = std::move(rest[0]);
                    else restNode = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(rest));
                    auto t = std::make_unique<IdentNode>("t");
                    auto f_of_t = replaceSubtree(restNode.get(), innerPattern.get(), t.get());
                    std::ostringstream desc;
                    desc << "Замена t = e^{" << factors[i]->asUnary().operand(0)->print() << "}, dt = " << duPattern->print() << " dx";
                    return integrateSubstitutionWithDesc(expr, var, innerPattern.get(), duPattern.get(), f_of_t.get(), desc.str());
                }
            }
        }
    }
    return {"", nullptr};
}

static std::pair<std::string, std::unique_ptr<ASTNode>> tryLogarithmicSubstitutionWithDesc(const ASTNode* expr, const std::string& var) {
    auto findLn = [&](const ASTNode* node, double& a, double& b) -> bool {
        if (node->isUnary() && node->asUnary().op == TokenType::LN) {
            const auto& u = node->asUnary();
            return getLinearCoeff(u.operand(0), var, a, b);
        }
        return false;
    };
    double a, b;
    if (findLn(expr, a, b)) {
        auto innerPattern = expr->clone();
        auto duPattern = derivative(innerPattern.get(), var);
        if (duPattern) {
            std::ostringstream desc;
            desc << "Замена t = ln(" << expr->asUnary().operand(0)->print() << "), dt = " << duPattern->print() << " dx → ∫ dt = t";
            return integrateSubstitutionWithDesc(expr, var, innerPattern.get(), duPattern.get(), nullptr, desc.str());
        }
    }
    if (expr->isMulti() || expr->isBinary()) {
        std::vector<const ASTNode*> factors;
        if (expr->isMulti() && expr->asMulti().op == TokenType::MUL) {
            for (size_t i = 0; i < expr->asMulti().operandCount(); ++i)
                factors.push_back(expr->asMulti().operand(i));
        } else if (expr->isBinary() && expr->asBinary().op == TokenType::MUL) {
            factors.push_back(expr->asBinary().operand(0));
            factors.push_back(expr->asBinary().operand(1));
        }
        for (size_t i = 0; i < factors.size(); ++i) {
            if (findLn(factors[i], a, b)) {
                auto innerPattern = factors[i]->clone();
                auto duPattern = derivative(innerPattern.get(), var);
                if (duPattern) {
                    std::vector<std::unique_ptr<ASTNode>> rest;
                    for (size_t j = 0; j < factors.size(); ++j)
                        if (j != i) rest.push_back(factors[j]->clone());
                    std::unique_ptr<ASTNode> restNode;
                    if (rest.empty()) restNode = std::make_unique<NumberNode>(1.0);
                    else if (rest.size() == 1) restNode = std::move(rest[0]);
                    else restNode = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(rest));
                    auto t = std::make_unique<IdentNode>("t");
                    auto f_of_t = replaceSubtree(restNode.get(), innerPattern.get(), t.get());
                    std::ostringstream desc;
                    desc << "Замена t = ln(" << factors[i]->asUnary().operand(0)->print() << "), dt = " << duPattern->print() << " dx";
                    return integrateSubstitutionWithDesc(expr, var, innerPattern.get(), duPattern.get(), f_of_t.get(), desc.str());
                }
            }
        }
    }
    return {"", nullptr};
}

static std::pair<std::string, std::unique_ptr<ASTNode>> tryTrigonometricSubstitutionWithDesc(const ASTNode* expr, const std::string& var) {
    auto tryTrig = [&](TokenType trigOp, const std::string& trigName) -> std::pair<std::string, std::unique_ptr<ASTNode>> {
        double a, b;
        if (expr->isUnary() && expr->asUnary().op == trigOp) {
            const auto& u = expr->asUnary();
            if (getLinearCoeff(u.operand(0), var, a, b)) {
                auto innerPattern = expr->clone();
                auto duPattern = derivative(innerPattern.get(), var);
                if (duPattern) {
                    std::ostringstream desc;
                    desc << "Замена t = " << trigName << "(" << u.operand(0)->print() << "), dt = " << duPattern->print() << " dx → ∫ dt = t";
                    return integrateSubstitutionWithDesc(expr, var, innerPattern.get(), duPattern.get(), nullptr, desc.str());
                }
            }
        }
        if (expr->isMulti() || expr->isBinary()) {
            std::vector<const ASTNode*> factors;
            if (expr->isMulti() && expr->asMulti().op == TokenType::MUL) {
                for (size_t i = 0; i < expr->asMulti().operandCount(); ++i)
                    factors.push_back(expr->asMulti().operand(i));
            } else if (expr->isBinary() && expr->asBinary().op == TokenType::MUL) {
                factors.push_back(expr->asBinary().operand(0));
                factors.push_back(expr->asBinary().operand(1));
            }
            for (size_t i = 0; i < factors.size(); ++i) {
                if (factors[i]->isUnary() && factors[i]->asUnary().op == trigOp) {
                    const auto& u = factors[i]->asUnary();
                    if (getLinearCoeff(u.operand(0), var, a, b)) {
                        auto innerPattern = factors[i]->clone();
                        auto duPattern = derivative(innerPattern.get(), var);
                        if (duPattern) {
                            std::vector<std::unique_ptr<ASTNode>> rest;
                            for (size_t j = 0; j < factors.size(); ++j)
                                if (j != i) rest.push_back(factors[j]->clone());
                            std::unique_ptr<ASTNode> restNode;
                            if (rest.empty()) restNode = std::make_unique<NumberNode>(1.0);
                            else if (rest.size() == 1) restNode = std::move(rest[0]);
                            else restNode = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(rest));
                            auto t = std::make_unique<IdentNode>("t");
                            auto f_of_t = replaceSubtree(restNode.get(), innerPattern.get(), t.get());
                            std::ostringstream desc;
                            desc << "Замена t = " << trigName << "(" << u.operand(0)->print() << "), dt = " << duPattern->print() << " dx";
                            return integrateSubstitutionWithDesc(expr, var, innerPattern.get(), duPattern.get(), f_of_t.get(), desc.str());
                        }
                    }
                }
            }
        }
        return {"", nullptr};
    };
    auto res = tryTrig(TokenType::SIN, "sin");
    if (res.first != "") return res;
    res = tryTrig(TokenType::COS, "cos");
    if (res.first != "") return res;
    res = tryTrig(TokenType::TG, "tg");
    if (res.first != "") return res;
    res = tryTrig(TokenType::CTG, "ctg");
    return res;
}

static std::pair<std::string, std::unique_ptr<ASTNode>> tryPowerSubstitutionWithDesc(const ASTNode* expr, const std::string& var) {
    if (!expr->isMulti() && !expr->isBinary()) return {"", nullptr};
    auto extractPower = [&](const ASTNode* node, double& n) -> bool {
        if (node->isIdent() && node->asIdent().value == var) { n = 1.0; return true; }
        if (node->isBinary() && node->asBinary().op == TokenType::POW &&
            node->asBinary().operand(0)->isIdent() && node->asBinary().operand(0)->asIdent().value == var &&
            node->asBinary().operand(1)->isNumber()) {
            n = node->asBinary().operand(1)->asNumber().value;
            return true;
        }
        return false;
    };
    std::vector<const ASTNode*> factors;
    if (expr->isMulti() && expr->asMulti().op == TokenType::MUL) {
        for (size_t i = 0; i < expr->asMulti().operandCount(); ++i) factors.push_back(expr->asMulti().operand(i));
    } else if (expr->isBinary() && expr->asBinary().op == TokenType::MUL) {
        factors.push_back(expr->asBinary().operand(0));
        factors.push_back(expr->asBinary().operand(1));
    } else return {"", nullptr};
    int powerIndex = -1;
    double n = 0.0;
    for (size_t i = 0; i < factors.size(); ++i) {
        double tmp;
        if (extractPower(factors[i], tmp) && std::abs(tmp + 1.0) > 1e-12) {
            powerIndex = static_cast<int>(i);
            n = tmp;
            break;
        }
    }
    if (powerIndex == -1) return {"", nullptr};
    std::vector<std::unique_ptr<ASTNode>> restFactors;
    for (size_t i = 0; i < factors.size(); ++i)
        if (static_cast<int>(i) != powerIndex) restFactors.push_back(factors[i]->clone());
    std::unique_ptr<ASTNode> g;
    if (restFactors.empty()) g = std::make_unique<NumberNode>(1.0);
    else if (restFactors.size() == 1) g = std::move(restFactors[0]);
    else g = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(restFactors));
    if (isConstant(g.get(), var)) return {"", nullptr};
    auto base_u = std::make_unique<BinaryOpNode>(TokenType::POW, std::make_unique<IdentNode>(var), std::make_unique<NumberNode>(n + 1.0));
    double global_a = 0.0, global_b = 0.0;
    bool first = true, ok = true;
    std::function<void(const ASTNode*)> collect = [&](const ASTNode* node) {
        if (!node || !ok) return;
        if (equalTrees(node, base_u.get())) {
            if (first) { global_a = 1.0; global_b = 0.0; first = false; }
            else if (std::abs(global_a - 1.0) > 1e-12 || std::abs(global_b - 0.0) > 1e-12) ok = false;
            return;
        }
        if (node->isBinary() && node->asBinary().op == TokenType::MUL) {
            const auto& bin = node->asBinary();
            if (bin.operand(0)->isNumber() && equalTrees(bin.operand(1), base_u.get())) {
                double a = bin.operand(0)->asNumber().value;
                if (first) { global_a = a; global_b = 0.0; first = false; }
                else if (std::abs(global_a - a) > 1e-12 || std::abs(global_b - 0.0) > 1e-12) ok = false;
                return;
            }
            if (bin.operand(1)->isNumber() && equalTrees(bin.operand(0), base_u.get())) {
                double a = bin.operand(1)->asNumber().value;
                if (first) { global_a = a; global_b = 0.0; first = false; }
                else if (std::abs(global_a - a) > 1e-12 || std::abs(global_b - 0.0) > 1e-12) ok = false;
                return;
            }
        }
        if (node->isBinary() && node->asBinary().op == TokenType::PLUS) {
            const auto& bin = node->asBinary();
            if (equalTrees(bin.operand(0), base_u.get()) && bin.operand(1)->isNumber()) {
                double b = bin.operand(1)->asNumber().value;
                if (first) { global_a = 1.0; global_b = b; first = false; }
                else if (std::abs(global_a - 1.0) > 1e-12 || std::abs(global_b - b) > 1e-12) ok = false;
                return;
            }
            if (equalTrees(bin.operand(1), base_u.get()) && bin.operand(0)->isNumber()) {
                double b = bin.operand(0)->asNumber().value;
                if (first) { global_a = 1.0; global_b = b; first = false; }
                else if (std::abs(global_a - 1.0) > 1e-12 || std::abs(global_b - b) > 1e-12) ok = false;
                return;
            }
            if (bin.operand(0)->isBinary() && bin.operand(0)->asBinary().op == TokenType::MUL &&
                bin.operand(0)->asBinary().operand(0)->isNumber() &&
                equalTrees(bin.operand(0)->asBinary().operand(1), base_u.get()) &&
                bin.operand(1)->isNumber()) {
                double a = bin.operand(0)->asBinary().operand(0)->asNumber().value;
                double b = bin.operand(1)->asNumber().value;
                if (first) { global_a = a; global_b = b; first = false; }
                else if (std::abs(global_a - a) > 1e-12 || std::abs(global_b - b) > 1e-12) ok = false;
                return;
            }
            if (bin.operand(1)->isBinary() && bin.operand(1)->asBinary().op == TokenType::MUL &&
                bin.operand(1)->asBinary().operand(0)->isNumber() &&
                equalTrees(bin.operand(1)->asBinary().operand(1), base_u.get()) &&
                bin.operand(0)->isNumber()) {
                double a = bin.operand(1)->asBinary().operand(0)->asNumber().value;
                double b = bin.operand(0)->asNumber().value;
                if (first) { global_a = a; global_b = b; first = false; }
                else if (std::abs(global_a - a) > 1e-12 || std::abs(global_b - b) > 1e-12) ok = false;
                return;
            }
        }
        if (node->isBinary() && node->asBinary().op == TokenType::MINUS) {
            const auto& bin = node->asBinary();
            if (equalTrees(bin.operand(0), base_u.get()) && bin.operand(1)->isNumber()) {
                double b = -bin.operand(1)->asNumber().value;
                if (first) { global_a = 1.0; global_b = b; first = false; }
                else if (std::abs(global_a - 1.0) > 1e-12 || std::abs(global_b - b) > 1e-12) ok = false;
                return;
            }
        }
        if (node->isUnary()) collect(node->asUnary().operand(0));
        else if (node->isBinary()) { collect(node->asBinary().operand(0)); collect(node->asBinary().operand(1)); }
        else if (node->isMulti()) for (size_t i = 0; i < node->asMulti().operandCount(); ++i) collect(node->asMulti().operand(i));
    };
    collect(g.get());
    if (!ok || first) return {"", nullptr};
    std::ostringstream desc;
    desc << "Замена t = ";
    if (std::abs(global_a - 1.0) > 1e-12) desc << global_a << "·";
    desc << "x^{" << n+1 << "}";
    if (std::abs(global_b) > 1e-12) desc << (global_b > 0 ? " + " : " - ") << std::abs(global_b);
    desc << ", dt = " << global_a << "·" << n+1 << "·x^{" << n << "} dx → x^{" << n << "} dx = dt/(" << global_a*(n+1) << ")";
    std::unique_ptr<ASTNode> u_pattern;
    auto base_u_clone = base_u->clone();
    if (std::abs(global_b) < 1e-12) {
        if (std::abs(global_a - 1.0) < 1e-12) u_pattern = std::move(base_u_clone);
        else u_pattern = std::make_unique<BinaryOpNode>(TokenType::MUL, std::make_unique<NumberNode>(global_a), std::move(base_u_clone));
    } else {
        auto term = (std::abs(global_a - 1.0) < 1e-12) ? std::move(base_u_clone) :
                        std::make_unique<BinaryOpNode>(TokenType::MUL, std::make_unique<NumberNode>(global_a), std::move(base_u_clone));
        u_pattern = std::make_unique<BinaryOpNode>(TokenType::PLUS, std::move(term), std::make_unique<NumberNode>(global_b));
    }
    auto du_factor = std::make_unique<NumberNode>(global_a * (n + 1.0));
    auto x_pow_n = factors[powerIndex]->clone();
    auto du_pattern = std::make_unique<BinaryOpNode>(TokenType::MUL, std::move(du_factor), std::move(x_pow_n));
    auto t = std::make_unique<IdentNode>("t");
    auto g_with_t = replaceSubtree(g.get(), u_pattern.get(), t.get());
    if (!g_with_t || dependsOnVar(g_with_t.get(), var)) return {"", nullptr};
    auto int_f_vec = directIntegrate(g_with_t.get(), "t");
    if (int_f_vec.empty()) return {"", nullptr};
    auto F_of_t = normalize(std::move(int_f_vec[0]));
    auto result = replaceSubtree(F_of_t.get(), t.get(), u_pattern.get());
    if (!result) return {"", nullptr};
    auto divisor = std::make_unique<NumberNode>(global_a * (n + 1.0));
    result = std::make_unique<BinaryOpNode>(TokenType::DIV, std::move(result), std::move(divisor));
    result = normalize(std::move(result));
    return {desc.str(), std::move(result)};
}

static std::pair<std::string, std::unique_ptr<ASTNode>> tryIntegrationByPartsWithDesc(const ASTNode* expr, const std::string& var) {
    auto priority = [&](const ASTNode* node) -> int {
        if (!dependsOnVar(node, var)) return -1;
        if (node->isUnary()) {
            TokenType op = node->asUnary().op;
            if (op == TokenType::LN) return 1;
            if (op == TokenType::EXP) return 5;
            if (op == TokenType::ARCSIN || op == TokenType::ARCTAN) return 2;
        }
        if (node->isBinary() && node->asBinary().op == TokenType::POW) return 3;
        if (node->isIdent()) return 3;
        if (node->isUnary() && (node->asUnary().op == TokenType::SIN || node->asUnary().op == TokenType::COS ||
                                node->asUnary().op == TokenType::TG || node->asUnary().op == TokenType::CTG)) return 4;
        return 5;
    };
    std::vector<const ASTNode*> factors;
    if (expr->isMulti() && expr->asMulti().op == TokenType::MUL) {
        for (size_t i = 0; i < expr->asMulti().operandCount(); ++i)
            factors.push_back(expr->asMulti().operand(i));
    } else if (expr->isBinary() && expr->asBinary().op == TokenType::MUL) {
        factors.push_back(expr->asBinary().operand(0));
        factors.push_back(expr->asBinary().operand(1));
    } else {
        return {"", nullptr};
    }
    int bestIndex = -1;
    int bestPriority = 10;
    for (size_t i = 0; i < factors.size(); ++i) {
        int p = priority(factors[i]);
        if (p != -1 && p < bestPriority) {
            bestPriority = p;
            bestIndex = static_cast<int>(i);
        }
    }
    if (bestIndex == -1) return {"", nullptr};
    std::unique_ptr<ASTNode> uNode = factors[bestIndex]->clone();
    std::unique_ptr<ASTNode> dvNode;
    if (factors.size() == 1) {
        dvNode = std::make_unique<NumberNode>(1.0);
    } else {
        std::vector<std::unique_ptr<ASTNode>> dvFactors;
        for (size_t i = 0; i < factors.size(); ++i)
            if (static_cast<int>(i) != bestIndex) dvFactors.push_back(factors[i]->clone());
        if (dvFactors.size() == 1) dvNode = std::move(dvFactors[0]);
        else dvNode = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(dvFactors));
    }
    auto vVec = directIntegrate(dvNode.get(), var);
    if (vVec.empty()) return {"", nullptr};
    auto v = normalize(std::move(vVec[0]));
    auto du = derivative(uNode.get(), var);
    if (!du) return {"", nullptr};
    std::ostringstream desc;
    desc << "Интегрирование по частям: u = " << uNode->print() << ", dv = " << dvNode->print() << " dx → ";
    desc << "du = " << du->print() << " dx, v = " << v->print();
    auto newIntegrand = std::make_unique<BinaryOpNode>(TokenType::MUL, v->clone(), std::move(du));
    auto newIntVec = directIntegrate(newIntegrand.get(), var);
    if (newIntVec.empty()) return {"", nullptr};
    auto newInt = normalize(std::move(newIntVec[0]));
    auto uv = std::make_unique<BinaryOpNode>(TokenType::MUL, uNode->clone(), v->clone());
    auto temp = std::make_unique<BinaryOpNode>(TokenType::MINUS, std::move(uv), std::move(newInt));
    auto result = normalize(std::move(temp));
    desc << " → ∫ u dv = uv - ∫ v du = " << result->print();
    return {desc.str(), std::move(result)};
}

static std::unique_ptr<ASTNode> sumResults(const std::vector<std::unique_ptr<ASTNode>>& nodes) {
    if (nodes.empty()) return nullptr;
    if (nodes.size() == 1) return nodes[0]->clone();
    std::vector<std::unique_ptr<ASTNode>> copies;
    copies.reserve(nodes.size());
    for (const auto& n : nodes) copies.push_back(n->clone());
    auto sum = std::make_unique<MultiOpNode>(TokenType::PLUS, std::move(copies));
    return normalize(std::move(sum));
}

static std::vector<IntegrationStep> cloneSteps(const std::vector<IntegrationStep>& steps) {
    std::vector<IntegrationStep> cloned;
    cloned.reserve(steps.size());
    for (const auto& step : steps) {
        IntegrationStep newStep;
        newStep.description = step.description;
        if (step.result) newStep.result = step.result->clone();
        cloned.push_back(std::move(newStep));
    }
    return cloned;
}

static void collectIntegralsWithSteps(const ASTNode* expr, const std::string& var,
                                      std::vector<std::vector<IntegrationStep>>& solutions,
                                      std::set<std::string>& seen,
                                      std::vector<IntegrationStep> currentSteps) {
    if (!expr) return;
    if (recursionDepth > MAX_RECURSION_DEPTH) return;
    auto simplified = simplifyDivision(expr, var);
    const ASTNode* target = simplified ? simplified.get() : expr;

    if (target->isMulti() && target->asMulti().op == TokenType::PLUS) {
        const auto& sumNode = target->asMulti();
        size_t termCount = sumNode.operandCount();
        std::vector<std::vector<IntegrationStep>> termStepsList;
        std::vector<std::unique_ptr<ASTNode>> termResults;
        bool allOk = true;

        for (size_t i = 0; i < termCount; ++i) {
            const ASTNode* term = sumNode.operand(i);
            std::vector<std::vector<IntegrationStep>> termSolutions;
            std::set<std::string> termSeen;
            recursionDepth++;
            collectIntegralsWithSteps(term, var, termSolutions, termSeen, {});
            recursionDepth--;
            if (termSolutions.empty()) {
                allOk = false;
                break;
            }
            termStepsList.push_back(std::move(termSolutions[0]));
            termResults.push_back(termStepsList.back().back().result->clone());
        }

        if (allOk) {
            std::vector<IntegrationStep> combinedSteps = cloneSteps(currentSteps);
            for (size_t i = 0; i < termCount; ++i) {
                IntegrationStep header;
                header.description = "Интегрируем слагаемое " + std::to_string(i+1) + ": " + sumNode.operand(i)->print();
                header.result = nullptr;
                combinedSteps.push_back(std::move(header));
                for (const auto& step : termStepsList[i]) {
                    IntegrationStep newStep;
                    newStep.description = step.description;
                    newStep.result = step.result ? step.result->clone() : nullptr;
                    combinedSteps.push_back(std::move(newStep));
                }
            }
            auto totalResult = sumResults(termResults);
            if (totalResult) {
                totalResult = normalize(std::move(totalResult));
                IntegrationStep finalStep;
                finalStep.description = "Суммируем полученные первообразные";
                finalStep.result = totalResult->clone();
                combinedSteps.push_back(std::move(finalStep));
                std::string key = normalizedString(totalResult.get());
                if (seen.find(key) == seen.end()) {
                    seen.insert(key);
                    solutions.push_back(std::move(combinedSteps));
                }
            }
        }
        return;
    }

    if (isConstant(target, var)) {
        auto res = std::make_unique<BinaryOpNode>(TokenType::MUL, target->clone(), std::make_unique<IdentNode>(var));
        auto norm = normalize(std::move(res));
        std::string key = normalizedString(norm.get());
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            IntegrationStep step;
            step.description = "∫ " + target->print() + " dx = " + target->print() + "·x";
            step.result = norm->clone();
            auto newSteps = cloneSteps(currentSteps);
            newSteps.push_back(std::move(step));
            solutions.push_back(std::move(newSteps));
        }
        return;
    }

    auto [desc, tableRes] = tryTableIntegralWithDesc(target, var);
    if (tableRes) {
        auto norm = normalize(std::move(tableRes));
        std::string key = normalizedString(norm.get());
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            IntegrationStep step;
            step.description = desc + " = " + norm->print();
            step.result = norm->clone();
            auto newSteps = cloneSteps(currentSteps);
            newSteps.push_back(std::move(step));
            solutions.push_back(std::move(newSteps));
        }
    }

    auto [expDesc, expRes] = tryExponentialSubstitutionWithDesc(target, var);
    if (expRes) {
        auto norm = normalize(std::move(expRes));
        std::string key = normalizedString(norm.get());
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            IntegrationStep step;
            step.description = expDesc + " → " + norm->print();
            step.result = norm->clone();
            auto newSteps = cloneSteps(currentSteps);
            newSteps.push_back(std::move(step));
            solutions.push_back(std::move(newSteps));
        }
    }

    auto [logDesc, logRes] = tryLogarithmicSubstitutionWithDesc(target, var);
    if (logRes) {
        auto norm = normalize(std::move(logRes));
        std::string key = normalizedString(norm.get());
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            IntegrationStep step;
            step.description = logDesc + " → " + norm->print();
            step.result = norm->clone();
            auto newSteps = cloneSteps(currentSteps);
            newSteps.push_back(std::move(step));
            solutions.push_back(std::move(newSteps));
        }
    }

    auto [trigDesc, trigRes] = tryTrigonometricSubstitutionWithDesc(target, var);
    if (trigRes) {
        auto norm = normalize(std::move(trigRes));
        std::string key = normalizedString(norm.get());
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            IntegrationStep step;
            step.description = trigDesc + " → " + norm->print();
            step.result = norm->clone();
            auto newSteps = cloneSteps(currentSteps);
            newSteps.push_back(std::move(step));
            solutions.push_back(std::move(newSteps));
        }
    }

    auto [powDesc, powRes] = tryPowerSubstitutionWithDesc(target, var);
    if (powRes) {
        auto norm = normalize(std::move(powRes));
        std::string key = normalizedString(norm.get());
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            IntegrationStep step;
            step.description = powDesc + " → " + norm->print();
            step.result = norm->clone();
            auto newSteps = cloneSteps(currentSteps);
            newSteps.push_back(std::move(step));
            solutions.push_back(std::move(newSteps));
        }
    }

    auto [partsDesc, partsRes] = tryIntegrationByPartsWithDesc(target, var);
    if (partsRes) {
        auto norm = normalize(std::move(partsRes));
        std::string key = normalizedString(norm.get());
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            IntegrationStep step;
            step.description = partsDesc;
            step.result = norm->clone();
            auto newSteps = cloneSteps(currentSteps);
            newSteps.push_back(std::move(step));
            solutions.push_back(std::move(newSteps));
        }
    }

    if (target->isMulti() && target->asMulti().op == TokenType::MUL) {
        double constFactor = 1.0;
        std::vector<std::unique_ptr<ASTNode>> nonConst;
        for (size_t i = 0; i < target->asMulti().operandCount(); ++i) {
            auto* op = target->asMulti().operand(i);
            if (op->isNumber()) constFactor *= op->asNumber().value;
            else if (isConstant(op, var)) { }
            else nonConst.push_back(op->clone());
        }
        if (!nonConst.empty() && std::abs(constFactor - 1.0) > 1e-12) {
            std::unique_ptr<ASTNode> rest;
            if (nonConst.size() == 1) rest = std::move(nonConst[0]);
            else rest = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(nonConst));
            std::vector<std::vector<IntegrationStep>> restSols;
            std::set<std::string> restSeen;
            recursionDepth++;
            collectIntegralsWithSteps(rest.get(), var, restSols, restSeen, {});
            recursionDepth--;
            if (!restSols.empty()) {
                auto& restSteps = restSols[0];
                auto restResult = restSteps.back().result->clone();
                std::unique_ptr<ASTNode> result;
                std::vector<std::unique_ptr<ASTNode>> mulVec;
                mulVec.push_back(std::make_unique<NumberNode>(constFactor));
                mulVec.push_back(std::move(restResult));
                result = std::make_unique<MultiOpNode>(TokenType::MUL, std::move(mulVec));
                auto norm = normalize(std::move(result));
                std::string key = normalizedString(norm.get());
                if (seen.find(key) == seen.end()) {
                    seen.insert(key);
                    IntegrationStep step;
                    std::ostringstream oss;
                    oss << "Выносим константу " << constFactor << " за знак интеграла → " << norm->print();
                    step.description = oss.str();
                    step.result = norm->clone();
                    auto newSteps = cloneSteps(currentSteps);
                    newSteps.push_back(std::move(step));
                    solutions.push_back(std::move(newSteps));
                }
            }
        }
    }
}

std::vector<std::vector<IntegrationStep>> integrateWithSteps(const ASTNode* expr, const std::string& var) {
    if (!expr) return {};
    if (recursionDepth > MAX_RECURSION_DEPTH) return {};
    ++recursionDepth;
    std::vector<std::vector<IntegrationStep>> solutions;
    std::set<std::string> seen;
    std::vector<IntegrationStep> emptySteps;
    collectIntegralsWithSteps(expr, var, solutions, seen, std::move(emptySteps));
    --recursionDepth;
    return solutions;
}

std::vector<std::unique_ptr<ASTNode>> directIntegrate(const ASTNode* expr, const std::string& var) {
    if (!expr) return {};
    auto solutions = integrateWithSteps(expr, var);
    std::vector<std::unique_ptr<ASTNode>> results;
    for (auto& sol : solutions) {
        if (!sol.empty() && sol.back().result) {
            results.push_back(sol.back().result->clone());
        }
    }
    return results;
}