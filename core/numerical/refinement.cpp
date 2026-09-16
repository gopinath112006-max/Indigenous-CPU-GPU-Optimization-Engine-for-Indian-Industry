#include "refinement.hpp"
#include <algorithm>
#include <cmath>

namespace hypernova::numerical {

int iterative_refinement(const SparseFactorization& fac,
                         const SparseMatrix& A,
                         std::vector<double>& x,
                         const std::vector<double>& rhs,
                         double relative_tol,
                         int max_iter) {
    const std::size_t n = x.size();
    if (n == 0 || rhs.size() != n || A.rows() != n) return 0;

    double norm_rhs = 0.0;
    for (double v : rhs) norm_rhs = std::max(norm_rhs, std::abs(v));
    if (norm_rhs == 0.0 || norm_rhs != norm_rhs) return 0;

    int corrections = 0;
    std::vector<double> r(n);
    for (int it = 0; it < max_iter; ++it) {
        std::vector<double> ax = A.multiply(x);
        double rel = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            rel = std::max(rel, std::abs(rhs[i] - ax[i]));
        }
        rel /= norm_rhs;
        if (rel <= relative_tol) break;

        for (std::size_t i = 0; i < n; ++i) r[i] = rhs[i] - ax[i];
        fac.solve(r);
        for (std::size_t i = 0; i < n; ++i) x[i] += r[i];
        ++corrections;
    }
    return corrections;
}

int dense_solve_refined(const std::vector<double>& kkt,
                        std::size_t total,
                        const std::vector<double>& rhs,
                        std::vector<double>& sol,
                        double relative_tol,
                        int max_iter) {
    if (total == 0 || kkt.size() < total * total || rhs.size() != total) return 0;

    auto ge_solve = [&](const std::vector<double>& rhs_in, std::vector<double>& out) {
        std::vector<double> M = kkt;
        std::vector<double> b = rhs_in;
        for (std::size_t i = 0; i < total; ++i) {
            std::size_t max_row = i;
            double max_val = std::abs(M[i * total + i]);
            for (std::size_t k = i + 1; k < total; ++k) {
                double val = std::abs(M[k * total + i]);
                if (val > max_val) {
                    max_val = val;
                    max_row = k;
                }
            }
            if (max_row != i) {
                for (std::size_t j = i; j < total; ++j) {
                    std::swap(M[i * total + j], M[max_row * total + j]);
                }
                std::swap(b[i], b[max_row]);
            }
            double pivot = M[i * total + i];
            if (std::abs(pivot) < 1e-14) {
                M[i * total + i] = 1e-14;
                pivot = M[i * total + i];
            }
            for (std::size_t k = i + 1; k < total; ++k) {
                double factor = M[k * total + i] / pivot;
                for (std::size_t j = i; j < total; ++j) {
                    M[k * total + j] -= factor * M[i * total + j];
                }
                b[k] -= factor * b[i];
            }
        }
        out.assign(total, 0.0);
        for (std::size_t i = total; i > 0; --i) {
            std::size_t row = i - 1;
            double sum = b[row];
            for (std::size_t j = row + 1; j < total; ++j) {
                sum -= M[row * total + j] * out[j];
            }
            out[row] = sum / M[row * total + row];
        }
    };

    ge_solve(rhs, sol);

    double norm_rhs = 0.0;
    for (double v : rhs) norm_rhs = std::max(norm_rhs, std::abs(v));
    if (norm_rhs == 0.0) return 0;

    int corrections = 0;
    for (int it = 0; it < max_iter; ++it) {
        std::vector<double> ax(total, 0.0);
        for (std::size_t i = 0; i < total; ++i) {
            double s = 0.0;
            for (std::size_t j = 0; j < total; ++j) s += kkt[i * total + j] * sol[j];
            ax[i] = s;
        }
        double rel = 0.0;
        for (std::size_t i = 0; i < total; ++i) {
            rel = std::max(rel, std::abs(rhs[i] - ax[i]));
        }
        rel /= norm_rhs;
        if (rel <= relative_tol) break;

        std::vector<double> corr_b(total);
        for (std::size_t i = 0; i < total; ++i) corr_b[i] = rhs[i] - ax[i];
        std::vector<double> corr;
        ge_solve(corr_b, corr);
        for (std::size_t i = 0; i < total; ++i) sol[i] += corr[i];
        ++corrections;
    }
    return corrections;
}

} // namespace hypernova::numerical