#pragma once
#include "Ast.h"
#include "Parser.h"
#include "Normalizer.h"
#include "IntegralClassifier.h"
#include <memory>
#include <vector>
#include <string>
#include <algorithm>

class ExprTree {
public:
    ExprTree() = default;
    explicit ExprTree(std::unique_ptr<ASTNode> root) noexcept : root_(std::move(root)) {}
    ~ExprTree() = default;
    ExprTree(const ExprTree& other) : root_(other.root_ ? other.root_->clone() : nullptr) {}
    ExprTree& operator=(const ExprTree& other) {
        if (this != &other) root_ = other.root_ ? other.root_->clone() : nullptr;
        return *this;
    }
    ExprTree(ExprTree&&) noexcept = default;
    ExprTree& operator=(ExprTree&&) noexcept = default;

    static ExprTree fromTokens(const std::vector<Token>& tokens) {
        Parser parser(tokens);
        return ExprTree(parser.parse());
    }
    std::string print() const { return root_ ? root_->print() : ""; }
    bool empty() const noexcept { return root_ == nullptr; }
    const ASTNode* root() const { return root_.get(); }
    void setRoot(std::unique_ptr<ASTNode> newRoot) { root_ = std::move(newRoot); }
    void normalize() { if (root_) root_ = ::normalize(std::move(root_)); }

    std::vector<std::string> integrate(const std::string& var) {
        if (!root_) return {};
        auto resultsVec = directIntegrate(root_.get(), var);
        std::vector<std::string> strResults;
        for (auto& res : resultsVec) {
            auto norm = ::normalize(std::move(res));
            if (norm) strResults.push_back(norm->print());
        }
        std::sort(strResults.begin(), strResults.end());
        strResults.erase(std::unique(strResults.begin(), strResults.end()), strResults.end());
        return strResults;
    }

private:
    std::unique_ptr<ASTNode> root_;
};