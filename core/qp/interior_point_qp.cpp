#include "interior_point_qp.hpp"
#include "../numerical/refinement.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <numeric>

namespace hypernova::qp {

InteriorPointQPSolver::InteriorPointQPSolver(const numerical::ToleranceConfig& tol, const InteriorPointQPOptions& options)
    : tol_(tol), options_(options) {}

InteriorPointQPResult InteriorPointQPSolver::solve(const model::Problem& problem) {
    auto start_time = std::chrono::high_resolution_clock::now();

    InteriorPointQPResult result;
    std::size_t nvars = problem.variables.size();

    if (!problem.is_qp() && !problem.is_miqp()) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
        return result;
    }

    const bool negate = (problem.obj_sense == model::ObjectiveSense::MAXIMIZE);

    model::Problem minimized = problem;
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
            double mu = compute_mu();
            if (check_convergence(minimized, mu)) {
                result.status = model::ProblemStatus::OPTIMAL;
                break;
            }

            compute_affine_step(minimized);
            double mu_aff = compute_mu();
            double sigma = (mu_aff / mu) * (mu_aff / mu) * (mu_aff / mu);
            sigma = std::clamp(sigma, 0.1, 0.9);

            compute_centering_step(minimized, mu, sigma);

            double alpha_p = compute_alpha(x_, dx_);
            double alpha_d = compute_alpha(s_, ds_);

            alpha_p = std::min(1.0, 0.99 * alpha_p);
            alpha_d = std::min(1.0, 0.99 * alpha_d);

            update_variables(alpha_p, alpha_d);
            result.iterations = iter + 1;
        }

        if (result.status == model::ProblemStatus::UNKNOWN) {
            result.status = had_interrupt ? model::ProblemStatus::INTERRUPTED
                                          : (time_up ? model::ProblemStatus::TIME_LIMIT
                                                     : model::ProblemStatus::ITER_LIMIT);
        }

        result.primal = x_;
        result.dual = y_;
        result.slack = s_;

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

    } catch (const std::exception& e) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
    }

    return result;
}

void InteriorPointQPSolver::initialize(const model::Problem& problem) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    x_.assign(nvars, 1.0);
    y_.assign(ncons, 0.0);
    z_.assign(nvars, 1.0);
    s_.assign(nvars, 1.0);

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

        x_[j] = std::max(x_[j], lb + 1e-4);
        x_[j] = std::min(x_[j], ub - 1e-4);
        s_[j] = 1.0;
        z_[j] = 1.0;
    }
}

bool InteriorPointQPSolver::check_convexity(const model::Problem& problem) {
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
        if (d < -std::max(options_.regularization, tol_.singular_tol())) return false;
    }
    return true;
}

double InteriorPointQPSolver::compute_mu() const {
    double sum = 0.0;
    for (std::size_t j = 0; j < x_.size(); ++j) {
        sum += x_[j] * z_[j] + s_[j] * z_[j];
    }
    return sum / (2.0 * x_.size());
}

void InteriorPointQPSolver::compute_affine_step(const model::Problem& problem) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();
    std::size_t total = nvars + ncons + nvars;

    std::vector<double> rhs(total, 0.0);
    form_kkt_system(problem, rhs);

    std::vector<double> sol(total, 0.0);
    solve_kkt_system(problem, rhs, sol);

    dx_.assign(nvars, 0.0);
    dy_.assign(ncons, 0.0);
    dz_.assign(nvars, 0.0);
    ds_.assign(nvars, 0.0);

    for (std::size_t j = 0; j < nvars; ++j) dx_[j] = sol[j];
    for (std::size_t i = 0; i < ncons; ++i) dy_[i] = sol[nvars + i];
    for (std::size_t j = 0; j < nvars; ++j) dz_[j] = sol[nvars + ncons + j];
    for (std::size_t j = 0; j < nvars; ++j) ds_[j] = (-x_[j] * dz_[j] - x_[j] * z_[j]) / z_[j];
}

void InteriorPointQPSolver::compute_centering_step(const model::Problem& problem, double mu, double sigma) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();
    std::size_t total = nvars + ncons + nvars;

    std::vector<double> rhs(total, 0.0);

    for (std::size_t j = 0; j < nvars; ++j) {
        rhs[j] = 0.0;
    }
    for (std::size_t i = 0; i < ncons; ++i) {
        rhs[nvars + i] = 0.0;
    }
    for (std::size_t j = 0; j < nvars; ++j) {
        rhs[nvars + ncons + j] = sigma * mu - x_[j] * z_[j];
    }

    std::vector<double> sol(total, 0.0);
    solve_kkt_system(problem, rhs, sol);

    dx_.assign(nvars, 0.0);
    dy_.assign(ncons, 0.0);
    dz_.assign(nvars, 0.0);
    ds_.assign(nvars, 0.0);

    for (std::size_t j = 0; j < nvars; ++j) dx_[j] = sol[j];
    for (std::size_t i = 0; i < ncons; ++i) dy_[i] = sol[nvars + i];
    for (std::size_t j = 0; j < nvars; ++j) dz_[j] = sol[nvars + ncons + j];
    for (std::size_t j = 0; j < nvars; ++j) ds_[j] = (-x_[j] * dz_[j] - x_[j] * z_[j] + sigma * mu) / z_[j];
}

void InteriorPointQPSolver::update_variables(double alpha_p, double alpha_d) {
    for (std::size_t j = 0; j < x_.size(); ++j) {
        x_[j] += alpha_p * dx_[j];
        s_[j] += alpha_p * ds_[j];
        z_[j] += alpha_d * dz_[j];
    }
    for (std::size_t i = 0; i < y_.size(); ++i) {
        y_[i] += alpha_d * dy_[i];
    }
}

bool InteriorPointQPSolver::check_convergence(const model::Problem& problem, double mu) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    double primal_residual = 0.0, dual_residual = 0.0;
    const auto& A = problem.constraint_matrix;

    for (std::size_t i = 0; i < ncons; ++i) {
        double primal_sum = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                primal_sum += A.values()[k] * x_[j];
            }
        }
        double rhs = problem.constraints[i].rhs;
        if (problem.constraints[i].sense == model::ConstraintSense::LE) {
            primal_residual = std::max(primal_residual, std::abs(rhs - primal_sum));
        } else if (problem.constraints[i].sense == model::ConstraintSense::GE) {
            primal_residual = std::max(primal_residual, std::abs(primal_sum - rhs));
        } else {
            primal_residual = std::max(primal_residual, std::abs(rhs - primal_sum));
        }
    }

    for (std::size_t j = 0; j < nvars; ++j) {
        double dual_sum = 0.0;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = 0; k < A.nnz(); ++k) {
                if (A.col_indices()[k] == j) {
                    std::size_t i = 0;
                    while (i < A.rows() && A.row_ptr()[i + 1] <= k) ++i;
                    if (i < A.rows()) dual_sum += A.values()[k] * y_[i];
                }
            }
        }

        double h_x = 0.0;
        for (const auto& term : problem.quadratic_terms) {
            if (term.row == j) h_x += term.coeff * x_[term.col];
            if (term.col == j && term.row != term.col) h_x += term.coeff * x_[term.row];
        }

        double target = problem.variables[j].objective_coeff;
        dual_residual = std::max(dual_residual, std::abs(target + h_x - dual_sum - z_[j]));
    }

    return primal_residual < options_.convergence_tol &&
           dual_residual < options_.convergence_tol &&
           mu < options_.complementarity_tol;
}

double InteriorPointQPSolver::compute_alpha(const std::vector<double>& vars, const std::vector<double>& dirs) {
    double alpha = 1.0;
    for (std::size_t i = 0; i < vars.size(); ++i) {
        if (dirs[i] < 0) {
            alpha = std::min(alpha, -vars[i] / dirs[i]);
        }
    }
    return alpha;
}

void InteriorPointQPSolver::form_kkt_system(const model::Problem& problem, std::vector<double>& rhs) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();

    rhs.resize(nvars + ncons + nvars, 0.0);

    for (std::size_t j = 0; j < nvars; ++j) {
        double h_x = 0.0;
        for (const auto& term : problem.quadratic_terms) {
            if (term.row == j) h_x += term.coeff * x_[term.col];
            if (term.col == j && term.row != term.col) h_x += term.coeff * x_[term.row];
        }
        rhs[j] = -(problem.variables[j].objective_coeff + h_x + z_[j]);
    }

    const auto& A = problem.constraint_matrix;
    for (std::size_t i = 0; i < ncons; ++i) {
        double sum = 0.0;
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            sum += A.values()[k] * x_[A.col_indices()[k]];
        }
        double target = problem.constraints[i].rhs;
        if (problem.constraints[i].sense == model::ConstraintSense::LE) {
            rhs[nvars + i] = -(target - sum);
        } else if (problem.constraints[i].sense == model::ConstraintSense::GE) {
            rhs[nvars + i] = -(sum - target);
        } else {
            rhs[nvars + i] = -(target - sum);
        }
    }

    for (std::size_t j = 0; j < nvars; ++j) {
        rhs[nvars + ncons + j] = -x_[j] * z_[j];
    }
}

void InteriorPointQPSolver::solve_kkt_system(const model::Problem& problem,
                                              const std::vector<double>& rhs,
                                              std::vector<double>& sol) {
    std::size_t nvars = problem.variables.size();
    std::size_t ncons = problem.constraints.size();
    std::size_t total = nvars + ncons + nvars;

    const auto& A = problem.constraint_matrix;

    std::vector<numerical::Triplet> triplets;
    triplets.reserve(nvars + 2 * problem.quadratic_terms.size() + 2 * A.nnz() + 3 * nvars);

    std::vector<double> h_diag(nvars, options_.regularization);
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

    for (std::size_t i = 0; i < ncons; ++i) {
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            std::size_t j = A.col_indices()[k];
            triplets.emplace_back(nvars + i, j, A.values()[k]);
            triplets.emplace_back(j, nvars + i, A.values()[k]);
        }
    }

    for (std::size_t j = 0; j < nvars; ++j) {
        triplets.emplace_back(j, nvars + ncons + j, 1.0);
        triplets.emplace_back(nvars + ncons + j, j, z_[j]);
        triplets.emplace_back(nvars + ncons + j, nvars + ncons + j, x_[j]);
    }

    numerical::SparseMatrix kkt_mat = numerical::SparseMatrix::from_triplets(total, total, triplets);
    kkt_factorization_ = numerical::create_factorization(numerical::FactorizationType::LU, kkt_mat, tol_);
    sol = rhs;
    kkt_factorization_->solve(sol);
    numerical::iterative_refinement(*kkt_factorization_, kkt_mat, sol, rhs);
}

} // namespace hypernova::qp