#include "interior_point.hpp"
#include "simplex.hpp"
#include "../numerical/refinement.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace hypernova::lp {

namespace {

struct StandardForm {
    numerical::SparseMatrix M;
    std::vector<double> rhs;
    std::vector<double> c;
    std::vector<double> col_scale;
    double offset = 0.0;
    std::size_t m_val = 0;
    std::size_t n_val = 0;

    std::size_t m() const { return m_val; }
    std::size_t n() const { return n_val; }

    struct VarInfo {
        std::vector<int> slots;
        std::vector<double> signs;
        std::vector<double> shifts;
        double fixed = 0.0;
        bool is_fixed = false;
    };
    std::vector<VarInfo> var_info;
};

void matvec(const numerical::SparseMatrix& M, const std::vector<double>& x, std::vector<double>& out) {
    out.assign(M.rows(), 0.0);
    for (std::size_t i = 0; i < M.rows(); ++i) {
        for (std::size_t k = M.row_ptr()[i]; k < M.row_ptr()[i + 1]; ++k) {
            out[i] += M.values()[k] * x[M.col_indices()[k]];
        }
    }
}

std::vector<double> matvec_t(const numerical::SparseMatrix& M, const std::vector<double>& x) {
    std::vector<double> out(M.cols(), 0.0);
    for (std::size_t i = 0; i < M.rows(); ++i) {
        for (std::size_t k = M.row_ptr()[i]; k < M.row_ptr()[i + 1]; ++k) {
            out[M.col_indices()[k]] += M.values()[k] * x[i];
        }
    }
    return out;
}

double max_abs(const std::vector<double>& v) {
    double best = 0.0;
    for (double val : v) best = std::max(best, std::abs(val));
    return best;
}

model::Problem remove_duplicate_rows(const model::Problem& problem) {
    const auto& A = problem.constraint_matrix;
    const std::size_t n = problem.variables.size();
    if (A.order() != numerical::StorageOrder::CSR || A.rows() == 0) return problem;

    std::vector<model::Constraint> kept;
    kept.reserve(A.rows());
    std::vector<numerical::Triplet> triplets;
    std::unordered_map<std::string, std::size_t> seen;
    seen.reserve(A.rows());

    for (std::size_t i = 0; i < A.rows(); ++i) {
        std::vector<std::pair<std::size_t, double>> ent;
        ent.reserve(A.row_ptr()[i + 1] - A.row_ptr()[i]);
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            ent.emplace_back(A.col_indices()[k], A.values()[k]);
        }
        std::sort(ent.begin(), ent.end());

        std::string key;
        for (const auto& e : ent) {
            std::uint64_t bv;
            std::memcpy(&bv, &e.second, sizeof(double));
            key += std::to_string(e.first);
            key += ':';
            key += std::to_string(bv);
            key += ';';
        }
        key += '|';
        key += std::to_string(static_cast<int>(problem.constraints[i].sense));
        key += ':';
        std::uint64_t br;
        std::memcpy(&br, &problem.constraints[i].rhs, sizeof(double));
        key += std::to_string(br);

        if (seen.find(key) != seen.end()) continue;
        seen.emplace(key, i);
        kept.push_back(problem.constraints[i]);
        for (const auto& e : ent) {
            triplets.emplace_back(kept.size() - 1, e.first, e.second);
        }
    }

    if (kept.size() == A.rows()) return problem;

    model::Problem out = problem;
    out.constraints = std::move(kept);
    out.constraint_matrix =
        numerical::SparseMatrix::from_triplets(out.constraints.size(), n, triplets);
    return out;
}

double max_step(const std::vector<double>& v, const std::vector<double>& d) {
    double alpha = 1.0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (d[i] < 0.0) {
            alpha = std::min(alpha, -v[i] / d[i]);
        }
    }
    return std::min(1.0, 0.99 * alpha);
}

numerical::SparseMatrix build_normal_equations(
    const numerical::SparseMatrix& M,
    const std::vector<double>& D,
    double regularization,
    const std::function<bool()>& interrupt = {}) {

    const std::size_t m = M.rows();
    const std::size_t n = M.cols();

    std::vector<std::size_t> col_nnz(n, 0);
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t k = M.row_ptr()[i]; k < M.row_ptr()[i + 1]; ++k) {
            ++col_nnz[M.col_indices()[k]];
        }
    }

    std::vector<std::size_t> col_start(n + 1, 0);
    for (std::size_t j = 0; j < n; ++j) col_start[j + 1] = col_start[j] + col_nnz[j];

    std::vector<std::size_t> crows(col_start[n]);
    std::vector<double> cvals(col_start[n]);
    std::vector<std::size_t> cursor(col_start.begin(), col_start.end() - 1);
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t k = M.row_ptr()[i]; k < M.row_ptr()[i + 1]; ++k) {
            const std::size_t j = M.col_indices()[k];
            crows[cursor[j]] = i;
            cvals[cursor[j]] = M.values()[k];
            ++cursor[j];
        }
    }

    std::vector<numerical::Triplet> triplets;
    for (std::size_t j = 0; j < n; ++j) {
        if (interrupt && (j & 63u) == 0u && interrupt()) break;
        if (D[j] <= 0.0) continue;
        const double d = D[j];
        for (std::size_t p = col_start[j]; p < col_start[j + 1]; ++p) {
            if (interrupt && interrupt()) return numerical::SparseMatrix::from_triplets(m, m, triplets);
            for (std::size_t q = col_start[j]; q < col_start[j + 1]; ++q) {
                triplets.emplace_back(crows[p], crows[q], cvals[p] * cvals[q] * d);
            }
        }
    }

    for (std::size_t i = 0; i < m; ++i) {
        triplets.emplace_back(i, i, regularization);
    }

    return numerical::SparseMatrix::from_triplets(m, m, triplets);
}

StandardForm build_standard_form(const model::Problem& problem, const numerical::ToleranceConfig& tol) {
    StandardForm f;
    const auto& A = problem.constraint_matrix;
    const std::size_t n = problem.variables.size();
    const std::size_t norig = problem.constraints.size();

    const bool negate = (problem.obj_sense == model::ObjectiveSense::MAXIMIZE);
    std::vector<double> cbase(n);
    for (std::size_t j = 0; j < n; ++j) {
        cbase[j] = negate ? -problem.variables[j].objective_coeff
                          : problem.variables[j].objective_coeff;
    }

    f.var_info.resize(n);
    std::size_t nslots = 0;
    std::size_t ntle = 0;
    std::size_t m_ub = 0;

    for (std::size_t j = 0; j < n; ++j) {
        const double lb = problem.variables[j].lower_bound;
        const double ub = problem.variables[j].upper_bound;

        if (std::isfinite(lb) && std::isfinite(ub) && std::abs(ub - lb) <= tol.feasibility_tol()) {
            f.var_info[j].is_fixed = true;
            f.var_info[j].fixed = lb;
            f.offset += cbase[j] * lb;
        } else if (std::isfinite(lb)) {
            f.var_info[j].slots.push_back(static_cast<int>(nslots));
            f.var_info[j].signs.push_back(1.0);
            f.var_info[j].shifts.push_back(lb);
            f.offset += cbase[j] * lb;
            ++nslots;
            if (!std::isfinite(ub)) {
            } else {
                ++m_ub;
            }
        } else if (std::isfinite(ub)) {
            f.var_info[j].slots.push_back(static_cast<int>(nslots));
            f.var_info[j].signs.push_back(-1.0);
            f.var_info[j].shifts.push_back(ub);
            f.offset += cbase[j] * ub;
            ++nslots;
        } else {
            f.var_info[j].slots.push_back(static_cast<int>(nslots));
            f.var_info[j].signs.push_back(1.0);
            f.var_info[j].shifts.push_back(0.0);
            ++nslots;

            f.var_info[j].slots.push_back(static_cast<int>(nslots));
            f.var_info[j].signs.push_back(-1.0);
            f.var_info[j].shifts.push_back(0.0);
            ++nslots;
        }
    }

    for (std::size_t i = 0; i < norig; ++i) {
        const auto& con = problem.constraints[i];
        if (con.sense == model::ConstraintSense::LE || con.sense == model::ConstraintSense::GE) {
            ++ntle;
        }
    }

    const std::size_t m_rows = norig + m_ub;
    const std::size_t ncols = nslots + ntle + m_ub;
    f.m_val = m_rows;
    f.n_val = ncols;
    f.rhs.assign(m_rows, 0.0);
    f.c.assign(ncols, 0.0);

    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t s = 0; s < f.var_info[j].slots.size(); ++s) {
            const int gi = f.var_info[j].slots[s];
            const double sign = f.var_info[j].signs[s];
            f.c[gi] = sign > 0 ? cbase[j] : -cbase[j];
        }
    }

    std::vector<numerical::Triplet> triplets;
    std::size_t next_slack = nslots;

    std::vector<double> gamma(norig, 1.0);
    if (A.order() == numerical::StorageOrder::CSR) {
        for (std::size_t i = 0; i < norig; ++i) {
            double rowmax = 0.0;
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                rowmax = std::max(rowmax, std::abs(A.values()[k]));
            }
            if (rowmax > 0.0) gamma[i] = 1.0 / rowmax;
        }
    }

    for (std::size_t i = 0; i < norig; ++i) {
        double const_i = 0.0;
        bool flip = (problem.constraints[i].sense == model::ConstraintSense::GE);

        if (A.order() == numerical::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                const std::size_t j = A.col_indices()[k];
                const double a = gamma[i] * A.values()[k];

                if (f.var_info[j].is_fixed) {
                    const_i += a * f.var_info[j].fixed;
                } else {
                    for (std::size_t s = 0; s < f.var_info[j].slots.size(); ++s) {
                        const int gi = f.var_info[j].slots[s];
                        const double sign = f.var_info[j].signs[s];
                        const double shift = f.var_info[j].shifts[s];
                        triplets.emplace_back(i, gi, flip ? -a * sign : a * sign);
                        const_i += a * shift;
                    }
                }
            }
        }

        if (problem.constraints[i].sense == model::ConstraintSense::LE ||
            problem.constraints[i].sense == model::ConstraintSense::GE) {
            triplets.emplace_back(i, next_slack++, gamma[i]);
        }

        f.rhs[i] = flip ? (-gamma[i] * problem.constraints[i].rhs + const_i)
                        : (gamma[i] * problem.constraints[i].rhs - const_i);
    }

    if (std::getenv("HYPERNOVA_IPM_DBG2")) {
        std::cerr << "PROBE gamma[0]=" << gamma[0] << " rhs0=" << f.rhs[0]
                  << " m=" << m_rows << " n=" << ncols << "\n";
    }

    std::size_t next_ub_row = norig;
    for (std::size_t j = 0; j < n; ++j) {
        const double lb = problem.variables[j].lower_bound;
        const double ub = problem.variables[j].upper_bound;
        if (std::isfinite(lb) && std::isfinite(ub) && std::abs(ub - lb) > tol.feasibility_tol()) {
            const int gi = f.var_info[j].slots[0];
            triplets.emplace_back(next_ub_row, gi, 1.0);
            triplets.emplace_back(next_ub_row, next_slack++, 1.0);
            f.rhs[next_ub_row] = ub - lb;
            ++next_ub_row;
        }
    }

    f.M = numerical::SparseMatrix::from_triplets(m_rows, ncols, triplets);

    f.col_scale.assign(ncols, 1.0);
    {
        std::vector<double> colmax(ncols, 0.0);
        for (std::size_t i = 0; i < m_rows; ++i) {
            for (std::size_t k = f.M.row_ptr()[i]; k < f.M.row_ptr()[i + 1]; ++k) {
                colmax[f.M.col_indices()[k]] =
                    std::max(colmax[f.M.col_indices()[k]], std::abs(f.M.values()[k]));
            }
        }
        std::vector<numerical::Triplet> scaled;
        scaled.reserve(f.M.nnz());
        for (std::size_t i = 0; i < m_rows; ++i) {
            for (std::size_t k = f.M.row_ptr()[i]; k < f.M.row_ptr()[i + 1]; ++k) {
                const std::size_t j = f.M.col_indices()[k];
                if (colmax[j] > 0.0) f.col_scale[j] = 1.0 / colmax[j];
            }
        }
        for (std::size_t i = 0; i < m_rows; ++i) {
            for (std::size_t k = f.M.row_ptr()[i]; k < f.M.row_ptr()[i + 1]; ++k) {
                const std::size_t j = f.M.col_indices()[k];
                scaled.emplace_back(i, j, f.M.values()[k] * f.col_scale[j]);
            }
        }
        f.M = numerical::SparseMatrix::from_triplets(m_rows, ncols, scaled);
        for (std::size_t j = 0; j < ncols; ++j) f.c[j] *= f.col_scale[j];
    }

    return f;
}

std::vector<double> to_original_primal(const StandardForm& f, const std::vector<double>& y) {
    const std::size_t n = f.var_info.size();
    std::vector<double> primal(n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        if (f.var_info[j].is_fixed) {
            primal[j] = f.var_info[j].fixed;
        } else {
            double val = 0.0;
            for (std::size_t s = 0; s < f.var_info[j].slots.size(); ++s) {
                const int gi = f.var_info[j].slots[s];
                if (gi < 0 || static_cast<std::size_t>(gi) >= f.col_scale.size()) continue;
                val += f.var_info[j].signs[s] * f.col_scale[gi] * y[gi];
                if (s == 0) {
                    val += f.var_info[j].shifts[s];
                }
            }
            primal[j] = val;
        }
    }
    return primal;
}

}  // namespace

InteriorPointSolver::InteriorPointSolver(const numerical::ToleranceConfig& tol, const InteriorPointOptions& options)
    : tol_(tol), options_(options) {}

InteriorPointResult InteriorPointSolver::solve(const model::Problem& problem) {
    auto start_time = std::chrono::high_resolution_clock::now();
    InteriorPointResult result;

    if (problem.variables.empty()) {
        result.status = model::ProblemStatus::OPTIMAL;
        result.objective_value = problem.obj_offset;
        return result;
    }

    try {
        model::Problem cleaned = remove_duplicate_rows(problem);
        StandardForm f = build_standard_form(cleaned, tol_);

        if (f.n() == 0) {
            result.status = model::ProblemStatus::OPTIMAL;
            result.objective_value = f.offset;
            result.primal = to_original_primal(f, std::vector<double>(0, 0.0));
            return result;
        }

        if (f.m() == 0) {
            bool unbounded = false;
            for (std::size_t j = 0; j < f.n(); ++j) {
                if (f.c[j] < -tol_.optimality_tol()) {
                    unbounded = true;
                    break;
                }
            }
            result.status = unbounded ? model::ProblemStatus::UNBOUNDED
                                       : model::ProblemStatus::OPTIMAL;
            std::vector<double> y(f.n(), 0.0);
            result.primal = to_original_primal(f, y);
            double obj = problem.obj_offset;
            for (std::size_t j = 0; j < problem.variables.size(); ++j) {
                obj += problem.variables[j].objective_coeff * result.primal[j];
            }
            result.objective_value = obj;
            return result;
        }

        std::vector<double> lam(f.m(), 0.0);
        std::vector<double> x(f.n(), 1.0);
        std::vector<double> z(f.n());
        for (std::size_t j = 0; j < f.n(); ++j) {
            z[j] = std::max(1.0, std::abs(f.c[j]));
        }

        std::vector<double> rp(f.m()), rd(f.n());
        std::vector<double> D(f.n());
        std::vector<double> tmp_m(f.m()), tmp_n(f.n());
        std::vector<double> dx(f.n()), dz(f.n()), dlam(f.m());
        std::vector<double> rhs_norm(f.m());

        bool done = false;
        bool time_up = false;
        bool interrupted = false;
        bool numerical_error = false;
        const std::size_t max_it = static_cast<std::size_t>(options_.max_iterations);
        std::size_t iter = 0;

        auto deadline_passed = [&]() -> bool {
            if (options_.time_limit_seconds <= 0.0) return false;
            auto now = std::chrono::high_resolution_clock::now();
            return std::chrono::duration<double>(now - start_time).count() >=
                   options_.time_limit_seconds;
        };

        // Reused sparse-Cholesky of the normal equations. The sparsity pattern
        // of N = M^T D M + reg*I only depends on which D[j] are nonzero, so the
        // symbolic analysis (AMD order + elimination tree) is amortized across
        // barrier iterations whenever that active-column set is unchanged.
        std::unique_ptr<numerical::SparseCholesky> barrier_chol;
        std::vector<char> barrier_active_prev;
        bool barrier_factored = false;

        for (iter = 0; iter < max_it; ++iter) {
            if (std::getenv("HYPERNOVA_IPM_PROF")) {
                auto now = std::chrono::high_resolution_clock::now();
                double el = std::chrono::duration<double>(now - start_time).count();
                std::cerr << "[ipm] iter=" << iter << " elapsed=" << el
                          << " n=" << f.n() << " m=" << f.m() << "\n";
            }
            if (options_.interrupt_callback && options_.interrupt_callback()) {
                interrupted = true;
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
            const auto t_enter = std::chrono::steady_clock::now();
            auto phase_ms = [&]() {
                return std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t_enter).count();
            };
            matvec(f.M, x, tmp_m);
            for (std::size_t i = 0; i < f.m(); ++i) rp[i] = tmp_m[i] - f.rhs[i];

            tmp_n = matvec_t(f.M, lam);
            for (std::size_t j = 0; j < f.n(); ++j) rd[j] = tmp_n[j] + z[j] - f.c[j];

            double mu = 0.0;
            for (std::size_t j = 0; j < f.n(); ++j) mu += x[j] * z[j];
            mu /= static_cast<double>(f.n());

            if (max_abs(rp) < options_.convergence_tol &&
                max_abs(rd) < options_.convergence_tol &&
                mu < options_.complementarity_tol) {
                done = true;
                break;
            }

for (std::size_t j = 0; j < f.n(); ++j) D[j] = x[j] / z[j];

            // x[j]/z[j] can overflow when z[j] is tiny (interior variables at
            // pseudo-convergence). Clamp instead of aborting so the barrier can
            // keep iterating toward true KKT feasibility.
            for (std::size_t j = 0; j < f.n(); ++j) {
                if (!(D[j] >= 0.0) || !std::isfinite(D[j]) || D[j] > 1e16) D[j] = 1e16;
            }

            numerical::SparseMatrix N = build_normal_equations(f.M, D, options_.regularization,
                                                               deadline_passed);
            if (deadline_passed()) { time_up = true; break; }
            const double t_build = phase_ms();
            if (std::getenv("HYPERNOVA_IPM_PROF")) {
                std::cerr << "[ipm]   normal_eq rows=" << N.rows() << " nnz=" << N.nnz()
                          << " build_ms=" << t_build << "\n";
            }
            std::vector<char> active(f.n(), 0);
            for (std::size_t j = 0; j < f.n(); ++j) active[j] = (D[j] > 0.0) ? 1 : 0;
            bool pattern_same = barrier_factored && barrier_active_prev.size() == active.size();
            if (pattern_same) {
                for (std::size_t j = 0; j < active.size(); ++j) {
                    if (barrier_active_prev[j] != active[j]) { pattern_same = false; break; }
                }
            }
            const double t_pattern = phase_ms() - t_build;
            if (!barrier_factored) {
                barrier_chol = std::make_unique<numerical::SparseCholesky>();
                barrier_chol->set_interrupt_callback(deadline_passed);
                barrier_chol->analyze(N);
                if (deadline_passed()) { time_up = true; break; }
                barrier_chol->factorize(N);
                barrier_factored = true;
            } else if (!pattern_same) {
                // D's zero pattern changed (columns pinned to bounds) -> the
                // normal-equation pattern changed, so redo the symbolic analysis.
                barrier_chol = std::make_unique<numerical::SparseCholesky>();
                barrier_chol->set_interrupt_callback(deadline_passed);
                barrier_chol->analyze(N);
                if (deadline_passed()) { time_up = true; break; }
                barrier_chol->factorize(N);
            } else {
                barrier_chol->set_interrupt_callback(deadline_passed);
                barrier_chol->refactorize(N, tol_);
            }
            if (deadline_passed()) { time_up = true; break; }
            barrier_active_prev = active;
            const double t_fac1 = phase_ms();
            if (std::getenv("HYPERNOVA_IPM_PROF")) {
                std::cerr << "[ipm]   factor_ms=" << (t_fac1 - t_build) << "\n";
            }

            for (std::size_t j = 0; j < f.n(); ++j) tmp_n[j] = D[j] * rd[j];
            std::vector<double> mdrd;
            matvec(f.M, tmp_n, mdrd);
            std::vector<double> mx;
            matvec(f.M, x, mx);

            for (std::size_t i = 0; i < f.m(); ++i) {
                rhs_norm[i] = -rp[i] - mdrd[i] + mx[i];
            }

            dlam = rhs_norm;
            barrier_chol->solve(dlam);
            numerical::iterative_refinement(*barrier_chol, N, dlam, rhs_norm);
            const double t_refine1 = phase_ms();

            tmp_n = matvec_t(f.M, dlam);
            bool num_bad = false;
            for (std::size_t j = 0; j < f.n(); ++j) {
                dx[j] = D[j] * (tmp_n[j] + rd[j]) - x[j];
                // Algebraically: dz[j] = -z[j] - (z[j]/x[j])*dx[j]
                //            = -(tmp_n[j] + rd[j])  (after substituting dx and D)
                // This form avoids overflow in z[j]/x[j] when x[j] → 0.
                dz[j] = -(tmp_n[j] + rd[j]);
            }

            num_bad = false;
            for (std::size_t j = 0; j < f.n() && !num_bad; ++j) {
                if (!std::isfinite(dx[j]) || !std::isfinite(dz[j])) num_bad = true;
            }
            for (std::size_t i = 0; i < f.m() && !num_bad; ++i) {
                if (!std::isfinite(dlam[i])) num_bad = true;
            }
            if (num_bad) {
                numerical_error = true;
                break;
            }

            const double ap_a = max_step(x, dx);
            const double ad_a = max_step(z, dz);

            double mu_aff = 0.0;
            for (std::size_t j = 0; j < f.n(); ++j) {
                mu_aff += (x[j] + ap_a * dx[j]) * (z[j] + ad_a * dz[j]);
            }
            mu_aff /= static_cast<double>(f.n());

            double sigma;
            if (!std::isfinite(mu_aff) || mu_aff < 0.0 || !(mu > 0.0)) {
                sigma = 0.5;
            } else {
                sigma = std::clamp(std::pow(mu_aff / std::max(mu, 1e-300), 3.0), 0.1, 0.9);
            }

            std::vector<double> zinv(f.n());
            std::vector<double> zinv_scaled(f.n());
            for (std::size_t j = 0; j < f.n(); ++j) {
                zinv[j] = 1.0 / z[j];
                zinv_scaled[j] = sigma * mu * zinv[j];
            }
            std::vector<double> mz;
            matvec(f.M, zinv_scaled, mz);
            for (std::size_t i = 0; i < f.m(); ++i) {
                rhs_norm[i] = -rp[i] - mdrd[i] + mx[i] - mz[i];
            }

            dlam = rhs_norm;
            barrier_chol->solve(dlam);
            numerical::iterative_refinement(*barrier_chol, N, dlam, rhs_norm);
            const double t_refine2 = phase_ms();

            tmp_n = matvec_t(f.M, dlam);
            for (std::size_t j = 0; j < f.n(); ++j) {
                dx[j] = D[j] * (tmp_n[j] + rd[j]) - x[j] + sigma * mu / z[j];
                // Same algebraic identity as the affine step:
                // dz[j] = (sigma*mu - x[j]*z[j] - z[j]*dx[j]) / x[j]
                //       = -(tmp_n[j] + rd[j])
                dz[j] = -(tmp_n[j] + rd[j]);
            }

            num_bad = false;
            for (std::size_t j = 0; j < f.n() && !num_bad; ++j) {
                if (!std::isfinite(dx[j]) || !std::isfinite(dz[j])) num_bad = true;
            }
            for (std::size_t i = 0; i < f.m() && !num_bad; ++i) {
                if (!std::isfinite(dlam[i])) num_bad = true;
            }
            if (num_bad) {
                numerical_error = true;
                break;
            }

            const double ap = max_step(x, dx);
            const double ad = max_step(z, dz);

            for (std::size_t j = 0; j < f.n(); ++j) {
                x[j] += ap * dx[j];
                z[j] += ad * dz[j];
            }
            for (std::size_t i = 0; i < f.m(); ++i) {
                lam[i] += ad * dlam[i];
            }

            num_bad = false;
            for (std::size_t j = 0; j < f.n() && !num_bad; ++j) {
                // Allow x[j] = 0 at convergence (correct limit for bound-pinned variables).
                if (x[j] < 0.0 || z[j] < 0.0 || !std::isfinite(x[j]) || !std::isfinite(z[j])) {
                    num_bad = true;
                }
            }
            if (num_bad) {
                numerical_error = true;
                break;
            }

            if (std::getenv("HYPERNOVA_IPM_DBG")) {
                std::cerr << "IPM it=" << iter << " mu=" << mu
                          << " rp=" << max_abs(rp) << " rd=" << max_abs(rd)
                          << " ap=" << ap << " ad=" << ad << "\n";
            }
            if (std::getenv("HYPERNOVA_IPM_PROF")) {
                std::cerr << "IPM prof it=" << iter
                          << " build~" << t_build
                          << "ms fac1~" << t_fac1
                          << "ms (pattern check " << t_pattern << "ms)"
                          << " refine1~" << t_refine1
                          << "ms refine2~" << t_refine2
                          << "ms nnz(N)=" << N.nnz() << "\n";
            }

            result.iterations = iter + 1;
        }

        if (!done && !interrupted) {
            matvec(f.M, x, tmp_m);
            for (std::size_t i = 0; i < f.m(); ++i) rp[i] = tmp_m[i] - f.rhs[i];
            tmp_n = matvec_t(f.M, lam);
            for (std::size_t j = 0; j < f.n(); ++j) rd[j] = tmp_n[j] + z[j] - f.c[j];
            double mu = 0.0;
            for (std::size_t j = 0; j < f.n(); ++j) mu += x[j] * z[j];
            mu /= static_cast<double>(f.n());

            // The barrier can stall with tiny mu/rd but a modest primal residual
            // when many variables are pinned to bounds. If crossover is enabled,
            // hand the near-converged point to simplex polish for an exact
            // optimum; otherwise fall back to the strict threshold.
            const double rp_tol = options_.crossover ? 1e-2 : 1e-4;
            if (max_abs(rp) < rp_tol && max_abs(rd) < 1e-4 && mu < 1e-4) {
                done = true;
            }
        }

        // Always report the incumbent iterate: a limited or stalled solve
        // should still surface the best point found rather than an empty one.
        std::vector<double> primal = to_original_primal(f, x);

        double obj = 0.0;
        bool obj_ok = std::isfinite(problem.obj_offset);
        for (std::size_t j = 0; j < problem.variables.size() && obj_ok; ++j) {
            const double term = problem.variables[j].objective_coeff * primal[j];
            if (!std::isfinite(term)) obj_ok = false;
            obj += term;
        }
        if (obj_ok) obj += problem.obj_offset;
        if (!obj_ok || numerical_error) numerical_error = true;

        result.objective_value = obj_ok ? obj : 0.0;
        result.primal = primal;
        result.dual.assign(problem.constraints.size(), 0.0);

        result.status = numerical_error ? model::ProblemStatus::NUMERICAL_ERROR
                                        : (interrupted ? model::ProblemStatus::INTERRUPTED
                                                       : (done ? model::ProblemStatus::OPTIMAL
                                                               : (time_up ? model::ProblemStatus::TIME_LIMIT
                                                                          : model::ProblemStatus::ITER_LIMIT)));

        if (std::getenv("HYPERNOVA_IPM_DBG")) {
            std::cerr << "IPM exit done=" << done << " time_up=" << time_up
                      << " interrupted=" << interrupted << " numerr=" << numerical_error << "\n";
        }

        // Crossover: polish with simplex. Also attempted when the barrier did
        // not converge cleanly (iteration/time limit), so a stalled point can
        // still be resolved to a definitive optimal/infeasible/unbounded
        // verdict. The polisher inherits the same wall-clock budget. Skipped on
        // interruption so a user interrupt is honored promptly.
        if (options_.crossover && !interrupted) {
            // Note: time_limit_seconds == 0 means "unlimited" (not a zero budget).
            double remaining = options_.time_limit_seconds;
            if (remaining > 0.0) {
                auto now = std::chrono::high_resolution_clock::now();
                remaining -= std::chrono::duration<double>(now - start_time).count();
            }
            if (options_.time_limit_seconds > 0.0 && remaining <= 0.0) {
                // Finite time budget exhausted; keep the barrier's verdict.
            } else {
                if (remaining <= 0.0) remaining = 0.0; // 0 = unlimited in the polisher
                SimplexOptions sim_opts;
                sim_opts.time_limit_seconds = remaining;

                // Verdict handling shared by the warm and cold polish passes.
                auto adopt = [&result, &done](const SimplexResult& sim) {
                    if (sim.status == model::ProblemStatus::OPTIMAL) {
                        result.status = model::ProblemStatus::OPTIMAL;
                        result.objective_value = sim.objective_value;
                        result.primal = sim.primal;
                        result.dual = sim.dual;
                        result.reduced_costs = sim.reduced_costs;
                        result.iterations = sim.iterations;
                    } else if (!done && sim.status == model::ProblemStatus::INFEASIBLE) {
                        result.status = model::ProblemStatus::INFEASIBLE;
                    } else if (sim.status == model::ProblemStatus::UNBOUNDED) {
                        result.status = model::ProblemStatus::UNBOUNDED;
                    }
                };

                bool resolved = false;

                // Warm-start the polisher from the IPM endpoint: variables pinned
                // to a finite bound are offered as nonbasic while strictly
                // interior variables are the basic-set candidates (0 = basic,
                // -1 = at lower bound, 1 = at upper bound, matching Basis).
                // Simplex then pivots the residual infeasibility away to an exact
                // vertex optimum, which is what yields certified duals and exact
                // complementarity for the verifier.
                std::vector<int> var_status(cleaned.variables.size(), 0);
                for (std::size_t j = 0; j < cleaned.variables.size(); ++j) {
                    const double lb = cleaned.variables[j].lower_bound;
                    const double ub = cleaned.variables[j].upper_bound;
                    const double at_tol = 1e-6 * (1.0 + std::max(std::abs(lb), std::abs(ub)));
                    if (primal[j] <= lb + at_tol) {
                        var_status[j] = -1;
                    } else if (primal[j] >= ub - at_tol) {
                        var_status[j] = 1;
                    }
                }
                try {
                    SimplexOptions warm_opts = sim_opts;
                    warm_opts.max_iterations = options_.crossover_max_iter;
                    SimplexSolver polisher(tol_, warm_opts);
                    auto sim = polisher.solve_with_basis(cleaned, var_status);
                    if (std::getenv("HYPERNOVA_IPM_DBG")) {
                        std::cerr << "IPM warm polisher status=" << static_cast<int>(sim.status)
                                  << " iters=" << sim.iterations
                                  << " obj=" << sim.objective_value << "\n";
                    }
                    adopt(sim);
                    resolved = sim.status == model::ProblemStatus::OPTIMAL ||
                               sim.status == model::ProblemStatus::UNBOUNDED ||
                               (sim.status == model::ProblemStatus::INFEASIBLE && !done);
                } catch (...) {
                    // Singular warm basis or unsupported start; fall back below.
                }

                // The warm basis can be ill-conditioned on degenerate LPs; if it
                // failed to resolve to a definitive verdict, retry from a cold
                // start to preserve the same behavior as the pre-warm crossover.
                if (!resolved) {
                    try {
                        SimplexSolver polisher(tol_, sim_opts);
                        auto sim = polisher.solve(cleaned);
                        if (std::getenv("HYPERNOVA_IPM_DBG")) {
                            std::cerr << "IPM cold polisher status=" << static_cast<int>(sim.status)
                                      << " iters=" << sim.iterations
                                      << " obj=" << sim.objective_value << "\n";
                        }
                        adopt(sim);
                    } catch (...) {
                        // Keep the barrier verdict.
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

} // namespace hypernova::lp