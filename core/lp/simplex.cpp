#include "simplex.hpp"
#include "../model/mps_parser.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <numeric>
#include <cmath>
#include <cstdlib>
#include <string>
#include <iostream>

namespace hypernova::lp {

static void extract_column(const numerical::SparseMatrix& A, std::size_t j, std::vector<double>& col) {
    col.assign(A.rows(), 0.0);
    for (std::size_t i = 0; i < A.rows(); ++i) {
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            if (A.col_indices()[k] == j) {
                col[i] = A.values()[k];
                break;
            }
        }
    }
}

static numerical::SparseMatrix build_basis_matrix(const numerical::SparseMatrix& A,
                                                   const std::vector<int>& basis) {
    std::size_t m = basis.size();
    std::vector<numerical::Triplet> triplets;
    for (std::size_t i = 0; i < m; ++i) {
        int var = basis[i];
        if (var < 0) continue;
        std::size_t v = static_cast<std::size_t>(var);
        for (std::size_t k = 0; k < A.rows(); ++k) {
            for (std::size_t kk = A.row_ptr()[k]; kk < A.row_ptr()[k + 1]; ++kk) {
                if (A.col_indices()[kk] == v) {
                    triplets.emplace_back(k, i, A.values()[kk]);
                    break;
                }
            }
        }
    }
    return numerical::SparseMatrix::from_triplets(m, m, triplets);
}

SimplexSolver::SimplexSolver(const numerical::ToleranceConfig& tol, const SimplexOptions& options)
    : tol_(tol), options_(options) {}

SimplexResult SimplexSolver::solve(const model::Problem& problem) {
    std::vector<int> var_map(problem.variables.size(), 0);
    std::vector<double> fixed_vals(problem.variables.size(), 0.0);
    model::Problem reduced = eliminate_fixed_variables(problem, var_map, fixed_vals);
    SimplexResult result = solve_impl(reduced, nullptr);
    bool needs_map = false;
    for (std::size_t j = 0; j < var_map.size(); ++j) {
        if (var_map[j] != static_cast<int>(j)) {
            needs_map = true;
            break;
        }
    }
    if (needs_map) {
        std::vector<double> full_primal(problem.variables.size(), 0.0);
        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            full_primal[j] = fixed_vals[j] +
                             ((var_map[j] >= 0) ? result.primal[static_cast<std::size_t>(var_map[j])] : 0.0);
        }
        result.primal = std::move(full_primal);
        if (!result.reduced_costs.empty()) {
            std::vector<double> full_rc(problem.variables.size(), 0.0);
            for (std::size_t j = 0; j < problem.variables.size(); ++j) {
                if (var_map[j] >= 0) {
                    full_rc[j] = result.reduced_costs[static_cast<std::size_t>(var_map[j])];
                }
            }
            result.reduced_costs = std::move(full_rc);
        }
    }
    return result;
}

SimplexResult SimplexSolver::solve_with_basis(const model::Problem& problem, const std::vector<int>& var_status, const std::vector<int>& constraint_status) {
    std::vector<int> var_map(problem.variables.size(), 0);
    std::vector<double> fixed_vals(problem.variables.size(), 0.0);
    model::Problem reduced = eliminate_fixed_variables(problem, var_map, fixed_vals);

    std::vector<int> reduced_status;
    if (var_status.size() == problem.variables.size()) {
        reduced_status.reserve(reduced.variables.size());
        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            if (var_map[j] >= 0) {
                reduced_status.push_back(var_status[j]);
            }
        }
        if (reduced_status.size() != reduced.variables.size()) {
            reduced_status.clear();
        }
    }
    SimplexResult result = solve_impl(reduced, reduced_status.empty() ? nullptr : &reduced_status, constraint_status.empty() ? nullptr : &constraint_status);
    bool needs_map = false;
    for (std::size_t j = 0; j < var_map.size(); ++j) {
        if (var_map[j] != static_cast<int>(j)) {
            needs_map = true;
            break;
        }
    }
    if (needs_map) {
        std::vector<double> full_primal(problem.variables.size(), 0.0);
        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            full_primal[j] = fixed_vals[j] +
                             ((var_map[j] >= 0) ? result.primal[static_cast<std::size_t>(var_map[j])] : 0.0);
        }
        result.primal = std::move(full_primal);
        if (!result.reduced_costs.empty()) {
            std::vector<double> full_rc(problem.variables.size(), 0.0);
            for (std::size_t j = 0; j < problem.variables.size(); ++j) {
                if (var_map[j] >= 0) {
                    full_rc[j] = result.reduced_costs[static_cast<std::size_t>(var_map[j])];
                }
            }
            result.reduced_costs = std::move(full_rc);
        }
    }
    return result;
}

model::Problem SimplexSolver::eliminate_fixed_variables(const model::Problem& problem,
                                                        std::vector<int>& var_map,
                                                        std::vector<double>& fixed_vals) {
    var_map.assign(problem.variables.size(), 0);
    fixed_vals.assign(problem.variables.size(), 0.0);

    bool has_transform = false;
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        const auto& v = problem.variables[j];
        bool fixed = std::isfinite(v.lower_bound) && std::isfinite(v.upper_bound) &&
                     std::abs(v.upper_bound - v.lower_bound) <= tol_.feasibility_tol() * std::max(1.0, std::abs(v.upper_bound));
        var_map[j] = fixed ? -1 : -2;
        fixed_vals[j] = fixed ? v.lower_bound : 0.0;
        if (fixed || (std::isfinite(v.lower_bound) && v.lower_bound != 0.0)) has_transform = true;
    }

    if (!has_transform) {
        for (std::size_t j = 0; j < problem.variables.size(); ++j) var_map[j] = static_cast<int>(j);
        return problem;
    }

    model::ProblemBuilder builder(problem.name);

    std::vector<std::size_t> new_idx(problem.variables.size(), 0);
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (var_map[j] == -1) continue;
        if (var_map[j] == -2) {
            const auto& v = problem.variables[j];
            double lb = v.lower_bound;
            double ub = v.upper_bound;
            double shift = 0.0;
            if (std::isfinite(v.lower_bound)) {
                shift = v.lower_bound;
                lb = 0.0;
                if (std::isfinite(ub)) {
                    ub = std::max(0.0, ub - shift);
                }
            }
            new_idx[j] = builder.add_variable(lb,
                                              ub,
                                              v.type,
                                              v.name);
            var_map[j] = static_cast<int>(new_idx[j]);
            fixed_vals[j] = shift;
        }
    }

    double offset_adjust = problem.obj_offset;
    const auto& A = problem.constraint_matrix;
    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        double rhs = problem.constraints[i].rhs;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                double val = A.values()[k];
                if (var_map[j] >= 0) {
                    coeffs.push_back({static_cast<std::size_t>(var_map[j]), val});
                }
                rhs -= val * fixed_vals[j];
            }
        }
        builder.add_constraint(coeffs, problem.constraints[i].sense, rhs, problem.constraints[i].name);
    }

    std::vector<std::pair<std::size_t, double>> obj_coeffs;
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        double c = problem.variables[j].objective_coeff;
        if (c == 0.0) continue;
        if (var_map[j] >= 0) {
            obj_coeffs.push_back({static_cast<std::size_t>(var_map[j]), c});
        }
        offset_adjust += c * fixed_vals[j];
    }
    builder.set_objective(obj_coeffs, problem.obj_sense);
    builder.get_problem().obj_offset = offset_adjust;

    return builder.build();
}

SimplexResult SimplexSolver::solve_impl(const model::Problem& problem, const std::vector<int>* warm_var_status, const std::vector<int>* warm_con_status) {
    auto start_time = std::chrono::high_resolution_clock::now();
    solve_start_time_ = start_time;

    n_original_vars_ = problem.variables.size();
    std::size_t norig_cons = problem.constraints.size();
    n_original_rows_ = norig_cons;

    // ---- Two-phase standard-form construction ------------------------------
    // Each row becomes an equality with a non-negative rhs and an identity
    // variable (basic at its rhs), producing a feasible phase-1 basis:
    //   LE, rhs >= 0  ->  a*x + s     = rhs   (s slack, no artificial)
    //   LE, rhs <  0  ->  a*x - e + t = -rhs  (e surplus, t artificial)
    //   EQ, rhs >= 0  ->  a*x + t     = rhs   (t artificial)
    //   EQ, rhs <  0  ->  a*x + t     = -rhs  (t artificial)
    // GE rows are negated first. Phase 1 drives every artificial to zero;
    // if that cannot be done, the problem is infeasible.

    std::vector<numerical::Triplet> triplets;
    std::vector<double> row_rhs(norig_cons, 0.0);
    std::vector<double> row_scale(norig_cons, 1.0);
    std::vector<char> row_needs_art(norig_cons, 0);
    std::vector<char> row_is_eq(norig_cons, 0);
    std::vector<char> row_slack_sign(norig_cons, 1);

    const bool dual_expand = (options_.algorithm == SimplexAlgorithm::DUAL);
    bool dual_eligible = true;
    if (dual_expand) {
        for (std::size_t i = 0; i < norig_cons; ++i) {
            if (problem.constraints[i].sense == model::ConstraintSense::EQ) {
                dual_eligible = false;
                break;
            }
        }
    }

    for (std::size_t i = 0; i < norig_cons; ++i) {
        double rhs = problem.constraints[i].rhs;
        double scale = 1.0;
        model::ConstraintSense sense = problem.constraints[i].sense;

        if (dual_eligible && dual_expand) {
            // Dual-simplex expansion uses only +/-1 slack/surplus identity
            // columns (no artificials), so the initial basis can be dual
            // feasible.
            const bool orig_nonneg = (rhs >= 0.0);
            if (!orig_nonneg) {
                scale = -1.0;
                rhs = -rhs;
            }
            const bool le_like = ((sense == model::ConstraintSense::LE) == orig_nonneg);
            row_slack_sign[i] = le_like ? 1 : -1;
            row_rhs[i] = rhs;
            row_scale[i] = scale;
            continue;
        }
        if (sense == model::ConstraintSense::GE) {
            scale = -1.0;
            rhs = -rhs;
            sense = model::ConstraintSense::LE;
        }
        if (sense == model::ConstraintSense::EQ) {
            if (rhs < 0.0) {
                scale = -scale;
                rhs = -rhs;
            }
            row_is_eq[i] = 1;
            row_needs_art[i] = 1;
        } else {
            if (rhs < 0.0) {
                scale = -scale;
                rhs = -rhs;
                row_needs_art[i] = 1;
            }
        }
        row_rhs[i] = rhs;
        row_scale[i] = scale;
    }

    expanded_problem_ = problem;
    expanded_problem_.variables = problem.variables;
    expanded_problem_.constraints = problem.constraints;

    std::vector<int> ub_row_var;
    for (std::size_t j = 0; j < n_original_vars_; ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        if (std::isfinite(lb) && std::isfinite(ub) && ub >= 0.0) {
            model::Constraint bc;
            bc.index = expanded_problem_.constraints.size();
            bc.sense = model::ConstraintSense::LE;
            bc.rhs = ub;
            bc.name = "_ub_" + problem.variables[j].name;
            expanded_problem_.constraints.push_back(bc);
            ub_row_var.push_back(static_cast<int>(j));
        }
    }

    // Transform variables with an infinite lower bound so the non-negative
    // standard form does not silently clamp them to x >= 0:
    //   fully free -> x = xp - xn ; upper-only -> x = ub - xc.
    free_split_p_.assign(n_original_vars_, -1);
    free_split_n_.assign(n_original_vars_, -1);
    complement_var_.assign(n_original_vars_, -1);
    bool has_infinite_lb = false;
    for (std::size_t j = 0; j < n_original_vars_; ++j) {
        const auto& v = problem.variables[j];
        if (!std::isfinite(v.lower_bound)) {
            has_infinite_lb = true;
            if (!std::isfinite(v.upper_bound)) {
                model::Variable xp;
                xp.index = expanded_problem_.variables.size();
                xp.lower_bound = 0.0;
                xp.upper_bound = std::numeric_limits<double>::infinity();
                xp.type = model::VarType::CONTINUOUS;
                xp.name = "_fp" + std::to_string(j);
                xp.objective_coeff = v.objective_coeff;
                expanded_problem_.variables.push_back(xp);
                free_split_p_[j] = static_cast<int>(xp.index);

                model::Variable xn;
                xn.index = xp.index + 1;
                xn.lower_bound = 0.0;
                xn.upper_bound = std::numeric_limits<double>::infinity();
                xn.type = model::VarType::CONTINUOUS;
                xn.name = "_fn" + std::to_string(j);
                xn.objective_coeff = -v.objective_coeff;
                expanded_problem_.variables.push_back(xn);
                free_split_n_[j] = static_cast<int>(xn.index);
            } else {
                model::Variable xc;
                xc.index = expanded_problem_.variables.size();
                xc.lower_bound = 0.0;
                xc.upper_bound = std::numeric_limits<double>::infinity();
                xc.type = model::VarType::CONTINUOUS;
                xc.name = "_fc" + std::to_string(j);
                xc.objective_coeff = -v.objective_coeff;
                expanded_problem_.variables.push_back(xc);
                expanded_problem_.obj_offset += v.objective_coeff * v.upper_bound;
                complement_var_[j] = static_cast<int>(xc.index);
            }
        }
    }
    if (has_infinite_lb) dual_eligible = false;

    std::size_t ncons = expanded_problem_.constraints.size();
    n_slack_vars_ = 0;
    row_basis_var_.assign(ncons, -1);
    artificial_cols_.assign(expanded_problem_.variables.size(), 0);

    for (std::size_t i = 0; i < ncons; ++i) {
        bool need_art;

        if (i < norig_cons) {
            need_art = (row_needs_art[i] != 0);
            double scale = row_scale[i];
            for (std::size_t k = problem.constraint_matrix.row_ptr()[i];
                 k < problem.constraint_matrix.row_ptr()[i + 1]; ++k) {
                std::size_t j = problem.constraint_matrix.col_indices()[k];
                double val = scale * problem.constraint_matrix.values()[k];
                if (val == 0.0) continue;
                if (free_split_p_[j] != -1) {
                    triplets.emplace_back(i, static_cast<std::size_t>(free_split_p_[j]), val);
                    triplets.emplace_back(i, static_cast<std::size_t>(free_split_n_[j]), -val);
                } else if (complement_var_[j] != -1) {
                    triplets.emplace_back(i, static_cast<std::size_t>(complement_var_[j]), -val);
                    row_rhs[i] -= val * problem.variables[j].upper_bound;
                } else {
                    triplets.emplace_back(i, j, val);
                }
            }
            expanded_problem_.constraints[i].rhs = row_rhs[i];
        } else {
            need_art = false;
            triplets.emplace_back(i, static_cast<std::size_t>(ub_row_var[i - norig_cons]), 1.0);
        }

        if (!need_art) {
            model::Variable slack;
            slack.index = expanded_problem_.variables.size();
            slack.lower_bound = 0.0;
            slack.upper_bound = std::numeric_limits<double>::infinity();
            slack.type = model::VarType::CONTINUOUS;
            slack.name = "_s" + std::to_string(n_slack_vars_);
            slack.objective_coeff = 0.0;
            expanded_problem_.variables.push_back(slack);
            const double slack_coeff = (i < norig_cons) ? static_cast<double>(row_slack_sign[i]) : 1.0;
            triplets.emplace_back(i, slack.index, slack_coeff);
            row_basis_var_[i] = static_cast<int>(slack.index);
            artificial_cols_.push_back(0);
            ++n_slack_vars_;
        } else if (row_is_eq[i]) {
            model::Variable art;
            art.index = expanded_problem_.variables.size();
            art.lower_bound = 0.0;
            art.upper_bound = std::numeric_limits<double>::infinity();
            art.type = model::VarType::CONTINUOUS;
            art.name = "_ae" + std::to_string(i);
            art.objective_coeff = 0.0;
            expanded_problem_.variables.push_back(art);
            triplets.emplace_back(i, art.index, 1.0);
            row_basis_var_[i] = static_cast<int>(art.index);
            artificial_cols_.push_back(1);
        } else {
            model::Variable surplus;
            surplus.index = expanded_problem_.variables.size();
            surplus.lower_bound = 0.0;
            surplus.upper_bound = std::numeric_limits<double>::infinity();
            surplus.type = model::VarType::CONTINUOUS;
            surplus.name = "_ep" + std::to_string(i);
            surplus.objective_coeff = 0.0;
            expanded_problem_.variables.push_back(surplus);
            triplets.emplace_back(i, surplus.index, -1.0);
            artificial_cols_.push_back(0);

            model::Variable art;
            art.index = expanded_problem_.variables.size();
            art.lower_bound = 0.0;
            art.upper_bound = std::numeric_limits<double>::infinity();
            art.type = model::VarType::CONTINUOUS;
            art.name = "_ae" + std::to_string(i);
            art.objective_coeff = 0.0;
            expanded_problem_.variables.push_back(art);
            triplets.emplace_back(i, art.index, 1.0);
            row_basis_var_[i] = static_cast<int>(art.index);
            artificial_cols_.push_back(1);
        }
    }

    expanded_problem_.constraint_matrix = numerical::SparseMatrix::from_triplets(
        ncons, expanded_problem_.variables.size(), triplets);

    for (std::size_t i = 0; i < ncons; ++i) {
        expanded_problem_.constraints[i].sense = model::ConstraintSense::EQ;
    }

    problem_ = &expanded_problem_;

    SimplexResult result;
    std::size_t nvars = problem_->variables.size();

    if (nvars == 0 || n_original_rows_ == 0) {
        if (n_original_rows_ == 0 && nvars > 0) {
            result.primal.resize(n_original_vars_, 0.0);
            bool unbounded = false;
            for (std::size_t j = 0; j < n_original_vars_; ++j) {
                double coeff = problem.variables[j].objective_coeff;
                if (coeff == 0.0) continue;
                bool improving_positive = (problem.obj_sense == model::ObjectiveSense::MAXIMIZE && coeff > 0.0) ||
                                          (problem.obj_sense == model::ObjectiveSense::MINIMIZE && coeff < 0.0);
                double lb = problem.variables[j].lower_bound;
                double ub = problem.variables[j].upper_bound;
                if (improving_positive) {
                    if (std::isinf(ub)) { unbounded = true; break; }
                    result.primal[j] = ub;
                } else {
                    if (std::isinf(lb)) { unbounded = true; break; }
                    result.primal[j] = lb;
                }
            }
            if (unbounded) {
                result.status = model::ProblemStatus::UNBOUNDED;
            } else {
                result.status = model::ProblemStatus::OPTIMAL;
                result.objective_value = problem.obj_offset;
                for (std::size_t j = 0; j < n_original_vars_; ++j) {
                    result.objective_value += problem.variables[j].objective_coeff * result.primal[j];
                }
            }
        } else {
            result.status = model::ProblemStatus::OPTIMAL;
            result.objective_value = problem_->obj_offset;
            result.primal.resize(n_original_vars_, 0.0);
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return result;
    }

try {
    result.algorithm_used = SimplexAlgorithm::PRIMAL;
    recovery_done_ = false;
    if (dual_eligible && options_.algorithm == SimplexAlgorithm::DUAL && !warm_var_status && prepare_dual_start()) {
        result.algorithm_used = SimplexAlgorithm::DUAL;

        initialize_basis(*problem_);
        rhs_values_.resize(problem_->constraints.size());
        for (std::size_t i = 0; i < problem_->constraints.size(); ++i) {
            rhs_values_[i] = problem_->constraints[i].rhs;
        }

        phase_cost_.assign(nvars, 0.0);
        for (std::size_t j = 0; j < nvars; ++j) {
            phase_cost_[j] = problem_->variables[j].objective_coeff;
        }
        phase1_active_ = false;
        maximize_ = false;
        perturb_level_ = 0;
        perturbed_ = false;

        result.status = run_dual_loop();
        result.iterations = dual_iterations_;

        if (result.status == model::ProblemStatus::ITER_LIMIT &&
            dual_iterations_ < static_cast<std::size_t>(options_.max_iterations)) {
            result.status = model::ProblemStatus::NUMERICAL_ERROR;
        }

        // Map expanded-form basic values back to the original variables,
        // un-complementing any negated column.
        result.primal.resize(n_original_vars_, 0.0);
        std::vector<double> dual_full_primal(nvars, 0.0);
        for (std::size_t i = 0; i < ncons; ++i) {
            if (basis_[i] >= 0) dual_full_primal[static_cast<std::size_t>(basis_[i])] = basic_solution_[i];
        }

        for (std::size_t j = 0; j < n_original_vars_; ++j) {
            if (complemented_[j]) {
                result.primal[j] = problem_->variables[j].upper_bound - dual_full_primal[j];
            } else {
                result.primal[j] = dual_full_primal[j];
            }
        }


        if (result.status == model::ProblemStatus::OPTIMAL) {
            const auto& A = problem.constraint_matrix;
            bool feasible = true;
            for (std::size_t i = 0; i < problem.constraints.size() && feasible; ++i) {
                double activity = 0.0;
                for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                    std::size_t j = A.col_indices()[k];
                    if (j < n_original_vars_) activity += A.values()[k] * result.primal[j];
                }
                double rhs = problem.constraints[i].rhs;
                if (problem.constraints[i].sense == model::ConstraintSense::LE) {
                    if (activity > rhs + tol_.feasibility_tol()) feasible = false;
                } else if (problem.constraints[i].sense == model::ConstraintSense::GE) {
                    if (activity < rhs - tol_.feasibility_tol()) feasible = false;
                } else {
                    if (std::abs(activity - rhs) > tol_.feasibility_tol()) feasible = false;
                }
            }
            for (std::size_t j = 0; j < n_original_vars_ && feasible; ++j) {
                double val = result.primal[j];
                double lb = problem.variables[j].lower_bound;
                double ub = problem.variables[j].upper_bound;
                if (val < lb - tol_.feasibility_tol() || val > ub + tol_.feasibility_tol()) {
                    feasible = false;
                }
            }
            if (!feasible) result.status = model::ProblemStatus::INFEASIBLE;
        }


        result.dual.resize(norig_cons, 0.0);
        for (std::size_t i = 0; i < norig_cons; ++i) {
            result.dual[i] = row_scale[i] * dual_solution_[i];
        }
        if (was_maximizing_) {
            for (double& v : result.dual) v = -v;
        }


        result.reduced_costs.resize(n_original_vars_, 0.0);
        for (std::size_t j = 0; j < n_original_vars_; ++j) {
            double rc = reduced_costs_[j];
            if (complemented_[j]) rc = -rc;
            result.reduced_costs[j] = was_maximizing_ ? -rc : rc;
        }


        result.basis_status.resize(nvars, 0);
        for (std::size_t i = 0; i < basis_.size(); ++i) {
            if (basis_[i] >= 0 && static_cast<std::size_t>(basis_[i]) < nvars) {
                result.basis_status[static_cast<std::size_t>(basis_[i])] = 1;
            }
        }


        result.objective_value = problem.obj_offset;
        for (std::size_t j = 0; j < n_original_vars_; ++j) {
            result.objective_value += problem.variables[j].objective_coeff * result.primal[j];
        }

        auto dual_end_time = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(dual_end_time - start_time).count();

        return result;
    }

    if (warm_var_status && warm_var_status->size() == n_original_vars_) {
        initialize_basis_with_warm_start(*problem_, *warm_var_status, warm_con_status);
    } else {
        initialize_basis(*problem_);
    }
    rhs_values_.resize(problem_->constraints.size());
    for (std::size_t i = 0; i < problem_->constraints.size(); ++i) {
        rhs_values_[i] = problem_->constraints[i].rhs;
    }

    auto set_phase_costs = [&]() {
        phase_cost_.assign(nvars, 0.0);
        if (phase1_active_) {
            for (std::size_t j = 0; j < nvars; ++j) {
                phase_cost_[j] = artificial_cols_[j] ? 1.0 : 0.0;
            }
        } else {
            for (std::size_t j = 0; j < nvars; ++j) {
                phase_cost_[j] = problem_->variables[j].objective_coeff;
            }
        }
    };

    auto compute_obj = [&]() {
        double obj = 0.0;
        for (std::size_t i = 0; i < ncons; ++i) {
            int var = basis_[i];
            if (var >= 0) {
                obj += phase_cost_[static_cast<std::size_t>(var)] * basic_solution_[i];
            }
        }
        obj += problem_->obj_offset;
        if (maximize_) obj = -obj;
        return obj;
    };

    std::size_t total_iters = 0;
    const bool need_phase1 = !artificial_cols_.empty();

    phase1_active_ = need_phase1;
    maximize_ = false;
    perturb_level_ = 0;
    set_phase_costs();

    auto run_phase_loop = [&]() {
        double prev_obj = compute_obj();
        int stall = 0;
        int nan_streak = 0;
        const bool prof = std::getenv("HYPERNOVA_SIMPLEX_PROF") != nullptr;
        double t_dual = 0.0, t_rc = 0.0, t_enter = 0.0, t_leave = 0.0, t_pivot = 0.0, t_update = 0.0;
        auto now_ = []() { return std::chrono::high_resolution_clock::now(); };
        for (std::size_t iter = 0; iter < static_cast<std::size_t>(options_.max_iterations); ++iter) {
            if (options_.interrupt_callback && options_.interrupt_callback())
                return model::ProblemStatus::INTERRUPTED;
            if (options_.time_limit_seconds > 0.0) {
                auto now = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                if (elapsed >= options_.time_limit_seconds) {
                    if (prof) {
                        std::cerr << "[simplex-prof] iters=" << iter << " dual=" << t_dual
                                  << " rc=" << t_rc << " enter=" << t_enter << " leave=" << t_leave
                                  << " pivot=" << t_pivot << " update=" << t_update << "\n";
                    }
                    return model::ProblemStatus::TIME_LIMIT;
                }
            }
            auto t0 = now_();
            compute_dual_solution();
            t_dual += std::chrono::duration<double>(now_() - t0).count();
            t0 = now_();
            compute_reduced_costs();
            t_rc += std::chrono::duration<double>(now_() - t0).count();

            if (check_optimality()) {
                if (perturbed_) remove_perturbation();
                if (prof) {
                    std::cerr << "[simplex-prof] iters=" << iter << " dual=" << t_dual
                              << " rc=" << t_rc << " enter=" << t_enter << " leave=" << t_leave
                              << " pivot=" << t_pivot << " update=" << t_update << "\n";
                }
                return model::ProblemStatus::OPTIMAL;
            }
            t0 = now_();
            int entering = select_entering_variable();
            t_enter += std::chrono::duration<double>(now_() - t0).count();
            if (entering < 0) {
                if (perturbed_) remove_perturbation();
                if (prof) {
                    std::cerr << "[simplex-prof] iters=" << iter << " dual=" << t_dual
                              << " rc=" << t_rc << " enter=" << t_enter << " leave=" << t_leave
                              << " pivot=" << t_pivot << " update=" << t_update << "\n";
                }
                return model::ProblemStatus::OPTIMAL;
            }
            t0 = now_();
            int leaving = select_leaving_variable(entering);
            t_leave += std::chrono::duration<double>(now_() - t0).count();
            if (leaving < 0) {
                if (!phase1_active_ && check_unbounded(entering))
                    return model::ProblemStatus::UNBOUNDED;
                if (perturbed_) remove_perturbation();
                if (perturb_level_ >= 16) break;
                apply_perturbation();
                prev_obj = compute_obj();
                stall = 0;
                continue;
            }

            t0 = now_();
            pivot(entering, leaving);
            t_pivot += std::chrono::duration<double>(now_() - t0).count();
            t0 = now_();
            update_basis_factorization(leaving, entering);
            t_update += std::chrono::duration<double>(now_() - t0).count();
            
            bool num_bad = false;
            for (double val : basic_solution_) {
                if (std::isnan(val) || std::isinf(val)) {
                    num_bad = true;
                    break;
                }
            }
            if (num_bad) {
                ++nan_streak;
                update_basis_factorization(-1, -1);
                if (nan_streak >= 16) {
                    return model::ProblemStatus::NUMERICAL_ERROR;
                }
            } else {
                nan_streak = 0;
            }

            total_iters += 1;

            const double this_obj = compute_obj();
            const double obj_scale = std::max(1.0, std::abs(prev_obj));
            if (prev_obj - this_obj > 1e-10 * obj_scale) {
                stall = 0;
                prev_obj = this_obj;
            } else {
                ++stall;
                if (stall >= options_.perturbation_iterations && !perturbed_) {
                    apply_perturbation();
                    prev_obj = compute_obj();
                    stall = 0;
                }
            }
        }
        if (prof) {
            std::cerr << "[simplex-prof] iters=" << static_cast<std::size_t>(options_.max_iterations)
                      << " dual=" << t_dual << " rc=" << t_rc << " enter=" << t_enter
                      << " leave=" << t_leave << " pivot=" << t_pivot << " update=" << t_update << "\n";
        }
        return model::ProblemStatus::ITER_LIMIT;
    };

    if (need_phase1) {
        const model::ProblemStatus p1_status = run_phase_loop();

        double art_sum = 0.0;
        for (std::size_t i = 0; i < ncons; ++i) {
            int var = basis_[i];
            if (var >= 0 && artificial_cols_[static_cast<std::size_t>(var)]) {
                art_sum += basic_solution_[i];
            }
        }

        if (std::getenv("HYPERNOVA_SIMPLEX_PROF")) {
            std::size_t n_basic_art = 0;
            double max_basic_art = 0.0;
            for (std::size_t i = 0; i < ncons; ++i) {
                int var = basis_[i];
                if (var >= 0 && artificial_cols_[static_cast<std::size_t>(var)]) {
                    ++n_basic_art;
                    max_basic_art = std::max(max_basic_art, basic_solution_[i]);
                }
            }
            std::cerr << "[simplex-p1] status=" << static_cast<int>(p1_status)
                      << " art_sum=" << art_sum << " basic_art=" << n_basic_art
                      << " max_basic_art=" << max_basic_art
                      << " iters=" << total_iters << "\n";
        }

        if (art_sum > std::max(tol_.feasibility_tol(), 1e-9)) {
            if (p1_status == model::ProblemStatus::OPTIMAL) {
                result.status = model::ProblemStatus::INFEASIBLE;
            } else if (p1_status == model::ProblemStatus::TIME_LIMIT) {
                result.status = model::ProblemStatus::TIME_LIMIT;
            } else if (p1_status == model::ProblemStatus::INTERRUPTED) {
                result.status = model::ProblemStatus::INTERRUPTED;
            } else {
                result.status = model::ProblemStatus::ITER_LIMIT;
            }
            result.primal.resize(n_original_vars_, 0.0);
            std::vector<double> full_primal(nvars, 0.0);
            for (std::size_t i = 0; i < ncons; ++i) {
                if (basis_[i] >= 0) full_primal[static_cast<std::size_t>(basis_[i])] = basic_solution_[i];
            }
            for (std::size_t j = 0; j < n_original_vars_; ++j) {
                if (free_split_p_[j] != -1) {
                    result.primal[j] = full_primal[static_cast<std::size_t>(free_split_p_[j])] -
                                       full_primal[static_cast<std::size_t>(free_split_n_[j])];
                } else if (complement_var_[j] != -1) {
                    result.primal[j] = problem.variables[j].upper_bound -
                                       full_primal[static_cast<std::size_t>(complement_var_[j])];
                } else {
                    result.primal[j] = full_primal[j];
                }
            }
            for (std::size_t j = 0; j < n_original_vars_; ++j) {
                result.objective_value += problem.variables[j].objective_coeff * result.primal[j];
            }
            result.objective_value += problem.obj_offset;
            result.iterations = total_iters;
            auto end_time = std::chrono::high_resolution_clock::now();
            result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return result;
        }
        if (p1_status == model::ProblemStatus::ITER_LIMIT ||
            p1_status == model::ProblemStatus::TIME_LIMIT ||
            p1_status == model::ProblemStatus::INTERRUPTED) {
            result.status = p1_status;
            result.primal.resize(n_original_vars_, 0.0);
            result.iterations = total_iters;
            auto end_time = std::chrono::high_resolution_clock::now();
            result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return result;
        }
    }

    // Phase 2: original objective. Artificial columns are excluded from entry.
    // Artificial variables still basic at zero after phase 1 must be pivoted out
    // of the basis: otherwise a phase-2 pivot can drive them positive, which
    // violates the equality they were introduced for (false "optimal" point that
    // undercuts the true optimum). Redundant rows (no valid pivot column) are
    // handled by the blocking ratio test below.
    if (need_phase1) {
        std::size_t purged = 0;
        std::size_t redundant_rows = 0;
        std::vector<double> col;
        for (std::size_t i = 0; i < ncons; ++i) {
            const int bv = basis_[i];
            if (bv < 0 || !artificial_cols_[static_cast<std::size_t>(bv)]) continue;
            int chosen = -1;
            std::vector<double> chosen_dir;
            for (std::size_t j = 0; j < nvars; ++j) {
                if (nonbasis_[j] < 0) continue;
                if (artificial_cols_[j]) continue;
                col.clear();
                extract_column(problem_->constraint_matrix, j, col);
                basis_factorization_->solve(col);
                if (std::abs(col[i]) > 1e-7) {
                    chosen = static_cast<int>(j);
                    chosen_dir = std::move(col);
                    break;
                }
            }
            if (chosen >= 0) {
                pivot_direction_ = std::move(chosen_dir);
                last_leaving_ = static_cast<int>(i);
                last_entering_ = chosen;
                pivot(chosen, static_cast<int>(i));
                update_basis_factorization(static_cast<int>(i), chosen);
                ++purged;
            } else {
                ++redundant_rows;
            }
        }
        if (std::getenv("HYPERNOVA_SIMPLEX_PROF")) {
            std::cerr << "[simplex-purge] purged=" << purged
                      << " redundant=" << redundant_rows << "\n";
        }
    }

    phase1_active_ = false;
    maximize_ = (problem.obj_sense == model::ObjectiveSense::MAXIMIZE);
    set_phase_costs();
    if (perturbed_) remove_perturbation();

    result.status = run_phase_loop();
    result.iterations = total_iters;

    if (result.status == model::ProblemStatus::OPTIMAL) {
        // Numerical cleanup: the terminal basis may sit at the end of a long
        // eta chain whose accumulated round-off inflates the primal/dual
        // residuals. Rebuild the factorization from scratch and re-derive the
        // solution before validating, and continue the simplex if the fresh
        // (more accurate) reduced costs reveal the basis was not yet optimal.
        iters_since_refactor_ = refactor_frequency_;
        last_leaving_ = -1;
        last_entering_ = -1;
        update_basis_factorization(-1, -1);
        compute_dual_solution();
        compute_reduced_costs();
        if (!check_optimality()) {
            result.status = run_phase_loop();
            result.iterations = total_iters;
        }
    }

    if (result.status == model::ProblemStatus::ITER_LIMIT &&
        result.iterations < static_cast<std::size_t>(options_.max_iterations)) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
    }

    auto finalize_and_check = [&]() -> bool {
        result.primal.resize(n_original_vars_, 0.0);
        std::vector<double> full_primal(nvars, 0.0);
        for (std::size_t i = 0; i < ncons; ++i) {
            if (basis_[i] >= 0) {
                full_primal[static_cast<std::size_t>(basis_[i])] = basic_solution_[i];
            }
        }
        for (std::size_t j = 0; j < n_original_vars_; ++j) {
            if (free_split_p_[j] != -1) {
                result.primal[j] = full_primal[static_cast<std::size_t>(free_split_p_[j])] -
                                   full_primal[static_cast<std::size_t>(free_split_n_[j])];
            } else if (complement_var_[j] != -1) {
                result.primal[j] = problem.variables[j].upper_bound -
                                   full_primal[static_cast<std::size_t>(complement_var_[j])];
            } else {
                result.primal[j] = full_primal[j];
            }
        }

        bool primal_feasible = true;
        if (result.status == model::ProblemStatus::OPTIMAL) {
            const auto& A = problem.constraint_matrix;
            bool feasible = true;
            double max_viol = 0.0;
            std::size_t viol_row = 0;
            std::size_t ncheck = problem.constraints.size();
            double global_scale = 1.0;
            for (std::size_t i = 0; i < ncheck; ++i) {
                double activity = 0.0;
                double rhs = problem.constraints[i].rhs;
                double row_ref = 1.0 + std::abs(rhs);
                if (A.order() == numerical::StorageOrder::CSR) {
                    for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                        std::size_t j = A.col_indices()[k];
                        if (j < n_original_vars_) {
                            activity += A.values()[k] * result.primal[j];
                            row_ref += std::abs(A.values()[k]) *
                                       std::max(1.0, std::abs(result.primal[j]));
                        }
                    }
                }
                double vio = 0.0;
                if (problem.constraints[i].sense == model::ConstraintSense::LE) {
                    vio = activity - rhs;
                } else if (problem.constraints[i].sense == model::ConstraintSense::GE) {
                    vio = rhs - activity;
                } else {
                    vio = std::abs(activity - rhs);
                }
                if (vio > tol_.feasibility_tol() * row_ref) feasible = false;
                if (vio > max_viol) { max_viol = vio; viol_row = i; }
                global_scale = std::max(global_scale, row_ref);
                if (std::getenv("HYPERNOVA_SIMPLEX_PROF") && vio > 1e-3) {
                    std::cerr << "[simplex-viol] row=" << i << " sense=" << static_cast<int>(problem.constraints[i].sense)
                              << " rhs=" << rhs << " activity=" << activity << " vio=" << vio
                              << " nnz=" << (A.row_ptr()[i + 1] - A.row_ptr()[i]) << "\n";
                }
            }
            for (std::size_t j = 0; j < n_original_vars_; ++j) {
                double val = result.primal[j];
                double lb = problem.variables[j].lower_bound;
                double ub = problem.variables[j].upper_bound;
                double vio = std::max(lb - val, val - ub);
                if (vio > tol_.feasibility_tol() * global_scale) feasible = false;
                if (vio > max_viol) { max_viol = vio; viol_row = ncheck + j; }
                if (std::getenv("HYPERNOVA_SIMPLEX_PROF") && vio > 1e-3) {
                    std::cerr << "[simplex-viol-var] j=" << j
                              << " name=" << problem.variables[j].name
                              << " val=" << val << " lb=" << lb << " ub=" << ub
                              << " vio=" << vio << "\n";
                }
            }
            if (std::getenv("HYPERNOVA_SIMPLEX_PROF")) {
                std::cerr << "[simplex-p2] status=" << static_cast<int>(result.status)
                          << " iters=" << total_iters << " feasible=" << feasible
                          << " max_viol=" << max_viol << " row=" << viol_row
                          << " nphase2iter=" << (total_iters) << "\n";
            }
            primal_feasible = feasible;
        }
        return primal_feasible;
    };

    bool primal_feasible = finalize_and_check();
    if (result.status == model::ProblemStatus::OPTIMAL && !primal_feasible &&
        !recovery_done_ && (options_.bland_rule || perturbed_)) {
        // Bounded recovery: Bland's lowest-index scan (or a lingering
        // perturbation) can drive the workload into a numerically degraded
        // basis that terminates "optimal" while the independent feasibility
        // re-check above still flags a violated row/variable. Re-drop to the
        // DEVEX priced entering rule and re-run the phase loop once from the
        // current basis before committing to a conclusion.
        recovery_done_ = true;
        options_.bland_rule = false;
        if (perturbed_) remove_perturbation();
        perturb_level_ = 0;
        result.status = run_phase_loop();
        result.iterations = total_iters;
        primal_feasible = finalize_and_check();
    }
    if (result.status == model::ProblemStatus::OPTIMAL && !primal_feasible) {
        result.status = model::ProblemStatus::INFEASIBLE;
    }

    result.dual.resize(norig_cons, 0.0);
    for (std::size_t i = 0; i < norig_cons; ++i) {
        result.dual[i] = row_scale[i] * dual_solution_[i];
    }

    result.reduced_costs.resize(n_original_vars_, 0.0);
    for (std::size_t j = 0; j < n_original_vars_; ++j) {
        if (free_split_p_[j] != -1) {
            result.reduced_costs[j] = reduced_costs_[static_cast<std::size_t>(free_split_p_[j])];
        } else if (complement_var_[j] != -1) {
            result.reduced_costs[j] = -reduced_costs_[static_cast<std::size_t>(complement_var_[j])];
        } else {
            result.reduced_costs[j] = reduced_costs_[j];
        }
    }

    result.basis_status.resize(nvars, 0);
    for (std::size_t i = 0; i < basis_.size(); ++i) {
        if (basis_[i] >= 0 && static_cast<std::size_t>(basis_[i]) < nvars) {
            result.basis_status[static_cast<std::size_t>(basis_[i])] = 1;
        }
    }

    result.objective_value = 0.0;
    for (std::size_t j = 0; j < n_original_vars_; ++j) {
        result.objective_value += problem.variables[j].objective_coeff * result.primal[j];
    }
    result.objective_value += problem.obj_offset;

    auto end_time = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

} catch (const std::exception& e) {
        if (std::getenv("HYPERNOVA_SIMPLEX_PROF")) {
            std::cerr << "[simplex-exception] " << e.what() << "\n";
        }
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
    }

    return result;
}

void SimplexSolver::initialize_basis(const model::Problem& problem) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    basis_.assign(ncons, -1);
    nonbasis_.assign(nvars, -1);
    basic_solution_.assign(ncons, 0.0);
    dual_solution_.assign(ncons, 0.0);
    reduced_costs_.assign(nvars, 0.0);

    for (std::size_t i = 0; i < ncons; ++i) {
        int v = row_basis_var_[i];
        if (v >= 0 && static_cast<std::size_t>(v) < nvars) {
            basis_[i] = v;
        }
    }

    for (std::size_t j = 0; j < nvars; ++j) {
        bool is_basic = false;
        for (std::size_t i = 0; i < ncons; ++i) {
            if (basis_[i] == static_cast<int>(j)) {
                is_basic = true;
                break;
            }
        }
        if (!is_basic) {
            nonbasis_[j] = 0;
        }
    }

    const auto& A = problem.constraint_matrix;
    numerical::SparseMatrix B = build_basis_matrix(A, basis_);

    if (B.nnz() > 0) {
        basis_factorization_ = numerical::create_factorization(
            numerical::FactorizationType::LU,
            B,
            tol_
        );
    }

    if (basis_factorization_ && ncons > 0) {
        std::vector<double> b(ncons);
        for (std::size_t i = 0; i < ncons; ++i) {
            b[i] = problem.constraints[i].rhs;
        }
        basis_factorization_->solve(b);
        basic_solution_ = b;
    }
}

void SimplexSolver::initialize_basis_with_warm_start(const model::Problem& problem,
                                                      const std::vector<int>& var_status,
                                                      const std::vector<int>* con_status) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    basis_.assign(ncons, -1);
    nonbasis_.assign(nvars, -1);
    basic_solution_.assign(ncons, 0.0);
    dual_solution_.assign(ncons, 0.0);
    reduced_costs_.assign(nvars, 0.0);

    std::vector<char> used(nvars, 0);

    if (con_status && con_status->size() == ncons) {
        for (std::size_t i = 0; i < ncons; ++i) {
            if ((*con_status)[i] == 1) { // 1 means basic
                int v = row_basis_var_[i];
                if (v >= 0 && static_cast<std::size_t>(v) < nvars) {
                    basis_[i] = v;
                    used[static_cast<std::size_t>(v)] = 1;
                }
            }
        }
    }

    std::vector<std::size_t> basic_candidates;
    for (std::size_t j = 0; j < var_status.size(); ++j) {
        if (var_status[j] == 1) {
            basic_candidates.push_back(j);
        } else if (var_status[j] == 0 && con_status == nullptr) {
            basic_candidates.push_back(j);
        }
    }

    for (std::size_t i = 0, cand_idx = 0; i < ncons && cand_idx < basic_candidates.size(); ++i) {
        if (basis_[i] < 0) {
            std::size_t v = basic_candidates[cand_idx++];
            if (!used[v]) {
                basis_[i] = static_cast<int>(v);
                used[v] = 1;
            }
        }
    }

    for (std::size_t i = 0; i < ncons; ++i) {
        if (basis_[i] >= 0) continue;
        int v = row_basis_var_[i];
        if (v >= 0 && static_cast<std::size_t>(v) < nvars && !used[static_cast<std::size_t>(v)]) {
            basis_[i] = v;
            used[static_cast<std::size_t>(v)] = 1;
        }
    }

    for (std::size_t j = 0; j < nvars; ++j) {
        bool is_basic = false;
        for (std::size_t i = 0; i < ncons; ++i) {
            if (basis_[i] == static_cast<int>(j)) {
                is_basic = true;
                break;
            }
        }
        if (!is_basic) {
            nonbasis_[j] = 0;
        }
    }

    const auto& A = problem.constraint_matrix;
    numerical::SparseMatrix B = build_basis_matrix(A, basis_);

    if (B.nnz() > 0) {
        basis_factorization_ = numerical::create_factorization(
            numerical::FactorizationType::LU,
            B,
            tol_
        );
    }

    if (basis_factorization_ && ncons > 0) {
        std::vector<double> b(ncons);
        for (std::size_t i = 0; i < ncons; ++i) {
            b[i] = problem.constraints[i].rhs;
        }
        basis_factorization_->solve(b);
        basic_solution_ = b;
    }
}

void SimplexSolver::compute_dual_solution() {
    if (!basis_factorization_) return;

    std::size_t ncons = basis_.size();
    std::vector<double> rhs(ncons);

    for (std::size_t i = 0; i < ncons; ++i) {
        int var = basis_[i];
        if (var >= 0) {
            rhs[i] = phase_cost_[static_cast<std::size_t>(var)];
        }
    }

    basis_factorization_->solve_transpose(rhs);
    dual_solution_ = rhs;
}

void SimplexSolver::compute_reduced_costs() {
    const std::size_t nvars = nonbasis_.size();
    if (nvars == 0) return;

    const std::vector<double> contrib = problem_->constraint_matrix.transpose_multiply(dual_solution_);
    for (std::size_t j = 0; j < nvars; ++j) {
        reduced_costs_[j] = phase_cost_[j] - contrib[j];
    }
}

void SimplexSolver::update_devex_weights(int leaving) {
    if (options_.pricing != PricingStrategy::DEVEX) return;
    if (leaving < 0 || !basis_factorization_) return;
    if (pricing_weights_.size() != reduced_costs_.size()) return;

    // Pivot row of the pre-pivot basis: u = e_leaving^T B^{-1} (BTran), then
    // gather alpha_j = u^T a_j over the (newly updated) nonbasic columns. The
    // entering column had the largest |alpha| (the pivot element), so scaling by
    // the sup normalizes the weights to <= 1; each column's weight is kept
    // monotone (max) so the rank approximation stays bounded over a run.
    std::vector<double> u(basis_.size(), 0.0);
    u[static_cast<std::size_t>(leaving)] = 1.0;
    basis_factorization_->solve_transpose(u);

    std::vector<double> row_vals(reduced_costs_.size(), 0.0);
    const auto& A = problem_->constraint_matrix;
    for (std::size_t i = 0; i < A.rows(); ++i) {
        if (u[i] == 0.0) continue;
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            row_vals[A.col_indices()[k]] += A.values()[k] * u[i];
        }
    }

    double s = 0.0;
    for (std::size_t j = 0; j < row_vals.size(); ++j) {
        if (nonbasis_[j] < 0) continue;
        s = std::max(s, std::abs(row_vals[j]));
    }
    if (s == 0.0) return;

    for (std::size_t j = 0; j < row_vals.size(); ++j) {
        if (nonbasis_[j] < 0) continue;
        const double t = row_vals[j] / s;
        const double w = t * t;
        if (w > pricing_weights_[j]) pricing_weights_[j] = w;
    }
}

void SimplexSolver::initialize_pricing_weights() {
    pricing_weights_.assign(reduced_costs_.size(), 1.0);
}

// Ranks improving nonbasis candidates in decreasing score. Score:
//   DANTZIG      -> |rc|
//   DEVEX        -> |rc| / sqrt(w)       (per-column Devex weight)
//   STEEPEST_EDGE-> |rc| / sqrt(w)       (rescaled exactly later by the caller)
// Zero-weight guarding keeps the sqrt stable. Ties resolve to the lowest index
// (Bland tie-break) when bland_rule is enabled.
std::vector<std::size_t> SimplexSolver::ranked_candidates(bool maximize) const {
    std::vector<std::size_t> cands;
    const std::size_t nvars = reduced_costs_.size();
    struct Ranked { std::size_t j; double score; };
    std::vector<Ranked> ranked;
    for (std::size_t j = 0; j < nvars; ++j) {
        if (nonbasis_[j] < 0) continue;
        if (!phase1_active_ && artificial_cols_[j]) continue;
        double rc = reduced_costs_[j];
        if (maximize) rc = -rc;
        if (rc >= -tol_.optimality_tol()) continue;
        double w = pricing_weights_[j];
        if (!(w > 0.0) || !std::isfinite(w)) w = 1.0;
        const double score = std::abs(rc) / std::sqrt(w);
        ranked.push_back({j, score});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
        [&](const Ranked& a, const Ranked& b) {
            if (a.score != b.score) return a.score > b.score;
            return options_.bland_rule ? a.j < b.j : true;
        });
    for (const auto& r : ranked) cands.push_back(r.j);
    return cands;
}

// Exact edge length 1 + ||B^{-1} a_var||^2 for the current (eta-updated)
// factorization. One FTRAN per call.
double SimplexSolver::exact_edge_weight(std::size_t var) const {
    std::vector<double> col;
    extract_column(problem_->constraint_matrix, var, col);
    if (basis_factorization_) basis_factorization_->solve(col);
    double n2 = 1.0;
    for (double v : col) n2 += v * v;
    return n2;
}

int SimplexSolver::select_entering_variable() {
    if (pricing_weights_.size() != reduced_costs_.size()) initialize_pricing_weights();
    const std::size_t nvars = reduced_costs_.size();

    // Anti-cycling fallback: Bland's index scan is cyclically safe, so it is the
    // default entering rule (and the forced rule on a perturbed problem). The
    // explicit pricing strategies below apply only when bland_rule is disabled.
    if (options_.bland_rule || perturbed_) {
        for (std::size_t j = 0; j < nvars; ++j) {
            if (nonbasis_[j] < 0) continue;
            if (!phase1_active_ && artificial_cols_[j]) continue;
            double rc = reduced_costs_[j];
            if (maximize_) rc = -rc;
            if (rc < -tol_.optimality_tol()) return static_cast<int>(j);
        }
        return -1;
    }

    std::vector<std::size_t> cands = ranked_candidates(maximize_);
    if (cands.empty()) return -1;

    if (options_.pricing == PricingStrategy::STEEPEST_EDGE) {
        const std::size_t probe = std::min(options_.steepest_edge_shortlist, cands.size());
        std::size_t best = cands[0];
        double best_score = -1.0;
        for (std::size_t k = 0; k < probe; ++k) {
            const std::size_t j = cands[k];
            double rc = reduced_costs_[j];
            if (maximize_) rc = -rc;
            const double w = exact_edge_weight(j);
            const double score = std::abs(rc) / std::sqrt(w);
            if (score > best_score || (score == best_score && j < best)) {
                best_score = score;
                best = j;
            }
        }
        return static_cast<int>(best);
    }

    return static_cast<int>(cands[0]);
}

int SimplexSolver::select_leaving_variable(int entering) {
    std::size_t ncons = basis_.size();

    if (!basis_factorization_) return -1;

    std::vector<double> pivot_col;
    extract_column(problem_->constraint_matrix, static_cast<std::size_t>(entering), pivot_col);

    std::vector<double> d = pivot_col;
    basis_factorization_->solve(d);

    const double ratio_tie_tol = 1e-9;
    // A basic variable can sit slightly below zero from accumulated round-off.
    // Such a variable must be allowed to leave (its ratio is clamped to zero)
    // rather than skipped, otherwise a later pivot drives it arbitrarily
    // negative. Genuinely infeasible values (well beyond round-off) keep the
    // historical skip so the primal simplex does not degenerate into a chain of
    // zero-step pivots on a basis it cannot repair.
    auto relaxed_ratio = [&](std::size_t i, double aij, double& ratio) -> bool {
        ratio = basic_solution_[i] / aij;
        if (ratio >= 0.0) return true;
        return ratio >= -tol_.feasibility_tol() ? (ratio = 0.0, true) : false;
    };
    if (options_.ratio_test == RatioTest::HARRIS_TWO_PASS) {
        // Corrected Harris two-pass ratio test
        double theta_max = std::numeric_limits<double>::infinity();
        int best_row = -1;
        
        // Pass 1: maximum allowed step size tolerating bound violation up to feasibility_tol
        for (std::size_t i = 0; i < ncons; ++i) {
            const double aij = d[i];
            if (aij <= tol_.pivot_tol()) continue;
            // Ignore variables that are already heavily infeasible (they stay in basis until fixed)
            if (basic_solution_[i] < -tol_.feasibility_tol()) continue;
            
            // max step size = (value + tolerance) / aij
            double max_step = (std::max(0.0, basic_solution_[i]) + tol_.feasibility_tol()) / aij;
            if (max_step < theta_max) {
                theta_max = max_step;
            }
        }
        
        // Pass 2: among candidates whose exact ratio is <= theta_max, pick the largest pivot
        double best_pivot = 0.0;
        for (std::size_t i = 0; i < ncons; ++i) {
            const double aij = d[i];
            if (aij <= tol_.pivot_tol()) continue;
            
            double ratio;
            if (!relaxed_ratio(i, aij, ratio)) continue;
            
            if (ratio <= theta_max) {
                if (best_row < 0 || aij > best_pivot + tol_.zero_tol()) {
                    best_pivot = aij;
                    best_row = static_cast<int>(i);
                }
            }
        }
        
        if (best_row < 0) {
            pivot_direction_.clear();
            last_leaving_ = -1;
            last_entering_ = -1;
            return -1;
        }

        pivot_direction_ = std::move(d);
        last_leaving_ = best_row;
        last_entering_ = entering;
        return best_row;
    }

    // Standard single-pass ratio test (kept for the STANDARD ratio-test option).
    int best_row = -1;
    double best_ratio = std::numeric_limits<double>::infinity();
    int best_var = -1;
    for (std::size_t i = 0; i < ncons; ++i) {
        const double aij = d[i];
        if (aij > tol_.pivot_tol()) {
            double ratio;
            if (!relaxed_ratio(i, aij, ratio)) continue;
            if (best_row < 0 || ratio < best_ratio - ratio_tie_tol) {
                best_ratio = ratio;
                best_row = static_cast<int>(i);
                best_var = basis_[i];
            } else if (options_.bland_rule && std::abs(ratio - best_ratio) <= ratio_tie_tol &&
                       basis_[i] < best_var) {
                best_var = basis_[i];
                best_row = static_cast<int>(i);
            }
        }
    }

    // Cache the FTRAN direction for the basis update and so `pivot` can reuse
    // it rather than re-solving for the same (entering, leaving) pair.
    if (best_row >= 0) {
        pivot_direction_ = std::move(d);
        last_leaving_ = best_row;
        last_entering_ = entering;
    }
    return best_row;
}

void SimplexSolver::pivot(int entering, int leaving) {
    int leaving_var = basis_[leaving];

    std::vector<double> d;
    if (last_entering_ == entering && last_leaving_ == leaving &&
        pivot_direction_.size() == basic_solution_.size()) {
        d = pivot_direction_;
    } else {
        std::vector<double> pivot_col;
        extract_column(problem_->constraint_matrix, static_cast<std::size_t>(entering), pivot_col);
        d = pivot_col;
        basis_factorization_->solve(d);
    }

    double pivot_val = d[leaving];
    if (std::abs(pivot_val) < tol_.singular_tol()) {
        throw std::runtime_error("Singular pivot element");
    }

    pivot_direction_ = d;
    last_leaving_ = leaving;
    last_entering_ = entering;

    const double step = basic_solution_[leaving] / pivot_val;
    for (std::size_t i = 0; i < basic_solution_.size(); ++i) {
        if (i == static_cast<std::size_t>(leaving)) {
            basic_solution_[i] = step;
        } else {
            basic_solution_[i] -= d[i] * step;
        }
    }

    basis_[leaving] = entering;
    nonbasis_[entering] = -1;
    if (leaving_var >= 0) {
        nonbasis_[leaving_var] = 0;
    }

    // Devex weight maintenance runs against the (still valid) pre-pivot
    // factorization, which carries the pivot row used to touch the candidate
    // weights. The updated nonbasic set is the one the new weights describe.
    update_devex_weights(leaving);
}

void SimplexSolver::update_basis_factorization(int /*leaving*/, int /*entering*/) {
    if (!problem_) return;
    const std::size_t ncons = basis_.size();
    if (ncons == 0) return;

    const int ld = last_leaving_;
    ++iters_since_refactor_;
    const bool prof = std::getenv("HYPERNOVA_SIMPLEX_PROF") != nullptr;
    // Flat refactor frequency keeps eta chains short (numerically clean for
    // degenerate phase-1 instances); the sparse LU refresh is cheap enough
    // that the periodic rebuild dominates no run.
    refactor_frequency_ = 25;
    const bool eta_ok = basis_factorization_ && ld >= 0 && !pivot_direction_.empty() &&
                        iters_since_refactor_ < refactor_frequency_ &&
                        basis_factorization_->apply_eta(ld, pivot_direction_);
    if (prof) {
        std::cerr << "[simplex-update] iter_chain=" << (basis_factorization_
                                                             ? basis_factorization_->eta_chain_size()
                                                             : 0u)
                  << " has_fac=" << (basis_factorization_ != nullptr)
                  << " last_leaving=" << ld
                  << " dir_sz=" << pivot_direction_.size()
                  << " iters_since=" << iters_since_refactor_
                  << " freq=" << refactor_frequency_ << "\n";
    }
if (eta_ok) {
        // Incremental eta-file update: the factorization now represents the
        // updated basis. Re-derive the basic solution from the current
        // factorization to match the reference (full-refactor) behavior and
        // keep the ratio tests clean over long eta chains.
        last_leaving_ = -1;
        last_entering_ = -1;
    } else {
        iters_since_refactor_ = 0;
        numerical::SparseMatrix B = build_basis_matrix(problem_->constraint_matrix, basis_);
        basis_factorization_ = numerical::create_factorization(
            numerical::FactorizationType::LU,
            B,
            tol_
        );
        last_leaving_ = -1;
        last_entering_ = -1;
    }
    if (!basis_factorization_) return;

    std::vector<double> b(ncons);
    for (std::size_t i = 0; i < ncons; ++i) {
        b[i] = (i < rhs_values_.size()) ? rhs_values_[i] : problem_->constraints[i].rhs;
    }
    basis_factorization_->solve(b);
    basic_solution_ = std::move(b);
}

bool SimplexSolver::check_optimality() {
    for (std::size_t j = 0; j < reduced_costs_.size(); ++j) {
        if (nonbasis_[j] < 0) continue;
        if (!phase1_active_ && artificial_cols_[j]) continue;
        double rc = reduced_costs_[j];
        if (maximize_) rc = -rc;
        if (rc < -tol_.optimality_tol()) {
            return false;
        }
    }
    return true;
}

bool SimplexSolver::check_unbounded(int entering) {
    std::size_t var_idx = static_cast<std::size_t>(entering);
    double ub = problem_->variables[var_idx].upper_bound;
    bool improving_positive = (problem_->obj_sense == model::ObjectiveSense::MAXIMIZE &&
                               problem_->variables[var_idx].objective_coeff > 0.0) ||
                              (problem_->obj_sense == model::ObjectiveSense::MINIMIZE &&
                               problem_->variables[var_idx].objective_coeff < 0.0);
    if (improving_positive && !std::isinf(ub)) return false;

    if (!basis_factorization_) return true;

    std::vector<double> column;
    extract_column(problem_->constraint_matrix, var_idx, column);

    basis_factorization_->solve(column);

    for (double val : column) {
        if (val > tol_.pivot_tol()) return false;
    }
    return true;
}

void SimplexSolver::apply_perturbation() {
    if (!basis_factorization_ || basic_solution_.empty()) return;
    ++perturb_level_;
    std::size_t ncons = basis_.size();
    std::vector<double> b(ncons);
    std::vector<double> saved_rhs(ncons);
    for (std::size_t i = 0; i < ncons; ++i) {
        saved_rhs[i] = rhs_values_[i];
    }
    const double amp = options_.perturbation_factor * static_cast<double>(perturb_level_);
    for (std::size_t i = 0; i < ncons; ++i) {
        rhs_values_[i] = problem_->constraints[i].rhs + amp * (1.0 + static_cast<double>(i));
        b[i] = rhs_values_[i];
    }
    basis_factorization_->solve(b);
    for (double val : b) {
        if (val < -tol_.feasibility_tol()) {
            rhs_values_.assign(saved_rhs.begin(), saved_rhs.end());
            --perturb_level_;
            return;
        }
    }
    basic_solution_ = std::move(b);
    perturbed_ = true;
}

void SimplexSolver::remove_perturbation() {
    if (!basis_factorization_ || basic_solution_.empty()) return;
    std::size_t ncons = basis_.size();
    std::vector<double> b(ncons);
    for (std::size_t i = 0; i < ncons; ++i) {
        rhs_values_[i] = problem_->constraints[i].rhs;
        b[i] = rhs_values_[i];
    }
    basis_factorization_->solve(b);
    basic_solution_ = std::move(b);
    perturbed_ = false;
}

bool SimplexSolver::prepare_dual_start() {
    // Mutates the expanded equality form so the identity (slack/artificial)
    // basis is dual feasible: every column's minimization-form cost is made
    // nonnegative by complementing negative-cost columns on their finite upper
    // bound (x_j -> ub_j - x_j, entries negated, RHS shifted). All-or-nothing:
    // if any column with negative cost has no finite upper bound, no mutation
    // is applied and the caller falls back to the primal simplex.
    const std::size_t n = n_original_vars_;

    complemented_.assign(n, 0);
    was_maximizing_ = (problem_->obj_sense == model::ObjectiveSense::MAXIMIZE);

    std::vector<double> min_cost(n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        double c = expanded_problem_.variables[j].objective_coeff;
        if (was_maximizing_) c = -c;
        min_cost[j] = c;
        if (c < -tol_.optimality_tol()) {
            if (!std::isfinite(expanded_problem_.variables[j].upper_bound)) {
                complemented_.clear();
                return false;
            }
            complemented_[j] = 1;
        }
    }

    if (expanded_problem_.constraint_matrix.order() != numerical::StorageOrder::CSR) {
        complemented_.clear();
        return false;
    }

    // Rewrite costs to minimization form (nonnegative after complementation).
    for (std::size_t j = 0; j < n; ++j) {
        double new_cost = complemented_[j] ? -min_cost[j] : std::max(min_cost[j], 0.0);
        expanded_problem_.variables[j].objective_coeff = new_cost;
    }

    // Negate complemented columns and shift the affected original rows' RHS.
    // The per-variable bound (shadow) rows must be left untouched:
    // complementing x_j = ub_j - x_j' preserves the shadow equation
    // x_j' + s = ub_j with a +1 coefficient, whereas negating it would
    // discard the upper bound.
    auto& A = expanded_problem_.constraint_matrix;
    for (std::size_t j = 0; j < n; ++j) {
        if (!complemented_[j]) continue;
        const double ub = expanded_problem_.variables[j].upper_bound;
        const std::size_t orig_rows = n_original_rows_;
        for (std::size_t i = 0; i < orig_rows; ++i) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                if (A.col_indices()[k] == j) {
                    const double v = A.values()[k];
                    A.mutable_values()[k] = -v;
                    expanded_problem_.constraints[i].rhs -= v * ub;
                }
            }
        }
    }
    return true;
}

int SimplexSolver::select_dual_leaving() {
    int p = -1;
    double worst = -1.0;
    for (std::size_t i = 0; i < basis_.size(); ++i) {
        if (basic_solution_[i] >= -tol_.feasibility_tol()) continue;
        if (options_.bland_rule) {
            if (p < 0) p = static_cast<int>(i);
        } else if (p < 0 || basic_solution_[i] < worst) {
            worst = basic_solution_[i];
            p = static_cast<int>(i);
        }
    }
    return p;
}

int SimplexSolver::select_dual_entering(const std::vector<double>& pivot_row) {
    const double tie_tol = 1e-9;
    struct DualCand { int q; double ratio; };
    std::vector<DualCand> cands;
    for (std::size_t j = 0; j < pivot_row.size(); ++j) {
        if (nonbasis_[j] < 0) continue;
        const double wj = pivot_row[j];
        if (wj > -tol_.pivot_tol()) continue;
        cands.push_back({static_cast<int>(j), reduced_costs_[j] / (-wj)});
    }
    if (cands.empty()) return -1;

    if (options_.pricing == PricingStrategy::STEEPEST_EDGE) {
        // Dual feasibility requires the minimum-ratio candidate: selecting any
        // other would make a reduced cost negative. Steepest-edge therefore only
        // re-ranks the candidates tied at the minimum ratio, preferring the
        // longest dual step among them (largest ||B^{-1} a_q||) to break
        // degeneracy. Ties on edge length resolve to the lowest index (Bland).
        double rmin = cands[0].ratio;
        for (const auto& c : cands) rmin = std::min(rmin, c.ratio);
        int best_q = -1;
        double best_w = -1.0;
        for (const auto& c : cands) {
            if (c.ratio > rmin + tie_tol) continue;
            const double w = exact_edge_weight(static_cast<std::size_t>(c.q));
            if (best_q < 0 || w > best_w + tie_tol ||
                (std::abs(w - best_w) <= tie_tol && options_.bland_rule && c.q < best_q)) {
                best_w = w;
                best_q = c.q;
            }
        }
        return best_q;
    }

    int best_q = -1;
    double best_ratio = 0.0;
    for (const auto& c : cands) {
        if (best_q < 0 || c.ratio < best_ratio - tie_tol ||
            (options_.bland_rule && std::abs(c.ratio - best_ratio) <= tie_tol && c.q < best_q)) {
            best_ratio = c.ratio;
            best_q = c.q;
        }
    }
    return best_q;
}

model::ProblemStatus SimplexSolver::run_dual_loop() {
    if (!basis_factorization_) return model::ProblemStatus::NUMERICAL_ERROR;

    const std::size_t nvars_l = problem_->variables.size();
    const std::size_t max_it = static_cast<std::size_t>(options_.max_iterations);
    dual_iterations_ = 0;

    for (; dual_iterations_ < max_it; ++dual_iterations_) {
        if (options_.interrupt_callback && options_.interrupt_callback())
            return model::ProblemStatus::INTERRUPTED;
        if (options_.time_limit_seconds > 0.0) {
            auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration<double>(now - solve_start_time_).count() >= options_.time_limit_seconds)
                return model::ProblemStatus::TIME_LIMIT;
        }

        compute_dual_solution();
        compute_reduced_costs();

        // The complementation construction keeps the identity basis dual
        // feasible and every dual pivot preserves that invariant; a real
        // violation indicates numerical breakdown.
        for (std::size_t j = 0; j < reduced_costs_.size(); ++j) {
            if (nonbasis_[j] < 0) continue;
            if (reduced_costs_[j] < -tol_.optimality_tol()) {
                return model::ProblemStatus::NUMERICAL_ERROR;
            }
        }

        bool primal_feasible = true;
        for (double v : basic_solution_) {
            if (v < -tol_.feasibility_tol()) { primal_feasible = false; break; }
        }
        if (primal_feasible) return model::ProblemStatus::OPTIMAL;

        const int p = select_dual_leaving();
        if (p < 0) return model::ProblemStatus::OPTIMAL;

        // Pivot row: alpha = e_p^T B^{-1}, w_j = alpha^T A_j over nonbasics.
        std::vector<double> e(basis_.size(), 0.0);
        e[static_cast<std::size_t>(p)] = 1.0;
        basis_factorization_->solve_transpose(e);

        std::vector<double> pivot_row(nvars_l, 0.0);
        const auto& A = problem_->constraint_matrix;
        for (std::size_t i = 0; i < A.rows(); ++i) {
            if (e[i] == 0.0) continue;
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                pivot_row[A.col_indices()[k]] += A.values()[k] * e[i];
            }
        }

        const int q = select_dual_entering(pivot_row);
        if (q < 0) return model::ProblemStatus::INFEASIBLE;

        pivot(q, p);
        update_basis_factorization(p, q);

        bool num_bad = false;
        for (double val : basic_solution_) {
            if (std::isnan(val) || std::isinf(val)) {
                num_bad = true;
                break;
            }
        }
        if (num_bad) {
            return model::ProblemStatus::NUMERICAL_ERROR;
        }
    }
    return model::ProblemStatus::ITER_LIMIT;
}

} // namespace hypernova::lp
