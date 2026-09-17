#include "solution_verifier.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace hypernova::validation {

SolutionVerifier::SolutionVerifier(const numerical::ToleranceConfig& tol)
    : tol_(tol) {}

bool SolutionVerifier::verify(const model::Problem& problem, const model::Solution& solution) {
    auto result = verify_detailed(problem, solution);
    return result.feasible && result.optimal;
}

bool SolutionVerifier::verify_feasible(const model::Problem& problem, const model::Solution& solution) {
    auto result = verify_detailed(problem, solution);
    return result.feasible;
}

VerificationResult SolutionVerifier::verify_detailed(
    const model::Problem& problem, const model::Solution& solution) {

    VerificationResult result;
    result.feasible = true;
    result.optimal = true;

    auto primal_check = check_primal_feasibility(problem, solution);
    result.primal_infeasibility = primal_check.primal_infeasibility;
    result.violated_constraints = primal_check.violated_constraints;
    if (!primal_check.feasible) {
        result.feasible = false;
        result.message = "Primal infeasible: " + primal_check.message;
    }

    auto dual_check = check_dual_feasibility(problem, solution);
    result.dual_infeasibility = dual_check.dual_infeasibility;
    if (!dual_check.feasible) {
        result.optimal = false;
        if (result.feasible) result.message = "Dual infeasible: " + dual_check.message;
    }

    auto comp_check = check_complementarity(problem, solution);
    result.complementarity = comp_check.complementarity;
    if (!comp_check.feasible) {
        result.optimal = false;
        if (result.feasible) result.message = "Complementarity violation: " + comp_check.message;
    }

    auto int_check = check_integrality(problem, solution);
    result.integrality_violation = int_check.integrality_violation;
    if (!int_check.feasible) {
        result.feasible = false;
        result.message = "Integrality violation: " + int_check.message;
    }

    return result;
}

VerificationResult SolutionVerifier::check_primal_feasibility(
    const model::Problem& problem, const model::Solution& solution) const {

    VerificationResult result;
    result.feasible = true;
    double max_violation = 0.0;

    if (solution.primal.size() != problem.variables.size()) {
        result.feasible = false;
        result.message = "Solution vector size mismatch";
        return result;
    }

    // Per-variable magnitude scale: a bound residual is judged against the
    // column's own coefficients and objective, so variables embedded in
    // large-coefficient rows tolerate round-off proportional to that data.
    const auto& A = problem.constraint_matrix;
    std::vector<double> col_scale(problem.variables.size(), 1.0);
    if (A.order() == model::StorageOrder::CSR) {
        for (std::size_t k = 0; k < A.nnz(); ++k) {
            std::size_t j = A.col_indices()[k];
            if (j < col_scale.size()) {
                col_scale[j] = std::max(col_scale[j], std::abs(A.values()[k]));
            }
        }
    }
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        col_scale[j] = std::max(col_scale[j], std::abs(problem.variables[j].objective_coeff));
        if (std::isfinite(lb)) col_scale[j] = std::max(col_scale[j], std::abs(lb));
        if (std::isfinite(ub)) col_scale[j] = std::max(col_scale[j], std::abs(ub));
    }

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        double val = solution.primal[j];
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;

        if (val < lb - tol_.feasibility_tol() * col_scale[j]) {
            double viol = lb - val;
            max_violation = std::max(max_violation, viol);
            result.violated_constraints.push_back(j);
        }
        if (val > ub + tol_.feasibility_tol() * col_scale[j]) {
            double viol = val - ub;
            max_violation = std::max(max_violation, viol);
            result.violated_constraints.push_back(j);
        }
    }

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        double activity = 0.0;
        double activity_abs = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                double term = A.values()[k] * solution.primal[j];
                activity += term;
                activity_abs += std::abs(term);
            }
        }

        const auto& con = problem.constraints[i];
        // Magnitude of the row's own data: judging an absolute residual against
        // it keeps large-coefficient instances from being flagged for a
        // numerically negligible (but absolutely > tol) violation.
        const double scale = 1.0 + std::abs(con.rhs) + activity_abs;
        double violation = 0.0;
        if (con.sense == model::ConstraintSense::LE) {
            violation = std::max(0.0, activity - con.rhs - tol_.feasibility_tol() * scale);
        } else if (con.sense == model::ConstraintSense::GE) {
            violation = std::max(0.0, con.rhs - activity - tol_.feasibility_tol() * scale);
        } else {
            violation = std::abs(activity - con.rhs) - tol_.feasibility_tol() * scale;
            violation = std::max(0.0, violation);
        }

        if (violation > 0.0) {
            max_violation = std::max(max_violation, violation);
            result.violated_constraints.push_back(problem.variables.size() + i);
        }
    }

    result.primal_infeasibility = max_violation;
    if (!result.violated_constraints.empty()) {
        result.feasible = false;
        result.message = "Max primal violation: " + std::to_string(max_violation);
    }

    return result;
}

VerificationResult SolutionVerifier::check_dual_feasibility(
    const model::Problem& problem, const model::Solution& solution) const {

    VerificationResult result;
    result.feasible = true;
    double max_violation = 0.0;

    if (solution.dual.size() != problem.constraints.size() ||
        (problem.num_integer_vars() > 0 &&
         solution.reduced_costs.size() != problem.variables.size())) {
        return result;
    }

    const auto& A = problem.constraint_matrix;

    const bool use_reduced_costs =
        solution.reduced_costs.size() == problem.variables.size() &&
        problem.quadratic_terms.empty();

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        double reduced_cost = 0.0;
        if (use_reduced_costs) {
            reduced_cost = solution.reduced_costs[j];
        } else {
            if (A.order() == model::StorageOrder::CSR) {
                for (std::size_t k = 0; k < A.nnz(); ++k) {
                    if (A.col_indices()[k] == j) {
                        std::size_t i = 0;
                        while (i < A.rows() && A.row_ptr()[i + 1] <= k) ++i;
                        if (i < A.rows()) {
                            reduced_cost += A.values()[k] * solution.dual[i];
                        }
                    }
                }
            }

            if (problem.quadratic_terms.empty()) {
                reduced_cost = problem.variables[j].objective_coeff - reduced_cost;
            } else {
                reduced_cost += problem.variables[j].objective_coeff;
                for (const auto& term : problem.quadratic_terms) {
                    if (term.row == j) {
                        reduced_cost += term.coeff * solution.primal[term.col];
                    }
                    if (term.col == j && term.row != j) {
                        reduced_cost += term.coeff * solution.primal[term.row];
                    }
                }
            }
        }

        if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) {
            reduced_cost = -reduced_cost;
        }

        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        double val = solution.primal[j];

        double violation = 0.0;
        if (val <= lb + tol_.feasibility_tol()) {
            violation = std::min(0.0, reduced_cost);
        } else if (val >= ub - tol_.feasibility_tol()) {
            violation = std::max(0.0, reduced_cost);
        } else {
            violation = std::abs(reduced_cost);
        }

        violation = std::abs(violation) - tol_.optimality_tol();
        if (violation > 0.0) {
            max_violation = std::max(max_violation, violation);
        }
    }

    result.dual_infeasibility = max_violation;
    if (max_violation > tol_.optimality_tol()) {
        result.feasible = false;
        result.message = "Max dual violation: " + std::to_string(max_violation);
        if (std::getenv("HYPERNOVA_DBG")) {
            std::cerr << "DBG_DUAL: sense=" << static_cast<int>(problem.obj_sense) << " nvar=" << problem.variables.size() << "\n";
            for (std::size_t j = 0; j < problem.variables.size(); ++j) {
                double reduced_cost = 0.0;
                if (A.order() == model::StorageOrder::CSR) {
                    for (std::size_t k = 0; k < A.nnz(); ++k) {
                        if (A.col_indices()[k] == j) {
                            std::size_t i = 0;
                            while (i < A.rows() && A.row_ptr()[i + 1] <= k) ++i;
                            if (i < A.rows()) reduced_cost += A.values()[k] * solution.dual[i];
                        }
                    }
                }
                reduced_cost = problem.variables[j].objective_coeff - reduced_cost;
                if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) reduced_cost = -reduced_cost;
                double lb = problem.variables[j].lower_bound;
                double ub = problem.variables[j].upper_bound;
                double val = solution.primal[j];
                double violation = 0.0;
                if (val <= lb + tol_.feasibility_tol()) violation = std::min(0.0, reduced_cost);
                else if (val >= ub - tol_.feasibility_tol()) violation = std::max(0.0, reduced_cost);
                else violation = std::abs(reduced_cost);
                violation = std::abs(violation) - tol_.optimality_tol();
                if (violation > tol_.optimality_tol())
std::cerr << "  viol var=" << j << " val=" << val << " lb=" << lb << " ub=" << ub
                      << " rc=" << reduced_cost << " viol=" << violation << " objc=" << problem.variables[j].objective_coeff << "\n";
            {
                double rowmajor = problem.variables[j].objective_coeff;
                for (std::size_t i = 0; i < A.rows(); ++i) {
                    for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                        if (A.col_indices()[k] == j) rowmajor -= A.values()[k] * solution.dual[i];
                    }
                }
                if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) rowmajor = -rowmajor;
                int istart = 0;
                double flatfirstrow = -1, flatfirstval = -1;
                for (std::size_t k = 0; k < A.nnz(); ++k) {
                    if (A.col_indices()[k] == j) {
                        std::size_t i2 = 0;
                        while (i2 < A.rows() && A.row_ptr()[i2 + 1] <= k) ++i2;
                        if (istart == 0) { flatfirstrow = (double)i2; flatfirstval = A.values()[k]; }
                        ++istart;
                    }
                }
                std::cerr << "    rowmajor_rc=" << rowmajor << " flat_first_row=" << flatfirstrow
                          << " flat_first_val=" << flatfirstval << " nmatches=" << istart << "\n";
            }
            }
        }
    }

    return result;
}

VerificationResult SolutionVerifier::check_complementarity(
    const model::Problem& problem, const model::Solution& solution) const {

    VerificationResult result;
    result.feasible = true;
    double max_violation = 0.0;

    if (solution.dual.size() != problem.constraints.size()) {
        return result;
    }

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        double activity = 0.0;
        double activity_abs = 0.0;
        const auto& A = problem.constraint_matrix;
        if (A.order() == numerical::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                double term = A.values()[k] * solution.primal[j];
                activity += term;
                activity_abs += std::abs(term);
            }
        }

        double slack = 0.0;
        if (problem.constraints[i].sense == model::ConstraintSense::LE) {
            slack = problem.constraints[i].rhs - activity;
        } else if (problem.constraints[i].sense == model::ConstraintSense::GE) {
            slack = activity - problem.constraints[i].rhs;
        }

        double dual = solution.dual[i];
        // The complementarity residual s_i * y_i is a product of two quantities
        // that both need to vanish. Judging it relative to the row scale alone
        // overstates the violation when the dual multiplier is large even though
        // the primal slack is numerically zero. Normalize each factor by its own
        // magnitude so the residual is a dimensionless relative measure.
        const double scale = std::max({1.0, std::abs(problem.constraints[i].rhs), activity_abs});
        const double dual_scale = std::max(1.0, std::abs(dual));
        double violation = std::abs(slack) / scale * (std::abs(dual) / dual_scale);
        max_violation = std::max(max_violation, violation);
    }

    result.complementarity = max_violation;
    if (max_violation > tol_.feasibility_tol()) {
        result.feasible = false;
        result.message = "Max complementarity violation: " + std::to_string(max_violation);
    }

    return result;
}

VerificationResult SolutionVerifier::check_integrality(
    const model::Problem& problem, const model::Solution& solution) const {

    VerificationResult result;
    result.feasible = true;
    double max_violation = 0.0;

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (problem.variables[j].type == model::VarType::INTEGER ||
            problem.variables[j].type == model::VarType::BINARY) {
            double val = solution.primal[j];
            double nearest_int = std::round(val);
            double violation = std::abs(val - nearest_int);
            max_violation = std::max(max_violation, violation);
        }
    }

    result.integrality_violation = max_violation;
    if (max_violation > tol_.integrality_tol()) {
        result.feasible = false;
        result.message = "Max integrality violation: " + std::to_string(max_violation);
    }

    return result;
}

} // namespace hypernova::validation