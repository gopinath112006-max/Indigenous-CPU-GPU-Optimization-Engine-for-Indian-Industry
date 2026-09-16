#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include <vector>
#include <cstddef>
#include <optional>

namespace hypernova::validation {

struct VerificationResult {
    bool feasible = false;
    bool optimal = false;
    double primal_infeasibility = 0.0;
    double dual_infeasibility = 0.0;
    double complementarity = 0.0;
    double integrality_violation = 0.0;
    std::vector<std::size_t> violated_constraints;
    std::string message;
};

class SolutionVerifier {
public:
    explicit SolutionVerifier(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults());
    ~SolutionVerifier() = default;

    bool verify(const model::Problem& problem, const model::Solution& solution);
    bool verify_feasible(const model::Problem& problem, const model::Solution& solution);
    VerificationResult verify_detailed(const model::Problem& problem, const model::Solution& solution);

private:
    numerical::ToleranceConfig tol_;

    VerificationResult check_primal_feasibility(const model::Problem& problem, const model::Solution& solution) const;
    VerificationResult check_dual_feasibility(const model::Problem& problem, const model::Solution& solution) const;
    VerificationResult check_complementarity(const model::Problem& problem, const model::Solution& solution) const;
    VerificationResult check_integrality(const model::Problem& problem, const model::Solution& solution) const;
};

} // namespace hypernova::validation