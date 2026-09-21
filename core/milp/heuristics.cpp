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
    for (const auto& term : problem.quadratic_terms) {
        if (term.row == term.col) {
            obj += 0.5 * term.coeff * solution[term.row] * solution[term.row];
        } else {
            obj += term.coeff * solution[term.row] * solution[term.col];
        }
    }
    return obj;
}
RoundingHeuristic::RoundingHeuristic(const numerical::ToleranceConfig& tol, int trials, unsigned seed)
    : Heuristic(tol), trials_(trials), seed_(seed) {}

void RoundingHeuristic::repair(const model::Problem& problem, std::vector<double>& x) const {
    const auto& A = problem.constraint_matrix;
    // Repair pass: for every violated row, nudge one variable in that row to
    // reduce the violation. Unlike a continuous-slack-only repair this also
    // moves integer variables (in integer steps), so pure-integer MIPs can be
    // repaired at all. Multiple sweeps let repairs propagate between rows.
    for (int sweep = 0; sweep < 8; ++sweep) {
        bool repaired_any = false;
        for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
            double activity = 0.0;
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                activity += A.values()[k] * x[A.col_indices()[k]];
            }

            const auto& con = problem.constraints[i];
            const double viol = activity - con.rhs;  // LE: >0, GE: <0, EQ: any
            if (std::abs(viol) <= tol_.feasibility_tol()) continue;

            // Pick the single variable whose move best reduces |violation|.
            std::size_t best_j = std::numeric_limits<std::size_t>::max();
            double best_new = 0.0;
            double best_abs_viol = std::abs(viol);
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                const double coeff = A.values()[k];
                if (std::abs(coeff) <= tol_.feasibility_tol()) continue;

                const double old = x[j];
                const double lb = problem.variables[j].lower_bound;
                const double ub = problem.variables[j].upper_bound;
                double delta = -viol / coeff;   // continuous step to zero the row
                double nv = old + delta;

                if (problem.variables[j].type != model::VarType::CONTINUOUS) {
                    double rdelta = std::round(delta);
                    if (std::abs(rdelta) < 0.5) rdelta = (delta >= 0.0) ? 1.0 : -1.0;
                    nv = old + rdelta;
                }
                nv = std::clamp(nv, lb, ub);

                const double abs_viol = std::abs(viol + coeff * (nv - old));
                if (abs_viol < best_abs_viol - tol_.feasibility_tol()) {
                    best_abs_viol = abs_viol;
                    best_j = j;
                    best_new = nv;
                }
            }
            if (best_j != std::numeric_limits<std::size_t>::max()) {
                x[best_j] = best_new;
                repaired_any = true;
            }
        }
        if (!repaired_any) break;
    }
}

HeuristicResult RoundingHeuristic::run(const model::Problem& problem, const std::vector<double>& lp_solution) {
    HeuristicResult result;
    result.solution = lp_solution;
    if (lp_solution.size() != problem.variables.size()) return result;

    const auto& A = problem.constraint_matrix;
    const bool matrix_ok =
        A.order() == model::StorageOrder::CSR && A.rows() == problem.constraints.size();

    std::mt19937 rng(seed_);
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    double best_obj = 0.0;
    bool have_best = false;
    std::vector<double> best_x;

    // Trial 0 is the deterministic nearest rounding; the rest are randomized
    // roundings (ceil with probability equal to the fractional part). Restarts
    // explore round-up/round-down combinations the single rounding misses.
    const int trials = matrix_ok ? std::max(1, trials_) : 1;
    for (int trial = 0; trial < trials; ++trial) {
        std::vector<double> x = lp_solution;
        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            const auto& var = problem.variables[j];
            if (var.type == model::VarType::CONTINUOUS) continue;

            const double val = x[j];
            double rounded;
            if (trial == 0) {
                rounded = std::round(val);
            } else {
                const double floor_v = std::floor(val);
                rounded = (uni(rng) < val - floor_v) ? floor_v + 1.0 : floor_v;
            }
            const double lb = var.lower_bound;
            const double ub = var.upper_bound;
            rounded = std::clamp(rounded, lb, ub);
            if (std::abs(rounded - lb) < tol_.feasibility_tol()) rounded = lb;
            if (std::abs(rounded - ub) < tol_.feasibility_tol()) rounded = ub;
            x[j] = rounded;
        }

        if (matrix_ok) repair(problem, x);

        if (check_feasibility(problem, x)) {
            const double obj = compute_objective(problem, x);
            const bool better =
                !have_best ||
                (problem.obj_sense == model::ObjectiveSense::MAXIMIZE ? obj > best_obj
                                                                      : obj < best_obj);
            if (better) {
                have_best = true;
                best_obj = obj;
                best_x = std::move(x);
            }
        }
    }

    if (have_best) {
        result.found_solution = true;
        result.solution = std::move(best_x);
        result.objective_value = best_obj;
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