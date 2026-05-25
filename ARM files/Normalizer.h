#pragma once
#include "Ast.h"
#include <memory>

std::unique_ptr<ASTNode> normalize(std::unique_ptr<ASTNode> node);
double evaluateUnary(TokenType op, double value);
double evaluateBinary(TokenType op, double left, double right);
double evaluateMulti(TokenType op, const std::vector<double>& values);