#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../lp/simplex.hpp"
#include <string>
#include <vector>

namespace hypernova::validation {

// A numerical-quality report for an LP: matrix statistics, a condition-number
// estimate of the normal-equations matrix A*A^T (Hager), and solution-quality
// residuals, summarized into a one-word quality grade plus warnings.
struct NumericalDiagnostics {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;

    std::size_t num_variables = 0;
    std::size_t num_constraints = 0;
    std::size_t num_nonzeros = 0;

    double max_coefficient = 0.0;
    double min_nonzero_coefficient = 0.0;
    double dynamic_range = 0.0;      // max / min_nonzero (0.0 if undefined)
    double condition_estimate = 0.0; // 1-norm cond(A*A^T) from Hager (0 if singular/none)

    double primal_infeasibility = 0.0;
    double dual_infeasibility = 0.0;
    double complementarity = 0.0;
    double integrality_violation = 0.0;

    std::string quality;            // "good" | "warn" | "poor"
    std::vector<std::string> warnings;
    bool matrix_rank_deficient = false;

    std::string to_string() const;
    std::string to_json() const;
};

// Solves a probe LP and assembles the numerical-quality report above.
class DiagnosticsAnalyzer {
public:
    explicit DiagnosticsAnalyzer(const numerical::ToleranceConfig& tol =
                                     numerical::ToleranceConfig::industrial_defaults(),
                                 const lp::SimplexOptions& options = lp::SimplexOptions());
    ~DiagnosticsAnalyzer() = default;

    NumericalDiagnostics analyze(const model::Problem& problem);

private:
    numerical::ToleranceConfig tol_;
    lp::SimplexOptions options_;
};

} // namespace hypernova::validation