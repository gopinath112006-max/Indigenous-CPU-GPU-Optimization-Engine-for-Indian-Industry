#include "interior_point_qp.hpp"
#include "../numerical/refinement.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <numeric>
#include <utility>

namespace hypernova::qp {

namespace {
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr std::size_t NPOS = std::numeric_limits<std::size_t>::max();
constexpr double STEP_SAFETY = 0.995;
constexpr double MIN_BARRIER_VALUE = 1e-4;
} // namespace

InteriorPointQPSolver::InteriorPointQPSolver(const numerical::ToleranceConfig& tol, const InteriorPointQPOptions& options)
    : tol_(tol), options_(options) {}

InteriorPointQPResult InteriorPointQPSolver::solve(const model::Problem& problem) {
    auto start_time = std::chrono::high_resolution_clock::now();

    InteriorPointQPResult result;
    std::size_t nvars = problem.variables.size();

    if (nvars == 0 || (!problem.is_qp() && !problem.is_miqp() && !problem.is_lp())) {
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

    if (options_.check_convexity) {
        result.convexity = classify_qp_convexity(minimized, tol_);
        if (result.convexity == ConvexityClassification::NONCONVEX) {
            result.status = model::ProblemStatus::NUMERICAL_ERROR;
            return result;
        }
    }

    for (const auto& var : minimized.variables) {
        if (var.lower_bound > var.upper_bound + tol_.feasibility_tol()) {
            result.status = model::ProblemStatus::INFEASIBLE;
            return result;
        }
    }

    const std::size_t orig_ncons = minimized.constraints.size();

    // Fixed variables (lb == ub) cannot pass through the bound barrier: at a
    // feasible iterate both bound slacks (x - lb) and (ub - x) are zero, so
    // the KKT barrier-complementarity block gets a structural zero on the
    // diagonal and stays singular no matter how the regularization is raised.
    // Model each fixed variable as a free variable pinned by an explicit
    // equality row instead: the binding multiplier becomes a free-signed
    // equality dual and the barrier never sees a zero slack column.
    {
        std::vector<std::pair<std::size_t, double>> pins;
        for (std::size_t j = 0; j < minimized.variables.size(); ++j) {
            const auto& v = minimized.variables[j];
            if (v.lower_bound > -INF && v.upper_bound < INF &&
                v.upper_bound - v.lower_bound <= tol_.feasibility_tol()) {
                pins.emplace_back(j, v.lower_bound);
            }
        }
        if (!pins.empty()) {
            const auto& A = minimized.constraint_matrix;
            std::vector<numerical::Triplet> triplets;
            triplets.reserve(A.nnz() + pins.size());
            for (std::size_t i = 0; i < minimized.constraints.size(); ++i) {
                for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
                    triplets.emplace_back(i, A.col_indices()[t], A.values()[t]);
                }
            }
            for (const auto& pin : pins) {
                const std::size_t j = pin.first;
                const double value = pin.second;
                const std::size_t i = minimized.constraints.size();
                model::Constraint con;
                con.index = i;
                con.sense = model::ConstraintSense::EQ;
                con.rhs = value;
                con.name = "fixed_x" + std::to_string(j);
                minimized.constraints.push_back(con);
                triplets.emplace_back(i, j, 1.0);
                minimized.variables[j].lower_bound = -INF;
                minimized.variables[j].upper_bound = INF;
            }
            minimized.constraint_matrix =
                numerical::SparseMatrix::from_triplets(minimized.constraints.size(),
                                                       minimized.variables.size(),
                                                       triplets);
        }
    }

    try {
        sweep_total_ = 0;
        reg_retries_ = 0;
        final_regularization_ = options_.regularization;
        kkt_step_ok_ = true;

        initialize(minimized);

        bool time_up = false;
        bool had_interrupt = false;
        int stall_count = 0;

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
            if (!kkt_step_ok_) {
                kkt_step_ok_ = true;
                result.status = model::ProblemStatus::NUMERICAL_ERROR;
                break;
            }
            double sigma = compute_sigma(mu);
            compute_centering_step(minimized, mu, sigma);

            const double tau = (mu > 0.0) ? std::clamp(1.0 - mu, 0.95, 0.9995) : 1.0;
            double alpha_p = compute_alpha_primal(tau);
            double alpha_d = compute_alpha_dual(tau);

            if (alpha_p <= 1e-12 && alpha_d <= 1e-12) {
                ++stall_count;
            } else {
                stall_count = 0;
            }
            if (stall_count >= 5) {
                result.status = model::ProblemStatus::NUMERICAL_ERROR;
                break;
            }

            update_variables(alpha_p, alpha_d);

            bool nonfinite = false;
            for (std::size_t j = 0; j < x_.size() && !nonfinite; ++j) {
                if (!std::isfinite(x_[j])) nonfinite = true;
            }
            for (std::size_t j = 0; j < y_.size() && !nonfinite; ++j) {
                if (!std::isfinite(y_[j])) nonfinite = true;
            }
            if (nonfinite) {
                result.status = model::ProblemStatus::NUMERICAL_ERROR;
                break;
            }

            result.iterations = iter + 1;
        }

        if (result.status == model::ProblemStatus::UNKNOWN) {
            result.status = had_interrupt ? model::ProblemStatus::INTERRUPTED
                                          : (time_up ? model::ProblemStatus::TIME_LIMIT
                                                     : model::ProblemStatus::ITER_LIMIT);
        }

        const auto& A = minimized.constraint_matrix;

        // Compute reduced costs: rc_j = c_j + (Qx)_j + (A^T y)_j
        result.reduced_costs.assign(nvars, 0.0);
        for (std::size_t j = 0; j < nvars; ++j) {
            result.reduced_costs[j] = minimized.variables[j].objective_coeff;
        }
        for (const auto& term : minimized.quadratic_terms) {
            result.reduced_costs[term.row] += term.coeff * x_[term.col];
            if (term.col != term.row) {
                result.reduced_costs[term.col] += term.coeff * x_[term.row];
            }
        }
        for (std::size_t i = 0; i < orig_ncons; ++i) {
            double yi = y_[i];
            for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
                result.reduced_costs[A.col_indices()[t]] += A.values()[t] * yi;
            }
        }

        result.primal = x_;
        result.dual.assign(y_.begin(), y_.begin() + orig_ncons);
        result.refinement_sweeps = sweep_total_;
        result.regularization_retries = reg_retries_;

        // Compute slacks for original constraints
        result.slack.assign(orig_ncons, 0.0);
        for (std::size_t i = 0; i < orig_ncons; ++i) {
            double sum = 0.0;
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                sum += A.values()[k] * x_[A.col_indices()[k]];
            }
            if (problem.constraints[i].sense == model::ConstraintSense::LE) {
                result.slack[i] = problem.constraints[i].rhs - sum;
            } else if (problem.constraints[i].sense == model::ConstraintSense::GE) {
                result.slack[i] = sum - problem.constraints[i].rhs;
            }
        }

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
            for (auto& rc : result.reduced_costs) rc = -rc;
        }

        // Final independent solution validation (Section 13)
        double max_bnd_viol = 0.0;
        bool numerical_ok = true;
        for (std::size_t j = 0; j < nvars; ++j) {
            if (!std::isfinite(result.primal[j])) {
                numerical_ok = false;
                break;
            }
            double lb = problem.variables[j].lower_bound;
            double ub = problem.variables[j].upper_bound;
            if (lb > -INF && result.primal[j] < lb - tol_.feasibility_tol()) {
                max_bnd_viol = std::max(max_bnd_viol, lb - result.primal[j]);
            }
            if (ub < INF && result.primal[j] > ub + tol_.feasibility_tol()) {
                max_bnd_viol = std::max(max_bnd_viol, result.primal[j] - ub);
            }
        }
        result.max_bound_violation = max_bnd_viol;

        if (!numerical_ok || !std::isfinite(result.objective_value)) {
            result.status = model::ProblemStatus::NUMERICAL_ERROR;
        } else if (result.status == model::ProblemStatus::OPTIMAL) {
            if (max_bnd_viol > tol_.feasibility_tol()) {
                result.status = model::ProblemStatus::SUBOPTIMAL;
            } else {
                double max_con_viol = 0.0;
                for (std::size_t i = 0; i < orig_ncons; ++i) {
                    double act = 0.0;
                    for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                        act += A.values()[k] * result.primal[A.col_indices()[k]];
                    }
                    double rhs = problem.constraints[i].rhs;
                    if (problem.constraints[i].sense == model::ConstraintSense::LE && act > rhs + tol_.feasibility_tol()) {
                        max_con_viol = std::max(max_con_viol, act - rhs);
                    } else if (problem.constraints[i].sense == model::ConstraintSense::GE && act < rhs - tol_.feasibility_tol()) {
                        max_con_viol = std::max(max_con_viol, rhs - act);
                    } else if (problem.constraints[i].sense == model::ConstraintSense::EQ && std::abs(act - rhs) > tol_.feasibility_tol()) {
                        max_con_viol = std::max(max_con_viol, std::abs(act - rhs));
                    }
                }
                result.primal_residual = std::max(max_bnd_viol, max_con_viol);
                if (max_con_viol > tol_.feasibility_tol()) {
                    result.status = model::ProblemStatus::SUBOPTIMAL;
                }
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    } catch (const std::exception& e) {
        (void)e;
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
    }

    return result;
}

void InteriorPointQPSolver::initialize(const model::Problem& problem) {
    nvars_ = problem.variables.size();
    ncons_ = problem.constraints.size();

    lb_var_.clear();
    lb_val_.clear();
    ub_var_.clear();
    ub_val_.clear();
    lb_idx_of_.assign(nvars_, NPOS);
    ub_idx_of_.assign(nvars_, NPOS);

    for (std::size_t j = 0; j < nvars_; ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        if (lb > -INF) {
            lb_idx_of_[j] = lb_var_.size();
            lb_var_.push_back(j);
            lb_val_.push_back(lb);
        }
        if (ub < INF) {
            ub_idx_of_[j] = ub_var_.size();
            ub_var_.push_back(j);
            ub_val_.push_back(ub);
        }
    }
    n_lb_ = lb_var_.size();
    n_ub_ = ub_var_.size();

    ineq_row_.clear();
    ineq_sigma_.clear();
    ineq_idx_of_.assign(ncons_, NPOS);
    for (std::size_t i = 0; i < ncons_; ++i) {
        if (problem.constraints[i].sense == model::ConstraintSense::LE) {
            ineq_idx_of_[i] = ineq_row_.size();
            ineq_row_.push_back(i);
            ineq_sigma_.push_back(1);
        } else if (problem.constraints[i].sense == model::ConstraintSense::GE) {
            ineq_idx_of_[i] = ineq_row_.size();
            ineq_row_.push_back(i);
            ineq_sigma_.push_back(-1);
        }
    }
    n_ineq_ = ineq_row_.size();

    x_.assign(nvars_, 0.0);
    for (std::size_t j = 0; j < nvars_; ++j) {
        double lb = problem.variables[j].lower_bound;
        double ub = problem.variables[j].upper_bound;
        double xj = 0.0;
        if (lb > -INF && ub < INF) {
            xj = (lb + ub) / 2.0;
        } else if (lb > -INF) {
            xj = lb + std::max(1.0, 0.1 * std::abs(lb));
        } else if (ub < INF) {
            xj = ub - std::max(1.0, 0.1 * std::abs(ub));
        } else {
            xj = 0.0;
        }
        if (lb > -INF && ub < INF) {
            if (ub - lb > 2e-4) {
                xj = std::clamp(xj, lb + 1e-4, ub - 1e-4);
            } else {
                xj = (lb + ub) / 2.0;
            }
        } else {
            if (lb > -INF) xj = std::max(xj, lb + 1e-4);
            if (ub < INF) xj = std::min(xj, ub - 1e-4);
        }
        x_[j] = xj;
    }

    sl_.assign(n_lb_, 1.0);
    su_.assign(n_ub_, 1.0);
    g_.assign(n_ineq_, 1.0);
    lambdal_.assign(n_lb_, 1.0);
    lambdau_.assign(n_ub_, 1.0);
    lambdag_.assign(n_ineq_, 1.0);

    // Initial objective gradient to scale dual variables
    std::vector<double> grad0(nvars_, 0.0);
    for (std::size_t j = 0; j < nvars_; ++j) {
        grad0[j] = problem.variables[j].objective_coeff;
    }
    for (const auto& term : problem.quadratic_terms) {
        grad0[term.row] += term.coeff * x_[term.col];
        if (term.col != term.row) grad0[term.col] += term.coeff * x_[term.row];
    }

    for (std::size_t l = 0; l < n_lb_; ++l) {
        std::size_t j = lb_var_[l];
        double s = x_[j] - lb_val_[l];
        sl_[l] = std::max(s, MIN_BARRIER_VALUE);
        lambdal_[l] = std::max(1.0, grad0[j] > 0.0 ? grad0[j] : 1.0);
    }
    for (std::size_t u = 0; u < n_ub_; ++u) {
        std::size_t j = ub_var_[u];
        double s = ub_val_[u] - x_[j];
        su_[u] = std::max(s, MIN_BARRIER_VALUE);
        lambdau_[u] = std::max(1.0, grad0[j] < 0.0 ? -grad0[j] : 1.0);
    }
    const auto& A = problem.constraint_matrix;
    for (std::size_t k = 0; k < n_ineq_; ++k) {
        std::size_t i = ineq_row_[k];
        double sum = 0.0;
        for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
            sum += A.values()[t] * x_[A.col_indices()[t]];
        }
        double gval = (ineq_sigma_[k] > 0) ? (problem.constraints[i].rhs - sum)
                                           : (sum - problem.constraints[i].rhs);
        g_[k] = std::max(gval, MIN_BARRIER_VALUE);
    }

    y_.assign(ncons_, 0.0);

    dx_.assign(nvars_, 0.0);
    dy_.assign(ncons_, 0.0);
    dsl_.assign(n_lb_, 0.0);
    dsu_.assign(n_ub_, 0.0);
    dg_.assign(n_ineq_, 0.0);
    dlambdal_.assign(n_lb_, 0.0);
    dlambdau_.assign(n_ub_, 0.0);
    dlambdag_.assign(n_ineq_, 0.0);
}



double InteriorPointQPSolver::compute_mu() const {
    const std::size_t k = comp_count();
    if (k == 0) return 0.0;
    double sum = 0.0;
    for (std::size_t l = 0; l < n_lb_; ++l) sum += sl_[l] * lambdal_[l];
    for (std::size_t u = 0; u < n_ub_; ++u) sum += su_[u] * lambdau_[u];
    for (std::size_t g = 0; g < n_ineq_; ++g) sum += g_[g] * lambdag_[g];
    return sum / static_cast<double>(k);
}

double InteriorPointQPSolver::compute_sigma(double mu) const {
    const std::size_t k = comp_count();
    if (k == 0 || mu <= 0.0) return 0.0;

    double ap = compute_alpha_pair(sl_, dsl_);
    ap = std::min(ap, compute_alpha_pair(su_, dsu_));
    ap = std::min(ap, compute_alpha_pair(g_, dg_));
    ap = std::min(ap, 1.0);

    double ad = compute_alpha_pair(lambdal_, dlambdal_);
    ad = std::min(ad, compute_alpha_pair(lambdau_, dlambdau_));
    ad = std::min(ad, compute_alpha_pair(lambdag_, dlambdag_));
    ad = std::min(ad, 1.0);

    double sum = 0.0;
    for (std::size_t l = 0; l < n_lb_; ++l) {
        sum += (sl_[l] + ap * dsl_[l]) * (lambdal_[l] + ad * dlambdal_[l]);
    }
    for (std::size_t u = 0; u < n_ub_; ++u) {
        sum += (su_[u] + ap * dsu_[u]) * (lambdau_[u] + ad * dlambdau_[u]);
    }
    for (std::size_t g = 0; g < n_ineq_; ++g) {
        sum += (g_[g] + ap * dg_[g]) * (lambdag_[g] + ad * dlambdag_[g]);
    }
    double mu_aff = sum / static_cast<double>(k);

    double ratio = mu_aff / mu;
    double sigma = ratio * ratio * ratio;
    return std::clamp(sigma, 0.1, 0.9);
}

void InteriorPointQPSolver::assemble_kkt(const model::Problem& problem, double regularization) {
    const std::size_t n = nvars_;
    const std::size_t nc = ncons_;
    const std::size_t pl = n_lb_;
    const std::size_t pu = n_ub_;
    const std::size_t pg = n_ineq_;
    const std::size_t dyn = n + nc;
    const std::size_t dsl_off = dyn;
    const std::size_t dsu_off = dyn + pl;
    const std::size_t dg_off = dyn + pl + pu;
    const std::size_t dl_off = dyn + pl + pu + pg;
    const std::size_t du_off = dl_off + pl;
    const std::size_t dgdual_off = du_off + pu;
    const std::size_t N = kkt_size();

    std::vector<numerical::Triplet> tris;
    tris.reserve(n + 2 * problem.quadratic_terms.size() + 2 * problem.constraint_matrix.nnz() +
                  4 * (pl + pu + pg) + 2 * pg);

    // H + regularization*I on the primal block; the diagonal regularization is
    // the mechanism used to keep an ill-conditioned KKT solvable.
    std::vector<double> h_diag(n, regularization);
    for (const auto& term : problem.quadratic_terms) {
        if (term.row == term.col) {
            h_diag[term.row] += term.coeff;
        } else {
            tris.emplace_back(term.row, term.col, term.coeff);
            tris.emplace_back(term.col, term.row, term.coeff);
        }
    }

    const auto& A = problem.constraint_matrix;

    // Stationarity-x rows: H, A^T, -lambdal, +lambdau.
    for (std::size_t j = 0; j < n; ++j) {
        tris.emplace_back(j, j, h_diag[j]);
    }
    for (std::size_t i = 0; i < nc; ++i) {
        for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
            std::size_t j = A.col_indices()[t];
            double v = A.values()[t];
            tris.emplace_back(j, n + i, v);      // Rx row j w.r.t. dy_i
            tris.emplace_back(n + i, j, v);      // Rc row i w.r.t. dx_j
        }
    }
    for (std::size_t l = 0; l < pl; ++l) {
        tris.emplace_back(lb_var_[l], dl_off + l, -1.0);
    }
    for (std::size_t u = 0; u < pu; ++u) {
        tris.emplace_back(ub_var_[u], du_off + u, 1.0);
    }

    // Constraint rows: +sigma on the slack g.
    for (std::size_t k = 0; k < pg; ++k) {
        std::size_t i = ineq_row_[k];
        tris.emplace_back(n + i, dg_off + k, static_cast<double>(ineq_sigma_[k]));
    }

    // Bound-definition rows: sl - x = -lb, su + x = ub.
    for (std::size_t l = 0; l < pl; ++l) {
        tris.emplace_back(dyn + l, lb_var_[l], -1.0);
        tris.emplace_back(dyn + l, dsl_off + l, 1.0);
    }
    for (std::size_t u = 0; u < pu; ++u) {
        tris.emplace_back(dyn + pl + u, ub_var_[u], 1.0);
        tris.emplace_back(dyn + pl + u, dsu_off + u, 1.0);
    }

    // Slack-g stationarity rows: sigma*y - lambdag = 0.
    for (std::size_t k = 0; k < pg; ++k) {
        std::size_t i = ineq_row_[k];
        tris.emplace_back(dyn + pl + pu + k, n + i, static_cast<double>(ineq_sigma_[k]));
        tris.emplace_back(dyn + pl + pu + k, dgdual_off + k, -1.0);
    }

    // Barrier complementarity rows.
    for (std::size_t l = 0; l < pl; ++l) {
        tris.emplace_back(dl_off + l, dsl_off + l, lambdal_[l]);
        tris.emplace_back(dl_off + l, dl_off + l, sl_[l]);
    }
    for (std::size_t u = 0; u < pu; ++u) {
        tris.emplace_back(du_off + u, dsu_off + u, lambdau_[u]);
        tris.emplace_back(du_off + u, du_off + u, su_[u]);
    }
    for (std::size_t g = 0; g < pg; ++g) {
        tris.emplace_back(dgdual_off + g, dg_off + g, lambdag_[g]);
        tris.emplace_back(dgdual_off + g, dgdual_off + g, g_[g]);
    }

    kkt_mat_ = numerical::SparseMatrix::from_triplets(N, N, tris);
}

void InteriorPointQPSolver::form_affine_rhs(const model::Problem& problem, std::vector<double>& rhs) const {
    const std::size_t n = nvars_;
    const std::size_t nc = ncons_;
    const std::size_t pl = n_lb_;
    const std::size_t pu = n_ub_;
    const std::size_t pg = n_ineq_;
    const std::size_t dyn = n + nc;
    const std::size_t dl_off = dyn + pl + pu + pg;
    const std::size_t du_off = dl_off + pl;
    const std::size_t dgdual_off = du_off + pu;

    rhs.assign(kkt_size(), 0.0);

    std::vector<double> grad(n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        grad[j] = problem.variables[j].objective_coeff;
    }
    for (const auto& term : problem.quadratic_terms) {
        grad[term.row] += term.coeff * x_[term.col];
        if (term.col != term.row) grad[term.col] += term.coeff * x_[term.row];
    }

    const auto& A = problem.constraint_matrix;
    for (std::size_t i = 0; i < nc; ++i) {
        double yi = y_[i];
        for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
            grad[A.col_indices()[t]] += A.values()[t] * yi;
        }
    }

    for (std::size_t j = 0; j < n; ++j) {
        double g = grad[j];
        if (lb_idx_of_[j] != NPOS) g -= lambdal_[lb_idx_of_[j]];
        if (ub_idx_of_[j] != NPOS) g += lambdau_[ub_idx_of_[j]];
        rhs[j] = -g;
    }

    for (std::size_t i = 0; i < nc; ++i) {
        double sum = 0.0;
        for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
            sum += A.values()[t] * x_[A.col_indices()[t]];
        }
        if (ineq_idx_of_[i] != NPOS) {
            std::size_t k = ineq_idx_of_[i];
            sum += static_cast<double>(ineq_sigma_[k]) * g_[k];
        }
        rhs[n + i] = -(sum - problem.constraints[i].rhs);
    }

    for (std::size_t l = 0; l < pl; ++l) {
        rhs[dyn + l] = -sl_[l] + x_[lb_var_[l]] - lb_val_[l];
    }
    for (std::size_t u = 0; u < pu; ++u) {
        rhs[dyn + pl + u] = -su_[u] - x_[ub_var_[u]] + ub_val_[u];
    }
    for (std::size_t k = 0; k < pg; ++k) {
        std::size_t i = ineq_row_[k];
        rhs[dyn + pl + pu + k] = -static_cast<double>(ineq_sigma_[k]) * y_[i] + lambdag_[k];
    }

    for (std::size_t l = 0; l < pl; ++l) {
        rhs[dl_off + l] = -sl_[l] * lambdal_[l];
    }
    for (std::size_t u = 0; u < pu; ++u) {
        rhs[du_off + u] = -su_[u] * lambdau_[u];
    }
    for (std::size_t g = 0; g < pg; ++g) {
        rhs[dgdual_off + g] = -g_[g] * lambdag_[g];
    }
}

void InteriorPointQPSolver::unpack_directions(const std::vector<double>& sol) {
    const std::size_t n = nvars_;
    const std::size_t nc = ncons_;
    const std::size_t pl = n_lb_;
    const std::size_t pu = n_ub_;
    const std::size_t pg = n_ineq_;
    const std::size_t dyn = n + nc;
    const std::size_t dsl_off = dyn;
    const std::size_t dsu_off = dyn + pl;
    const std::size_t dg_off = dyn + pl + pu;
    const std::size_t dl_off = dyn + pl + pu + pg;
    const std::size_t du_off = dl_off + pl;
    const std::size_t dgdual_off = du_off + pu;

    dx_.assign(sol.begin(), sol.begin() + n);
    dy_.assign(sol.begin() + n, sol.begin() + dyn);
    dsl_.assign(sol.begin() + dsl_off, sol.begin() + dsu_off);
    dsu_.assign(sol.begin() + dsu_off, sol.begin() + dg_off);
    dg_.assign(sol.begin() + dg_off, sol.begin() + dl_off);
    dlambdal_.assign(sol.begin() + dl_off, sol.begin() + du_off);
    dlambdau_.assign(sol.begin() + du_off, sol.begin() + dgdual_off);
    dlambdag_.assign(sol.begin() + dgdual_off, sol.end());
}

void InteriorPointQPSolver::compute_affine_step(const model::Problem& problem) {
    // Adaptive H + delta*I regularization: start at options_.regularization and,
    // if the KKT factorization is singular or iterative refinement cannot drive
    // the relative residual down, bump delta (capped at regularization_max) and
    // re-factorize.  A large static delta is never used; the residual check
    // verifies each accepted solve.
    double reg = options_.regularization;
    const std::size_t max_attempts = 8;
    for (std::size_t attempt = 0; attempt < max_attempts; ++attempt) {
        assemble_kkt(problem, reg);

        std::vector<double> rhs(kkt_size(), 0.0);
        form_affine_rhs(problem, rhs);
        std::vector<double> sol = rhs;

        kkt_factorization_ = numerical::create_factorization(numerical::FactorizationType::LU, kkt_mat_, tol_);
        if (kkt_factorization_) {
            kkt_factorization_->solve(sol);
            sweep_total_ += static_cast<std::size_t>(std::max(
                0, numerical::iterative_refinement(*kkt_factorization_, kkt_mat_, sol, rhs, 1e-10, 5)));

            // Residual verification: accept the solve only when the relative
            // max-norm residual is small.  The LU "singular" flag is expected
            // to trip benignly on the saddle KKT as the barrier tightens, so it
            // is not a rejection signal on its own; a residual that refuses to
            // converge is.
            if (kkt_relative_residual(sol, rhs) <= 1e-8) {
                final_regularization_ = reg;
                unpack_directions(sol);
                return;
            }
        }

        if (reg >= options_.regularization_max) break;
        const double bumped = std::min(reg * 10.0, options_.regularization_max);
        ++reg_retries_;
        if (std::getenv("HYPERNOVA_QP_DBG")) {
            std::cerr << "[qp-ipm] KKT residual not converged"
                      << " (singular=" << (kkt_factorization_ && kkt_factorization_->stats().singular ? 1 : 0)
                      << ") regularization " << reg << " -> " << bumped << "\n";
        }
        reg = bumped;
    }

    kkt_step_ok_ = false;
}

double InteriorPointQPSolver::kkt_relative_residual(const std::vector<double>& sol,
                                                    const std::vector<double>& rhs) const {
    if (rhs.empty()) return 0.0;
    std::vector<double> kx = kkt_mat_.multiply(sol);
    double max_res = 0.0;
    double max_rhs = 0.0;
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        max_res = std::max(max_res, std::abs(kx[i] - rhs[i]));
        max_rhs = std::max(max_rhs, std::abs(rhs[i]));
    }
    return max_res / std::max(1e-300, max_rhs);
}

void InteriorPointQPSolver::compute_centering_step(const model::Problem& problem, double mu, double sigma) {
    const std::size_t pl = n_lb_;
    const std::size_t pu = n_ub_;
    const std::size_t pg = n_ineq_;
    const std::size_t dyn = nvars_ + ncons_;
    const std::size_t dl_off = dyn + pl + pu + pg;
    const std::size_t du_off = dl_off + pl;
    const std::size_t dgdual_off = du_off + pu;

    std::vector<double> rhs(kkt_size(), 0.0);
    form_affine_rhs(problem, rhs);

    // Mehrotra corrector: complementarity RHS becomes
    // -s*z - ds_aff*dz_aff + sigma*mu.
    for (std::size_t l = 0; l < pl; ++l) {
        rhs[dl_off + l] += sigma * mu - dsl_[l] * dlambdal_[l];
    }
    for (std::size_t u = 0; u < pu; ++u) {
        rhs[du_off + u] += sigma * mu - dsu_[u] * dlambdau_[u];
    }
    for (std::size_t g = 0; g < pg; ++g) {
        rhs[dgdual_off + g] += sigma * mu - dg_[g] * dlambdag_[g];
    }

    std::vector<double> sol = rhs;
    if (kkt_factorization_) {
        kkt_factorization_->solve(sol);
        sweep_total_ += static_cast<std::size_t>(std::max(
            0, numerical::iterative_refinement(*kkt_factorization_, kkt_mat_, sol, rhs, 1e-10, 5)));
        unpack_directions(sol);
    } else {
        kkt_step_ok_ = false;
    }
}

double InteriorPointQPSolver::compute_alpha_pair(const std::vector<double>& vals, const std::vector<double>& dirs) const {
    double alpha = 1.0;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        if (dirs[i] < -1e-15) {
            double v = std::max(vals[i], 1e-15);
            double ratio = -v / dirs[i];
            alpha = std::min(alpha, ratio);
        }
    }
    return alpha;
}

double InteriorPointQPSolver::compute_alpha_primal(double tau) const {
    double alpha = 1.0;
    alpha = std::min(alpha, compute_alpha_pair(sl_, dsl_));
    alpha = std::min(alpha, compute_alpha_pair(su_, dsu_));
    alpha = std::min(alpha, compute_alpha_pair(g_, dg_));

    // Directly protect x_ against stepping across finite lower and upper bounds
    for (std::size_t l = 0; l < n_lb_; ++l) {
        std::size_t j = lb_var_[l];
        if (dx_[j] < -1e-15) {
            double dist = std::max(x_[j] - lb_val_[l], 1e-15);
            alpha = std::min(alpha, dist / (-dx_[j]));
        }
    }
    for (std::size_t u = 0; u < n_ub_; ++u) {
        std::size_t j = ub_var_[u];
        if (dx_[j] > 1e-15) {
            double dist = std::max(ub_val_[u] - x_[j], 1e-15);
            alpha = std::min(alpha, dist / dx_[j]);
        }
    }

    if (comp_count() == 0) return 1.0;
    return std::min(1.0, tau * alpha);
}

double InteriorPointQPSolver::compute_alpha_dual(double tau) const {
    double alpha = 1.0;
    alpha = std::min(alpha, compute_alpha_pair(lambdal_, dlambdal_));
    alpha = std::min(alpha, compute_alpha_pair(lambdau_, dlambdau_));
    alpha = std::min(alpha, compute_alpha_pair(lambdag_, dlambdag_));
    if (comp_count() == 0) return 1.0;
    return std::min(1.0, tau * alpha);
}

void InteriorPointQPSolver::update_variables(double alpha_p, double alpha_d) {
    for (std::size_t j = 0; j < x_.size(); ++j) {
        x_[j] += alpha_p * dx_[j];
    }
    for (std::size_t l = 0; l < sl_.size(); ++l) {
        sl_[l] += alpha_p * dsl_[l];
        if (sl_[l] < 1e-14) sl_[l] = 1e-14;
    }
    for (std::size_t u = 0; u < su_.size(); ++u) {
        su_[u] += alpha_p * dsu_[u];
        if (su_[u] < 1e-14) su_[u] = 1e-14;
    }
    for (std::size_t g = 0; g < g_.size(); ++g) {
        g_[g] += alpha_p * dg_[g];
        if (g_[g] < 1e-14) g_[g] = 1e-14;
    }
    for (std::size_t i = 0; i < y_.size(); ++i) {
        y_[i] += alpha_d * dy_[i];
    }
    for (std::size_t l = 0; l < lambdal_.size(); ++l) {
        lambdal_[l] += alpha_d * dlambdal_[l];
        if (lambdal_[l] < 1e-14) lambdal_[l] = 1e-14;
    }
    for (std::size_t u = 0; u < lambdau_.size(); ++u) {
        lambdau_[u] += alpha_d * dlambdau_[u];
        if (lambdau_[u] < 1e-14) lambdau_[u] = 1e-14;
    }
    for (std::size_t g = 0; g < lambdag_.size(); ++g) {
        lambdag_[g] += alpha_d * dlambdag_[g];
        if (lambdag_[g] < 1e-14) lambdag_[g] = 1e-14;
    }
}

bool InteriorPointQPSolver::check_convergence(const model::Problem& problem, double mu) {
    const std::size_t n = nvars_;
    const std::size_t nc = ncons_;
    const auto& A = problem.constraint_matrix;

    double primal_res = 0.0;
    double dual_res = 0.0;
    double comp_res = 0.0;

    for (std::size_t i = 0; i < nc; ++i) {
        double sum = 0.0;
        for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
            sum += A.values()[t] * x_[A.col_indices()[t]];
        }
        if (ineq_idx_of_[i] != NPOS) {
            std::size_t k = ineq_idx_of_[i];
            sum += static_cast<double>(ineq_sigma_[k]) * g_[k];
        }
        primal_res = std::max(primal_res, std::abs(sum - problem.constraints[i].rhs));
    }
    for (std::size_t l = 0; l < n_lb_; ++l) {
        primal_res = std::max(primal_res,
                              std::abs(x_[lb_var_[l]] - lb_val_[l] - sl_[l]));
        comp_res = std::max(comp_res, sl_[l] * lambdal_[l]);
    }
    for (std::size_t u = 0; u < n_ub_; ++u) {
        primal_res = std::max(primal_res,
                              std::abs(ub_val_[u] - x_[ub_var_[u]] - su_[u]));
        comp_res = std::max(comp_res, su_[u] * lambdau_[u]);
    }

    std::vector<double> grad(n, 0.0);
    double max_obj_scale = 1.0;
    for (std::size_t j = 0; j < n; ++j) {
        grad[j] = problem.variables[j].objective_coeff;
        max_obj_scale = std::max(max_obj_scale, std::abs(grad[j]));
    }
    for (const auto& term : problem.quadratic_terms) {
        grad[term.row] += term.coeff * x_[term.col];
        if (term.col != term.row) grad[term.col] += term.coeff * x_[term.row];
        max_obj_scale = std::max(max_obj_scale, std::abs(term.coeff));
    }
    for (std::size_t j = 0; j < n; ++j) {
        max_obj_scale = std::max(max_obj_scale, std::abs(grad[j]));
    }
    for (std::size_t i = 0; i < nc; ++i) {
        double yi = y_[i];
        for (std::size_t t = A.row_ptr()[i]; t < A.row_ptr()[i + 1]; ++t) {
            grad[A.col_indices()[t]] += A.values()[t] * yi;
        }
    }
    for (std::size_t j = 0; j < n; ++j) {
        if (lb_idx_of_[j] != NPOS) grad[j] -= lambdal_[lb_idx_of_[j]];
        if (ub_idx_of_[j] != NPOS) grad[j] += lambdau_[ub_idx_of_[j]];
        dual_res = std::max(dual_res, std::abs(grad[j]));
    }
    for (std::size_t k = 0; k < n_ineq_; ++k) {
        std::size_t i = ineq_row_[k];
        dual_res = std::max(dual_res,
                            std::abs(static_cast<double>(ineq_sigma_[k]) * y_[i] - lambdag_[k]));
        comp_res = std::max(comp_res, g_[k] * lambdag_[k]);
    }

    const double tol_p = options_.convergence_tol;
    const double tol_d = options_.convergence_tol * max_obj_scale;
    const double tol_mu = options_.complementarity_tol * max_obj_scale;
    const double tol_comp = options_.convergence_tol * max_obj_scale;

    return primal_res < tol_p &&
           dual_res < tol_d &&
           mu < tol_mu &&
           comp_res < tol_comp;
}

} // namespace hypernova::qp