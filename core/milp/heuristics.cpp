#include "heuristics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace hypernova::milp {

Heuristic::Heuristic(const numerical::ToleranceConfig& tol)
    : tol_(tol) {}

bool Heuristic::check_feasibility(const model::Problem& problem, const std::vector<double>& solution) const {
    const auto& A = problem.constraint_matrix;

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        double val = solution[j];
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;

        if (val < lb - tol_.feasibility_tol() || val > ub + tol_.feasibility_tol()) {
            return false;
        }
    }

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        double activity = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                activity += A.values()[k] * solution[j];
            }
        }

        const auto& con = problem.constraints[i];
        if (con.sense == model::ConstraintSense::LE && activity > con.rhs + tol_.feasibility_tol()) {
            return false;
        }
        if (con.sense == model::ConstraintSense::GE && activity < con.rhs - tol_.feasibility_tol()) {
            return false;
        }
        if (con.sense == model::ConstraintSense::EQ && std::abs(activity - con.rhs) > tol_.feasibility_tol()) {
            return false;
        }
    }

    return true;
}

double Heuristic::compute_objective(const model::Problem& problem, const std::vector<double>& solution) const {
    double obj = problem.obj_offset;
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        obj += problem.variables[j].objective_coeff * solution[j];
    }
    return obj;
}

HeuristicResult RoundingHeuristic::run(const model::Problem& problem, const std::vector<double>& lp_solution) {
    HeuristicResult result;
    result.solution = lp_solution;

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (problem.variables[j].type != model::VarType::CONTINUOUS) {
            double val = result.solution[j];
            double rounded = std::round(val);
            double lb = problem.variables[j].lower_bound;
            double ub = problem.variables[j].upper_bound;

            if (rounded < lb) rounded = lb;
            if (rounded > ub) rounded = ub;

            if (std::abs(rounded - lb) < tol_.feasibility_tol()) rounded = lb;
            if (std::abs(rounded - ub) < tol_.feasibility_tol()) rounded = ub;

            result.solution[j] = rounded;
        }
    }

    const auto& A = problem.constraint_matrix;
    if (A.order() != model::StorageOrder::CSR || A.rows() != problem.constraints.size()) {
        // Not usable: fall back to a plain feas-check on the rounded solution.
        if (check_feasibility(problem, result.solution)) {
            result.found_solution = true;
            result.objective_value = compute_objective(problem, result.solution);
        }
        return result;
    }
    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        double activity = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                activity += A.values()[k] * result.solution[j];
            }
        }

        const auto& con = problem.constraints[i];
        double excess = 0.0;
        if (con.sense == model::ConstraintSense::LE) {
            excess = activity - con.rhs;
        } else if (con.sense == model::ConstraintSense::GE) {
            excess = con.rhs - activity;
        } else {
            excess = std::abs(activity - con.rhs);
        }

        if (excess <= tol_.feasibility_tol()) continue;

        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            std::size_t j = A.col_indices()[k];
            if (problem.variables[j].type != model::VarType::CONTINUOUS) continue;
            double coeff = A.values()[k];
            if (std::abs(coeff) < tol_.feasibility_tol()) continue;

            double adjustment = excess / coeff;
            double new_val = result.solution[j] - adjustment;
            double lb = problem.variables[j].lower_bound;
            double ub = problem.variables[j].upper_bound;
            new_val = std::clamp(new_val, lb, ub);

            if (con.sense == model::ConstraintSense::LE || con.sense == model::ConstraintSense::EQ) {
                if (new_val > result.solution[j]) new_val = result.solution[j];
            }
            if (con.sense == model::ConstraintSense::GE || con.sense == model::ConstraintSense::EQ) {
                if (new_val < result.solution[j]) new_val = result.solution[j];
            }

            result.solution[j] = new_val;
            break;
        }
    }

    if (check_feasibility(problem, result.solution)) {
        result.found_solution = true;
        result.objective_value = compute_objective(problem, result.solution);
    }

    return result;
}

DivingHeuristic::DivingHeuristic(const numerical::ToleranceConfig& tol, int max_iterations)
    : Heuristic(tol), max_iterations_(max_iterations) {}

HeuristicResult DivingHeuristic::run(const model::Problem& problem, const std::vector<double>& lp_solution) {
    HeuristicResult result;
    std::vector<double> current = lp_solution;

    for (int iter = 0; iter < max_iterations_; ++iter) {
        int best_var = -1;
        double best_frac = -1.0;

        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            if (problem.variables[j].type != model::VarType::CONTINUOUS) {
                double val = current[j];
                double frac = val - std::floor(val);
                if (frac > tol_.feasibility_tol() && frac < 1.0 - tol_.feasibility_tol()) {
                    double dist = std::abs(frac - 0.5);
                    if (best_var < 0 || dist < best_frac) {
                        best_frac = dist;
                        best_var = static_cast<int>(j);
                    }
                }
            }
        }

        if (best_var < 0) break;

        double val = current[static_cast<std::size_t>(best_var)];
        double rounded = (val - std::floor(val) < 0.5) ? std::floor(val) : std::ceil(val);
        double lb = problem.variables[best_var].lower_bound;
        double ub = problem.variables[best_var].upper_bound;
        rounded = std::clamp(rounded, lb, ub);
        current[best_var] = rounded;
    }

    // A diving heuristic only reports a solution when the final point is fully
    // integral; constraint-feasible-but-fractional points are not valid.
    bool all_integral = true;
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (problem.variables[j].type != model::VarType::CONTINUOUS) {
            if (std::abs(current[j] - std::round(current[j])) > tol_.integrality_tol()) {
                all_integral = false;
                break;
            }
        }
    }

    if (all_integral && check_feasibility(problem, current)) {
        result.found_solution = true;
        result.solution = current;
        result.objective_value = compute_objective(problem, current);
    }

    return result;
}

HeuristicResult FeasibilityPumpHeuristic::run(const model::Problem& problem, const std::vector<double>& lp_solution) {
    HeuristicResult result;
    std::vector<double> x = lp_solution;
    std::vector<double> x_round = lp_solution;

    for (int iter = 0; iter < 100; ++iter) {
        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            if (problem.variables[j].type != model::VarType::CONTINUOUS) {
                double val = x[j];
                x_round[j] = std::round(val);
                x_round[j] = std::clamp(x_round[j],
                    problem.variables[j].lower_bound,
                    problem.variables[j].upper_bound);
            } else {
                x_round[j] = x[j];
            }
        }

        if (check_feasibility(problem, x_round)) {
            result.found_solution = true;
            result.solution = x_round;
            result.objective_value = compute_objective(problem, x_round);
            break;
        }

        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            if (problem.variables[j].type != model::VarType::CONTINUOUS) {
                x[j] = 0.5 * x[j] + 0.5 * x_round[j];
            }
        }
    }

    return result;
}

RINSHeuristic::RINSHeuristic(const numerical::ToleranceConfig& tol, int frequency)
    : Heuristic(tol), frequency_(frequency) {}

HeuristicResult RINSHeuristic::run(const model::Problem& problem, const std::vector<double>& lp_solution) {
    HeuristicResult result;

    if (incumbent_.empty()) return result;

    std::vector<double> x = lp_solution;

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (problem.variables[j].type != model::VarType::CONTINUOUS) {
            double lp_val = x[j];
            double inc_val = incumbent_[j];

            if (std::abs(lp_val - inc_val) < tol_.feasibility_tol()) {
                x[j] = inc_val;
            } else if (std::abs(lp_val - std::round(lp_val)) < tol_.feasibility_tol()) {
                x[j] = inc_val;
            }
        }
    }

    if (check_feasibility(problem, x)) {
        result.found_solution = true;
        result.solution = x;
        result.objective_value = compute_objective(problem, x);
    }

    return result;
}

HeuristicManager::HeuristicManager(const numerical::ToleranceConfig& tol)
    : tol_(tol) {}

void HeuristicManager::add_heuristic(std::unique_ptr<Heuristic> heuristic) {
    heuristics_.push_back(std::move(heuristic));
}

HeuristicResult HeuristicManager::run_all(const model::Problem& problem, const std::vector<double>& lp_solution) {
    HeuristicResult best_result;

    for (auto& heuristic : heuristics_) {
        auto result = heuristic->run(problem, lp_solution);
        if (result.found_solution &&
            (!best_result.found_solution || result.objective_value < best_result.objective_value)) {
            best_result = std::move(result);
        }
    }

    return best_result;
}

void HeuristicManager::set_incumbent(const std::vector<double>& incumbent) {
    for (auto& heuristic : heuristics_) {
        if (auto rins = dynamic_cast<RINSHeuristic*>(heuristic.get())) {
            rins->set_incumbent(incumbent);
        }
    }
}

} // namespace hypernova::milp