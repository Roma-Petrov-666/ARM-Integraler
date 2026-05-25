#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Ast.h"

struct IntegrationStep {
    std::string description;
    std::unique_ptr<ASTNode> result;

    IntegrationStep() = default;
    IntegrationStep(const IntegrationStep&) = delete;
    IntegrationStep& operator=(const IntegrationStep&) = delete;
    IntegrationStep(IntegrationStep&&) = default;
    IntegrationStep& operator=(IntegrationStep&&) = default;
};

std::vector<std::vector<IntegrationStep>> integrateWithSteps(const ASTNode* expr, const std::string& var);
std::vector<std::unique_ptr<ASTNode>> directIntegrate(const ASTNode* expr, const std::string& var);