#include "active_set.hpp"
#include <algorithm>
#include "../numerical/refinement.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hypernova::qp {

ActiveSetQPSolver::ActiveSetQPSolver(const numerical::ToleranceConfig& tol, const ActiveSetOptions& options)
    : tol_(tol), options_(options) {}

ActiveSetResult ActiveSetQPSolver::solve(const model::Problem& problem) {
    auto start_time = std::chrono::high_resolution_clock::now();

    ActiveSetResult result;
    std::size_t nvars = problem.variables.size();

    if (!problem.is_qp() && !problem.is_miqp()) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
        return result;
    }

    model::Problem minimized = problem;
    const bool negate = (problem.obj_sense == model::ObjectiveSense::MAXIMIZE);
    if (negate) {
        minimized.obj_sense = model::ObjectiveSense::MINIMIZE;
        minimized.obj_offset = -problem.obj_offset;
        for (std::size_t j = 0; j < minimized.variables.size(); ++j) {
            minimized.variables[j].objective_coeff = -problem.variables[j].objective_coeff;
        }
        for (auto& term : minimized.quadratic_terms) {
            term.coeff = -term.coeff;
        }
    }

    if (options_.check_convexity && !check_convexity(minimized)) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
        return result;
    }

    try {
        initialize(minimized);

        bool time_up = false;
        bool had_interrupt = false;
        std::size_t stall = 0;
        std::size_t recoveries = 0;
        double prev_obj = compute_objective(minimized, x_);
        for (std::size_t iter = 0; iter < static_cast<std::size_t>(options_.max_iterations); ++iter) {
            if (options_.interrupt_callback && options_.interrupt_callback()) {
                had_interrupt = true;
                break;
            }
            if (options_.time_limit_seconds > 0.0) {
                auto now = std::chrono::high_resolution_clock::now();
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                if (elapsed >= options_.time_limit_seconds) {
                    time_up = true;
                    break;
                }
            }

            int redundant = find_redundant_constraint(minimized);
            if (redundant >= 0) {
                remove_constraint(redundant);
                continue;
            }

            int blocking = find_blocking_constraint(minimized);
            if (blocking >= 0) {
                add_constraint(blocking);
                continue;
            }

            if (is_kkt_optimal(minimized)) {
                result.status = model::ProblemStatus::OPTIMAL;
                break;
            }
            if (std::getenv("HYPERNOVA_DBG_QP")) {
                std::cerr << "[QP] iter=" << iter << " stall=" << stall << " x=[";
                for (double v : x_) std::cerr << " " << v;
                std::cerr << " ] active_set=";
                for (int a : active_set_) std::cerr << " " << a;
                std::cerr << " lambda=[";
                for (double l : lambda_) std::cerr << " " << l;
                std::cerr << " ] f=" << compute_objective(minimized, x_) << "\n";
            }

            solve_kkt_system(minimized);
            result.iterations = iter + 1;

            redundant = find_redundant_constraint(minimized);
            if (redundant >= 0) {
                remove_constraint(redundant);
            }

            // Anti-cycling: on rank-deficient (e.g. rank-1) convex QPs the
            // redundant/blocking add-remove toggle can spin without progress
            // for the whole iteration budget. Detect a stall (no objective
            // decrease) and hand off to a projected-gradient recovery that
            // polishes a feasible point on the current active face.
            double cur_obj = compute_objective(minimized, x_);
            if (cur_obj < prev_obj - std::max(1e-11, 1e-9 * std::max(1.0, std::abs(prev_obj)))) {
                prev_obj = cur_obj;
                stall = 0;
            } else {
                ++stall;
            }
            if (stall >= 250 && recoveries < 8) {
                ++recoveries;
                stall = 0;
                bool recovered = projected_gradient_recovery(minimized);
                if (recovered) {
                    prev_obj = compute_objective(minimized, x_);
                }
            }
        }

        if (result.status == model::ProblemStatus::UNKNOWN) {
            result.status = had_interrupt ? model::ProblemStatus::INTERRUPTED
                                          : (time_up ? model::ProblemStatus::TIME_LIMIT
                                                     : model::ProblemStatus::ITER_LIMIT);
            if (result.status == model::ProblemStatus::ITER_LIMIT &&
                max_constraint_violation(minimized, x_) <= options_.feasibility_tol) {
                result.status = model::ProblemStatus::SUBOPTIMAL;
            }
        }

        result.primal = x_;
        result.dual = lambda_;
        result.active_set = active_set_;

        result.objective_value = 0.0;
        for (std::size_t j = 0; j < nvars; ++j) {
            result.objective_value += minimized.variables[j].objective_coeff * x_[j];
        }
        for (const auto& term : minimized.quadratic_terms) {
            if (term.row == term.col) {
                result.objective_value += 0.5 * term.coeff * x_[term.row] * x_[term.col];
            } else {
                result.objective_value += term.coeff * x_[term.row] * x_[term.col];
            }
        }
        result.objective_value += minimized.obj_offset;

        if (negate) {
            result.objective_value = -result.objective_value;
            for (auto& l : result.dual) l = -l;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    } catch (const std::exception&) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
    }

    return result;
}

void ActiveSetQPSolver::initialize(const model::Problem& problem) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    x_.assign(nvars, 0.0);
    lambda_.assign(ncons, 0.0);
    active_set_.clear();

    for (std::size_t j = 0; j < nvars; ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        if (lb > -std::numeric_limits<double>::infinity() &&
            ub < std::numeric_limits<double>::infinity()) {
            x_[j] = (lb + ub) / 2.0;
        } else if (lb > -std::numeric_limits<double>::infinity()) {
            x_[j] = lb + 1.0;
        } else if (ub < std::numeric_limits<double>::infinity()) {
            x_[j] = ub - 1.0;
        }
    }

    for (std::size_t i = 0; i < ncons; ++i) {
        const auto& con = problem.constraints[i];
        double activity = 0.0;
        const auto& A = problem.constraint_matrix;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                activity += A.values()[k] * x_[j];
            }
        }

        bool active = false;
        if (con.sense == model::ConstraintSense::LE) {
            active = std::abs(activity - con.rhs) < options_.feasibility_tol;
        } else if (con.sense == model::ConstraintSense::GE) {
            active = std::abs(activity - con.rhs) < options_.feasibility_tol;
        } else if (con.sense == model::ConstraintSense::EQ) {
            active = std::abs(activity - con.rhs) < options_.feasibility_tol;
        }

        if (active) {
            active_set_.push_back(static_cast<int>(i));
        }
    }
}

bool ActiveSetQPSolver::check_convexity(const model::Problem& problem) {
    if (problem.quadratic_terms.empty()) return true;

    std::size_t nvars = problem.variables.size();
    std::vector<numerical::Triplet> triplets;
    triplets.reserve(problem.quadratic_terms.size());
    for (const auto& term : problem.quadratic_terms) {
        if (term.row == term.col) {
            triplets.emplace_back(term.row, term.col, term.coeff);
        } else {
            triplets.emplace_back(term.row, term.col, term.coeff);
            triplets.emplace_back(term.col, term.row, term.coeff);
        }
    }

    numerical::SparseMatrix q_mat = numerical::SparseMatrix::from_triplets(nvars, nvars, triplets);
    auto ldlt = numerical::create_factorization(numerical::FactorizationType::LDLT, q_mat, tol_);
    if (!ldlt) return false;

    for (double d : ldlt->D_values()) {
        if (d < -std::max(options_.feasibility_tol, tol_.singular_tol())) return false;
    }
    return true;
}

bool ActiveSetQPSolver::is_kkt_optimal(const model::Problem& problem) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    std::vector<double> grad(nvars, 0.0);
    for (std::size_t j = 0; j < nvars; ++j) {
        grad[j] = problem.variables[j].objective_coeff;
    }

    for (const auto& term : problem.quadratic_terms) {
        grad[term.row] += term.coeff * x_[term.col];
        if (term.row != term.col) {
            grad[term.col] += term.coeff * x_[term.row];
        }
    }

    const auto& A = problem.constraint_matrix;
    // Active constraints must actually hold: a KKT point with a violated
    // active constraint (e.g. after clamped initialization) is not optimal.
    for (int idx : active_set_) {
        std::size_t i = static_cast<std::size_t>(idx);
        if (i >= ncons) continue;

        const auto& con = problem.constraints[i];
        double activity = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                activity += A.values()[k] * x_[j];
            }
        }

        if (con.sense == model::ConstraintSense::EQ) {
            if (std::abs(activity - con.rhs) > options_.feasibility_tol) return false;
        } else if (con.sense == model::ConstraintSense::LE) {
            if (activity - con.rhs > options_.feasibility_tol) return false;
        } else {  // GE
            if (con.rhs - activity > options_.feasibility_tol) return false;
        }
    }

    for (int idx : active_set_) {
        std::size_t i = static_cast<std::size_t>(idx);
        if (i >= ncons) continue;

        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                grad[j] += lambda_[i] * A.values()[k];
            }
        }
    }

    for (std::size_t j = 0; j < nvars; ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        double val = x_[j];
        bool debug = std::getenv("HYPERNOVA_DBG_QP") != nullptr;

        if (debug) {
            std::cerr << "[QP-KKT] j=" << j << " val=" << val << " lb=" << lb << " ub=" << ub
                      << " grad=" << grad[j] << "\n";
        }

        // Any value outside its bounds is infeasible and cannot be optimal,
        // regardless of gradient sign (e.g. a branching-fixed integer left at
        // its pin, or a clamped variable never driven back inside).
        if (val < lb - options_.feasibility_tol || val > ub + options_.feasibility_tol) {
            if (debug) std::cerr << "[QP-KKT]   -> fail: out of bounds\n";
            return false;
        }

        // A variable pinned to a single value (lb == ub, e.g. a branching
        // bound fixing an integer) must still be feasible (its value is not
        // allowed to drift outside the pin); the gradient sign carries no
        // optimality information for such variables.
        if (ub - lb <= options_.feasibility_tol) {
            continue;
        }

        if (val <= lb + options_.feasibility_tol) {
            if (grad[j] < -options_.optimality_tol) {
                if (debug) std::cerr << "[QP-KKT]   -> fail at lb, grad<0\n";
                return false;
            }
        } else if (val >= ub - options_.feasibility_tol) {
            if (grad[j] > options_.optimality_tol) {
                if (debug) std::cerr << "[QP-KKT]   -> fail at ub, grad>0\n";
                return false;
            }
        } else {
            if (std::abs(grad[j]) > options_.optimality_tol) {
                if (debug) std::cerr << "[QP-KKT]   -> fail interior, |grad|>tol\n";
                return false;
            }
        }
    }

    return true;
}

int ActiveSetQPSolver::find_blocking_constraint(const model::Problem& problem) {
    std::size_t ncons = problem.constraints.size();

    for (std::size_t i = 0; i < ncons; ++i) {
        if (std::find(active_set_.begin(), active_set_.end(), static_cast<int>(i)) != active_set_.end()) {
            continue;
        }

        const auto& con = problem.constraints[i];
        double activity = 0.0;
        const auto& A = problem.constraint_matrix;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                activity += A.values()[k] * x_[j];
            }
        }

        double violation = 0.0;
        if (con.sense == model::ConstraintSense::LE) {
            violation = activity - con.rhs;
        } else if (con.sense == model::ConstraintSense::GE) {
            violation = con.rhs - activity;
        } else if (con.sense == model::ConstraintSense::EQ) {
            violation = std::abs(activity - con.rhs);
        }

        if (violation > options_.feasibility_tol) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ActiveSetQPSolver::find_redundant_constraint(const model::Problem& problem) {
    for (int idx : active_set_) {
        if (idx < 0) continue;
        std::size_t i = static_cast<std::size_t>(idx);
        const auto& con = problem.constraints[i];

        if (con.sense == model::ConstraintSense::EQ) continue;

        double activity = 0.0;
        const auto& A = problem.constraint_matrix;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                activity += A.values()[k] * x_[j];
            }
        }

        double violation = 0.0;
        bool wrong_sign = false;
        if (con.sense == model::ConstraintSense::LE) {
            violation = activity - con.rhs;
            wrong_sign = lambda_[i] < -options_.optimality_tol;
        } else if (con.sense == model::ConstraintSense::GE) {
            violation = con.rhs - activity;
            wrong_sign = lambda_[i] > options_.optimality_tol;
        } else {
            violation = std::abs(activity - con.rhs);
        }

        if (violation > -options_.feasibility_tol && wrong_sign) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ActiveSetQPSolver::add_constraint(int idx) {
    if (std::find(active_set_.begin(), active_set_.end(), idx) == active_set_.end()) {
        active_set_.push_back(idx);
    }
}

void ActiveSetQPSolver::remove_constraint(int idx) {
    active_set_.erase(
        std::remove(active_set_.begin(), active_set_.end(), idx),
        active_set_.end()
    );
    if (idx >= 0 && static_cast<std::size_t>(idx) < lambda_.size()) {
        lambda_[static_cast<std::size_t>(idx)] = 0.0;
    }
}

void ActiveSetQPSolver::solve_kkt_system(const model::Problem& problem) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    std::size_t nactive = active_set_.size();

    if (nactive == 0) {
        bool has_offdiag = false;
        for (const auto& term : problem.quadratic_terms) {
            if (term.row != term.col) { has_offdiag = true; break; }
        }

        if (!has_offdiag) {
            std::vector<double> grad(nvars, 0.0);
            for (std::size_t j = 0; j < nvars; ++j) {
                grad[j] = problem.variables[j].objective_coeff;
            }
            for (const auto& term : problem.quadratic_terms) {
                grad[term.row] += term.coeff * x_[term.col];
                if (term.row != term.col) {
                    grad[term.col] += term.coeff * x_[term.row];
                }
            }

            std::vector<double> dx(nvars, 0.0);
            std::vector<double> h_diag(nvars, 0.0);
            for (const auto& term : problem.quadratic_terms) {
                if (term.row == term.col) h_diag[term.row] += term.coeff;
            }
            for (std::size_t j = 0; j < nvars; ++j) {
                double hval = h_diag[j] + 1e-10;
                dx[j] = -grad[j] / hval;
                double lb = problem.variables[j].lower_bound;
                double ub = problem.variables[j].upper_bound;
                if (ub - lb <= options_.feasibility_tol) dx[j] = 0.0;  // pinned
                // A variable already at a bound whose step points further out
                // must not move; leaving its direction non-zero would freeze
                // the shared max_step and stall the other variables.
                if ((x_[j] <= lb + options_.feasibility_tol && dx[j] < 0) ||
                    (x_[j] >= ub - options_.feasibility_tol && dx[j] > 0)) {
                    dx[j] = 0.0;
                }
            }

            double max_step = 1.0;
            for (std::size_t j = 0; j < nvars; ++j) {
                double lb = problem.variables[j].lower_bound;
                double ub = problem.variables[j].upper_bound;
                if (ub - lb <= options_.feasibility_tol) continue;  // pinned
                if (dx[j] < 0) {
                    if (lb > -std::numeric_limits<double>::infinity()) {
                        max_step = std::min(max_step, (lb - x_[j]) / dx[j]);
                    }
                } else if (dx[j] > 0) {
                    if (ub < std::numeric_limits<double>::infinity()) {
                        max_step = std::min(max_step, (ub - x_[j]) / dx[j]);
                    }
                }
            }
            max_step = std::min(max_step, 1.0);

            for (std::size_t j = 0; j < nvars; ++j) {
                x_[j] += max_step * dx[j];
            }
            return;
        }

        std::vector<double> rhs(nvars, 0.0);
        std::vector<numerical::Triplet> triplets;
        triplets.reserve(nvars + 2 * problem.quadratic_terms.size());
        std::vector<double> h_diag(nvars, 1e-10);
        for (const auto& term : problem.quadratic_terms) {
            if (term.row == term.col) {
                h_diag[term.row] += term.coeff;
            } else {
                triplets.emplace_back(term.row, term.col, term.coeff);
                triplets.emplace_back(term.col, term.row, term.coeff);
            }
        }
        for (std::size_t j = 0; j < nvars; ++j) {
            triplets.emplace_back(j, j, h_diag[j]);
        }

        for (std::size_t j = 0; j < nvars; ++j) {
            double grad = problem.variables[j].objective_coeff;
            for (const auto& term : problem.quadratic_terms) {
                if (term.row == j) grad += term.coeff * x_[term.col];
                if (term.col == j && term.row != j) grad += term.coeff * x_[term.row];
            }
            rhs[j] = -grad;
        }

        std::vector<double> sol(nvars, 0.0);
        numerical::SparseMatrix kkt_mat = numerical::SparseMatrix::from_triplets(nvars, nvars, triplets);
        auto fac = numerical::create_factorization(numerical::FactorizationType::LU, kkt_mat, tol_);
        sol = rhs;
        fac->solve(sol);
        numerical::iterative_refinement(*fac, kkt_mat, sol, rhs);

        for (std::size_t j = 0; j < nvars; ++j) {
            double lb = problem.variables[j].lower_bound;
            double ub = problem.variables[j].upper_bound;
            if (ub - lb <= options_.feasibility_tol) sol[j] = 0.0;  // pinned
            // Bound-saturated variable with an outward step: freeze it so it
            // cannot zero the shared max_step and stall the free variables.
            if ((x_[j] <= lb + options_.feasibility_tol && sol[j] < 0) ||
                (x_[j] >= ub - options_.feasibility_tol && sol[j] > 0)) {
                sol[j] = 0.0;
            }
        }

        double max_step = 1.0;
        for (std::size_t j = 0; j < nvars; ++j) {
            double lb = problem.variables[j].lower_bound;
            double ub = problem.variables[j].upper_bound;
            if (ub - lb <= options_.feasibility_tol) continue;  // pinned
            if (sol[j] < 0) {
                if (lb > -std::numeric_limits<double>::infinity()) {
                    double step = (lb - x_[j]) / sol[j];
                    if (step > 0) max_step = std::min(max_step, step);
                }
            } else if (sol[j] > 0) {
                if (ub < std::numeric_limits<double>::infinity()) {
                    double step = (ub - x_[j]) / sol[j];
                    if (step > 0) max_step = std::min(max_step, step);
                }
            }
        }
        max_step = std::min(max_step, 1.0);

        for (std::size_t j = 0; j < nvars; ++j) {
            x_[j] += max_step * sol[j];
        }
        return;
    }

    const auto& A = problem.constraint_matrix;

    // Pin rows hold fixed variables at their value (lb == ub bounding, or a
    // bound that blocks the current step). They are rebuilt per solve.
    auto build_and_solve = [&](const std::vector<std::pair<std::size_t, double>>& pins)
        -> std::vector<double> {
        std::size_t total_now = nvars + nactive + pins.size();

        std::vector<numerical::Triplet> triplets;
        triplets.reserve(nvars + 2 * problem.quadratic_terms.size() + 2 * A.nnz());

        std::vector<double> h_diag(nvars, 1e-10);
        for (const auto& term : problem.quadratic_terms) {
            if (term.row == term.col) {
                h_diag[term.row] += term.coeff;
            } else {
                triplets.emplace_back(term.row, term.col, term.coeff);
                triplets.emplace_back(term.col, term.row, term.coeff);
            }
        }
        for (std::size_t j = 0; j < nvars; ++j) {
            triplets.emplace_back(j, j, h_diag[j]);
        }

        for (std::size_t w = 0; w < nactive; ++w) {
            std::size_t i = static_cast<std::size_t>(active_set_[w]);
            if (i >= ncons) continue;

            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                triplets.emplace_back(nvars + w, j, A.values()[k]);
                triplets.emplace_back(j, nvars + w, A.values()[k]);
            }
        }

        for (std::size_t p = 0; p < pins.size(); ++p) {
            std::size_t j = pins[p].first;
            triplets.emplace_back(nvars + nactive + p, j, 1.0);
            triplets.emplace_back(j, nvars + nactive + p, 1.0);
        }

        std::vector<double> rhs_now(total_now, 0.0);
        for (std::size_t j = 0; j < nvars; ++j) {
            double grad = problem.variables[j].objective_coeff;
            for (const auto& term : problem.quadratic_terms) {
                if (term.row == j) grad += term.coeff * x_[term.col];
                if (term.col == j && term.row != j) grad += term.coeff * x_[term.row];
            }
            rhs_now[j] = -grad;
        }

        for (std::size_t w = 0; w < nactive; ++w) {
            std::size_t i = static_cast<std::size_t>(active_set_[w]);
            if (i >= ncons) continue;

            double sum = 0.0;
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                sum += A.values()[k] * x_[A.col_indices()[k]];
            }
            rhs_now[nvars + w] = -(sum - problem.constraints[i].rhs);
        }

        for (std::size_t p = 0; p < pins.size(); ++p) {
            // Pin row: hold x_j exactly at its pin value as a hard equality.
            rhs_now[nvars + nactive + p] = -(x_[pins[p].first] - pins[p].second);
        }

        std::vector<double> sol(total_now, 0.0);
        numerical::SparseMatrix kkt_mat = numerical::SparseMatrix::from_triplets(total_now, total_now, triplets);
        kkt_factorization_ = numerical::create_factorization(numerical::FactorizationType::LU, kkt_mat, tol_);
        sol = rhs_now;
        kkt_factorization_->solve(sol);
        numerical::iterative_refinement(*kkt_factorization_, kkt_mat, sol, rhs_now);
        return sol;
    };

    // Phase 1 hard pins: variables whose bounds coincide (e.g. branching-fixed
    // integers). These cannot move at all.
    std::vector<std::pair<std::size_t, double>> pins;
    for (std::size_t j = 0; j < nvars; ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        if (ub - lb <= options_.feasibility_tol) {
            pins.emplace_back(j, lb);
        }
    }

    // Phase 2: a solve with only the hard pins may drive a free variable across
    // one of its bounds (e.g. an active equality pulling a variable that sits on
    // a bound). Add that bound as a pin and re-solve so the remaining variables
    // satisfy the active constraints.
    std::vector<double> sol = build_and_solve(pins);
    {
        std::vector<std::pair<std::size_t, double>> bound_pins;
        bool blocked = false;
        for (std::size_t j = 0; j < nvars; ++j) {
            bool already = false;
            for (const auto& p : pins) {
                if (p.first == j) { already = true; break; }
            }
            if (already) continue;

            double lb = problem.variables[j].lower_bound;
            double ub = problem.variables[j].upper_bound;
            double t = x_[j] + sol[j];
            if (t < lb - options_.feasibility_tol) {
                bound_pins.emplace_back(j, lb);
                blocked = true;
            } else if (t > ub + options_.feasibility_tol) {
                bound_pins.emplace_back(j, ub);
                blocked = true;
            }
        }
        if (blocked) {
            for (const auto& bp : bound_pins) pins.push_back(bp);
            sol = build_and_solve(pins);
        }
    }

    std::vector<double> dx(nvars, 0.0);
    for (std::size_t j = 0; j < nvars; ++j) dx[j] = sol[j];

    double max_step = 1.0;
    for (std::size_t j = 0; j < nvars; ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        if (ub - lb <= options_.feasibility_tol) continue;  // pinned
        if (dx[j] < 0) {
            if (lb > -std::numeric_limits<double>::infinity()) {
                double step = (lb - x_[j]) / dx[j];
                if (step > 0) max_step = std::min(max_step, step);
            }
        } else if (dx[j] > 0) {
            if (ub < std::numeric_limits<double>::infinity()) {
                double step = (ub - x_[j]) / dx[j];
                if (step > 0) max_step = std::min(max_step, step);
            }
        }
    }
    max_step = std::min(max_step, 1.0);

    for (std::size_t j = 0; j < nvars; ++j) {
        x_[j] += max_step * dx[j];
    }

    // Snap pinned variables back onto their pin value: a coupled active
    // constraint could otherwise leave them a hair off the bound.
    for (const auto& p : pins) {
        x_[p.first] = p.second;
    }

    for (std::size_t w = 0; w < nactive; ++w) {
        std::size_t i = static_cast<std::size_t>(active_set_[w]);
        if (i < ncons) {
            lambda_[i] = sol[nvars + w];
        }
    }
}

double ActiveSetQPSolver::compute_objective(const model::Problem& problem,
                                            const std::vector<double>& x) const {
    double obj = problem.obj_offset;
    for (std::size_t j = 0; j < problem.variables.size() && j < x.size(); ++j) {
        obj += problem.variables[j].objective_coeff * x[j];
    }
    for (const auto& term : problem.quadratic_terms) {
        if (term.row >= x.size() || term.col >= x.size()) continue;
        if (term.row == term.col) {
            obj += 0.5 * term.coeff * x[term.row] * x[term.col];
        } else {
            obj += term.coeff * x[term.row] * x[term.col];
        }
    }
    return obj;
}

void ActiveSetQPSolver::compute_gradient(const model::Problem& problem,
                                         const std::vector<double>& x,
                                         std::vector<double>& grad) const {
    grad.assign(problem.variables.size(), 0.0);
    for (std::size_t j = 0; j < problem.variables.size() && j < x.size(); ++j) {
        grad[j] = problem.variables[j].objective_coeff;
    }
    for (const auto& term : problem.quadratic_terms) {
        if (term.row >= x.size() || term.col >= x.size()) continue;
        grad[term.row] += term.coeff * x[term.col];
        if (term.row != term.col) {
            grad[term.col] += term.coeff * x[term.row];
        }
    }
}

double ActiveSetQPSolver::max_constraint_violation(const model::Problem& problem,
                                                   const std::vector<double>& x) const {
    double max_viol = 0.0;
    const auto& A = problem.constraint_matrix;
    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        const auto& con = problem.constraints[i];
        double activity = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                if (j < x.size()) activity += A.values()[k] * x[j];
            }
        }
        double viol = 0.0;
        if (con.sense == model::ConstraintSense::LE) {
            viol = activity - con.rhs;
        } else if (con.sense == model::ConstraintSense::GE) {
            viol = con.rhs - activity;
        } else {
            viol = std::abs(activity - con.rhs);
        }
        max_viol = std::max(max_viol, viol);
    }
    return max_viol;
}

bool ActiveSetQPSolver::project_to_feasible(const model::Problem& problem,
                                            std::vector<double>& x) const {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();
    const auto& A = problem.constraint_matrix;

    auto clip = [&]() {
        for (std::size_t j = 0; j < nvars; ++j) {
            double lb = problem.variables[j].lower_bound;
            double ub = problem.variables[j].upper_bound;
            if (double v = x[j]; v < lb) {
                x[j] = lb;
            } else if (v > ub) {
                x[j] = ub;
            }
        }
    };

    clip();

    for (int pass = 0; pass < 120; ++pass) {
        std::vector<std::size_t> rows;
        bool violated = false;
        for (std::size_t i = 0; i < ncons; ++i) {
            const auto& con = problem.constraints[i];
            double activity = 0.0;
            if (A.order() == model::StorageOrder::CSR) {
                for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                    std::size_t j = A.col_indices()[k];
                    if (j < x.size()) activity += A.values()[k] * x[j];
                }
            }
            bool needs_fix = false;
            if (con.sense == model::ConstraintSense::EQ) {
                needs_fix = std::abs(activity - con.rhs) > options_.feasibility_tol;
            } else if (con.sense == model::ConstraintSense::LE) {
                needs_fix = activity - con.rhs > options_.feasibility_tol;
            } else {
                needs_fix = con.rhs - activity > options_.feasibility_tol;
            }
            if (needs_fix) {
                rows.push_back(i);
                violated = true;
            }
        }
        if (!violated) return true;

        // Project x onto the intersection of the current violation hyperplanes
        // (row residuals r = A_r x - b_r) via the Gram matrix A_r A_r^T.
        std::vector<numerical::Triplet> triplets;
        for (std::size_t a = 0; a < rows.size(); ++a) {
            std::size_t ia = rows[a];
            for (std::size_t b = a; b < rows.size(); ++b) {
                std::size_t ib = rows[b];
                double dot = 0.0;
                for (std::size_t k1 = A.row_ptr()[ia]; k1 < A.row_ptr()[ia + 1]; ++k1) {
                    std::size_t j1 = A.col_indices()[k1];
                    double a1 = A.values()[k1];
                    if (j1 >= x.size()) continue;
                    for (std::size_t k2 = A.row_ptr()[ib]; k2 < A.row_ptr()[ib + 1]; ++k2) {
                        if (A.col_indices()[k2] == j1) {
                            dot += a1 * A.values()[k2];
                            break;
                        }
                    }
                }
                triplets.emplace_back(a, b, dot);
                if (a != b) triplets.emplace_back(b, a, dot);
            }
            triplets.emplace_back(a, a, 1e-12);
        }

        numerical::SparseMatrix gram = numerical::SparseMatrix::from_triplets(rows.size(), rows.size(), triplets);
        auto fac = numerical::create_factorization(numerical::FactorizationType::LU, gram, tol_);
        std::vector<double> u(rows.size(), 0.0);
        for (std::size_t a = 0; a < rows.size(); ++a) {
            std::size_t i = rows[a];
            double activity = 0.0;
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                if (j < x.size()) activity += A.values()[k] * x[j];
            }
            u[a] = activity - problem.constraints[i].rhs;
        }
        if (fac) {
            fac->solve(u);
            for (std::size_t a = 0; a < rows.size(); ++a) {
                std::size_t i = rows[a];
                for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                    std::size_t j = A.col_indices()[k];
                    if (j < x.size()) x[j] -= A.values()[k] * u[a];
                }
            }
        } else {
            return false;
        }
        clip();
    }

    return max_constraint_violation(problem, x) <= options_.feasibility_tol;
}

void ActiveSetQPSolver::rebuild_active_set(const model::Problem& problem) {
    active_set_.clear();
    const auto& A = problem.constraint_matrix;
    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        const auto& con = problem.constraints[i];
        if (con.sense == model::ConstraintSense::EQ) {
            active_set_.push_back(static_cast<int>(i));
            continue;
        }
        double activity = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                if (j < x_.size()) activity += A.values()[k] * x_[j];
            }
        }
        if (std::abs(activity - con.rhs) < options_.feasibility_tol) {
            active_set_.push_back(static_cast<int>(i));
        }
    }
}

void ActiveSetQPSolver::recompute_multipliers(const model::Problem& problem) {
    std::size_t ncons = problem.constraints.size();
    lambda_.assign(ncons, 0.0);

    std::vector<std::size_t> rows;
    for (int idx : active_set_) {
        if (idx >= 0 && static_cast<std::size_t>(idx) < ncons) rows.push_back(static_cast<std::size_t>(idx));
    }
    if (rows.empty()) return;

    std::vector<double> grad;
    compute_gradient(problem, x_, grad);

    // The KKT certificate only requires the reduced gradient to vanish on the
    // *interior* (free) variables; multipliers for variables sitting on a bound
    // are absorbed by the bound condition in the sign test. Least-squares fit
    // the active-row multipliers against the free-variable residuals.
    std::vector<std::size_t> free_vars;
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        double val = x_[j];
        if (ub - lb > options_.feasibility_tol &&
            val > lb + options_.feasibility_tol &&
            val < ub - options_.feasibility_tol) {
            free_vars.push_back(j);
        }
    }
    if (free_vars.empty()) return;

    const auto& A = problem.constraint_matrix;
    // B[k][a] = coefficient of active row 'a' on free variable 'k'.
    std::vector<std::vector<double>> B(free_vars.size(), std::vector<double>(rows.size(), 0.0));
    std::vector<double> neg_g(free_vars.size(), 0.0);
    for (std::size_t k = 0; k < free_vars.size(); ++k) {
        std::size_t j = free_vars[k];
        neg_g[k] = -grad[j];
        for (std::size_t a = 0; a < rows.size(); ++a) {
            std::size_t i = rows[a];
            for (std::size_t kk = A.row_ptr()[i]; kk < A.row_ptr()[i + 1]; ++kk) {
                if (A.col_indices()[kk] == j) {
                    B[k][a] = A.values()[kk];
                    break;
                }
            }
        }
    }

    // Normal equations: (B^T B) lambda = B^T (-g_F), regularized.
    std::vector<numerical::Triplet> triplets;
    for (std::size_t a = 0; a < rows.size(); ++a) {
        for (std::size_t b = a; b < rows.size(); ++b) {
            double dot = 0.0;
            for (std::size_t k = 0; k < free_vars.size(); ++k) dot += B[k][a] * B[k][b];
            triplets.emplace_back(a, b, dot);
            if (a != b) triplets.emplace_back(b, a, dot);
        }
        triplets.emplace_back(a, a, 1e-12);
    }
    numerical::SparseMatrix gram = numerical::SparseMatrix::from_triplets(rows.size(), rows.size(), triplets);
    auto fac = numerical::create_factorization(numerical::FactorizationType::LU, gram, tol_);
    if (!fac) return;

    std::vector<double> rhs(rows.size(), 0.0);
    for (std::size_t a = 0; a < rows.size(); ++a) {
        for (std::size_t k = 0; k < free_vars.size(); ++k) rhs[a] += B[k][a] * neg_g[k];
    }
    fac->solve(rhs);
    for (std::size_t a = 0; a < rows.size(); ++a) {
        lambda_[rows[a]] = rhs[a];
    }
}

bool ActiveSetQPSolver::projected_gradient_recovery(const model::Problem& problem) {
    const std::size_t nvars = problem.variables.size();
    if (nvars == 0) return false;

    std::vector<double> x = x_;
    double best_f = compute_objective(problem, x);
    std::vector<double> best_x = x;
    bool feasible_seed = project_to_feasible(problem, x);
    if (feasible_seed) {
        double f = compute_objective(problem, x);
        if (f < best_f - 1e-12) {
            best_f = f;
            best_x = x;
        }
    }

    bool improved = false;
    auto recovery_start = std::chrono::high_resolution_clock::now();
    if (std::getenv("HYPERNOVA_DBG_QP")) {
        std::cerr << "\n[QP-REC] enter recovery x=[";
        for (double v : x) std::cerr << " " << v;
        std::cerr << " ] f0=" << best_f << " feas_seed=" << feasible_seed
                  << " nvars=" << nvars << " ncons=" << problem.constraints.size() << "\n";
    }
    for (int outer = 0; outer < 400; ++outer) {
        if (options_.interrupt_callback && options_.interrupt_callback()) break;
        if (options_.time_limit_seconds > 0.0) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - recovery_start).count();
            if (elapsed >= options_.time_limit_seconds) break;
        }

        std::vector<double> grad;
        compute_gradient(problem, x, grad);

        bool moved = false;
        for (int ls = 0; ls < 60; ++ls) {
            double t = std::pow(0.5, ls);
            std::vector<double> xt = x;
            for (std::size_t j = 0; j < nvars; ++j) {
                xt[j] -= t * grad[j];
                double lb = problem.variables[j].lower_bound;
                double ub = problem.variables[j].upper_bound;
                if (xt[j] < lb) xt[j] = lb;
                if (xt[j] > ub) xt[j] = ub;
            }
            if (!project_to_feasible(problem, xt)) continue;
            double f = compute_objective(problem, xt);
            if (f <= best_f - 1e-11) {
                best_f = f;
                best_x = xt;
                x = xt;
                moved = true;
                improved = true;
                break;
            }
        }
        if (!moved) break;
    }

    if (std::getenv("HYPERNOVA_DBG_QP")) {
        std::cerr << "[QP-REC] exit improved=" << improved << " best_f=" << best_f << " x=[";
        for (double v : best_x) std::cerr << " " << v;
        std::cerr << " ] viol=" << max_constraint_violation(problem, best_x) << "\n";
    }
    if (!improved) return false;

    x_ = best_x;
    rebuild_active_set(problem);
    recompute_multipliers(problem);
    return true;
}

} // namespace hypernova::qp