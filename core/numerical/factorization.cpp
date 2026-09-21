#include "factorization.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <unordered_map>

namespace hypernova::numerical {

const std::vector<double> SparseFactorization::empty_D_;

namespace {

double matrix_1norm(const SparseMatrix& A) {
    std::vector<double> col_sum(A.cols(), 0.0);
    if (A.order() == StorageOrder::CSR) {
        for (std::size_t i = 0; i < A.rows(); ++i) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                col_sum[A.col_indices()[k]] += std::abs(A.values()[k]);
            }
        }
    }
    double norm = 0.0;
    for (double s : col_sum) norm = std::max(norm, s);
    return norm;
}

double hager_inverse_1norm(const SparseFactorization& fac, std::size_t n) {
    std::vector<double> v(n, 1.0 / std::sqrt(static_cast<double>(n)));
    double est = 0.0;
    for (int iter = 0; iter < 5 && n > 0; ++iter) {
        std::vector<double> z = v;
        fac.solve_transpose(z);
        std::size_t j = 0;
        for (std::size_t k = 1; k < n; ++k) {
            if (std::abs(z[k]) > std::abs(z[j])) j = k;
        }
        std::vector<double> x(n, 0.0);
        x[j] = 1.0;
        fac.solve(x);
        double nrm = 0.0;
        for (double val : x) nrm += std::abs(val);
        if (nrm <= est + 1e-15) break;
        est = nrm;
        for (std::size_t k = 0; k < n; ++k) v[k] = (x[k] >= 0.0 ? 1.0 : -1.0);
    }
    return est;
}

double estimate_condition_number(const SparseFactorization& fac, const SparseMatrix& A) {
    std::size_t n = A.rows();
    if (n == 0) return 0.0;
    double norm_a = matrix_1norm(A);
    if (norm_a == 0.0) return 0.0;
    double norm_inv = hager_inverse_1norm(fac, n);
    return norm_a * norm_inv;
}

// Build the undirected elimination graph induced by the stored pattern of A.
// Each nonzero (i,j) yields the edge (i,j); symmetric entries are seen through
// symmetrization, so a one-triangle storage still yields the full graph.
// Self loops are retained (they mark which vertices participate), and each
// adjacency list is returned sorted with no duplicates.
std::vector<std::vector<std::size_t>> build_elimination_graph(const SparseMatrix& A) {
    const std::size_t n = A.rows();
    std::vector<std::vector<std::size_t>> adj(n);
    if (A.order() != StorageOrder::CSR || n == 0) return adj;

    std::vector<int> mark(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            const std::size_t j = A.col_indices()[k];
            if (j >= n) continue;
            if (mark[j] != static_cast<int>(i)) {
                mark[j] = static_cast<int>(i);
                adj[i].push_back(j);
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        for (const std::size_t j : adj[i]) {
            if (j != i) adj[j].push_back(i);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        std::sort(adj[i].begin(), adj[i].end());
        adj[i].erase(std::unique(adj[i].begin(), adj[i].end()), adj[i].end());
    }
    return adj;
}

// Minimum-degree ordering of the elimination graph plus the resulting fill-in
// pattern of the Cholesky/LDLT factor, expressed in the permuted (B = P A P^T)
// coordinate system.
//
// perm[r] is the original vertex eliminated at step r (row r of B).
// inv_perm[v] is the position of original vertex v in B.
// The pattern of row r of L is {r} U {inv_perm[w] : w in adj[order[r]], inv_perm[w] < r},
// i.e. the diagonal plus every neighbor of the vertex already eliminated.
//
// The graph is updated as elimination progresses: eliminating a vertex turns its
// live neighbors into a clique (the symbolic fill-in). Dead vertices stay in the
// adjacency lists so that the row patterns above are complete.
void compute_md_order_and_pattern(const SparseMatrix& A,
                                  std::vector<std::size_t>& perm,
                                  std::vector<std::size_t>& inv_perm,
                                  std::vector<std::size_t>& L_row_ptr,
                                  std::vector<std::size_t>& L_col_indices,
                                  const std::function<bool()>& interrupt = {}) {
    const std::size_t n = A.rows();
    perm.assign(n, n);
    inv_perm.assign(n, n);
    L_row_ptr.assign(n + 1, 0);

    std::vector<std::vector<std::size_t>> pattern(n);
    if (n == 0) {
        L_col_indices.clear();
        return;
    }

    std::vector<std::vector<std::size_t>> adj = build_elimination_graph(A);
    std::vector<char> live(n, 1);
    std::vector<std::size_t> deg(n, 0);
    for (std::size_t v = 0; v < n; ++v) {
        for (const std::size_t w : adj[v]) {
            if (w != v) ++deg[v];
        }
    }

    for (std::size_t r = 0; r < n; ++r) {
        if (interrupt && (r & 15u) == 0u && interrupt()) break;
        std::size_t best = n;
        std::size_t best_deg = n + 1;
        for (std::size_t v = 0; v < n; ++v) {
            if (!live[v]) continue;
            if (deg[v] < best_deg || (deg[v] == best_deg && v < best)) {
                best = v;
                best_deg = deg[v];
            }
        }

        perm[r] = best;
        inv_perm[best] = r;

        std::vector<std::size_t> row;
        row.reserve(adj[best].size() + 1);
        for (const std::size_t w : adj[best]) {
            if (w != best && inv_perm[w] < r) row.push_back(inv_perm[w]);
        }
        row.push_back(r);
        std::sort(row.begin(), row.end());
        row.erase(std::unique(row.begin(), row.end()), row.end());
        pattern[r] = std::move(row);

        live[best] = 0;
        for (const std::size_t w : adj[best]) {
            if (live[w]) --deg[w];
        }

        for (const std::size_t a : adj[best]) {
            if (!live[a] || a == best) continue;
            if (interrupt && interrupt()) return;
            for (const std::size_t b : adj[best]) {
                if (!live[b] || b <= a) continue;
                if (std::find(adj[a].begin(), adj[a].end(), b) == adj[a].end()) {
                    adj[a].push_back(b);
                    adj[b].push_back(a);
                    ++deg[a];
                    ++deg[b];
                }
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        L_row_ptr[i + 1] = L_row_ptr[i] + pattern[i].size();
    }
    L_col_indices.resize(L_row_ptr[n]);
    for (std::size_t i = 0; i < n; ++i) {
        std::copy(pattern[i].begin(), pattern[i].end(),
                  L_col_indices.begin() + L_row_ptr[i]);
    }
}

// Build B = P A P^T row entries (sorted ascending, duplicate columns summed).
// inv_perm[original] is the position of that row/column in B, which is the
// permutation implied by compute_md_order_and_pattern.
std::vector<std::vector<std::pair<std::size_t, double>>> permute_symmetric(
    const SparseMatrix& A, const std::vector<std::size_t>& inv_perm) {
    const std::size_t n = A.rows();
    std::vector<std::vector<std::pair<std::size_t, double>>> B(n);
    if (A.order() != StorageOrder::CSR || n == 0) return B;

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t r = inv_perm[i];
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            const std::size_t j = A.col_indices()[k];
            if (j >= n) continue;
            B[r].emplace_back(inv_perm[j], A.values()[k]);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        std::sort(B[i].begin(), B[i].end());
        std::vector<std::pair<std::size_t, double>> merged;
        merged.reserve(B[i].size());
        for (const auto& e : B[i]) {
            if (!merged.empty() && merged.back().first == e.first) {
                merged.back().second += e.second;
            } else {
                merged.push_back(e);
            }
        }
        B[i] = std::move(merged);
    }
    return B;
}

double permuted_value(const std::vector<std::vector<std::pair<std::size_t, double>>>& B,
                      std::size_t i, std::size_t j) {
    const auto& row = B[i];
    for (const auto& e : row) {
        if (e.first == j) return e.second;
        if (e.first > j) break;
    }
    return 0.0;
}

} // namespace

// ============================================================================
// SparseLU Implementation
// ============================================================================

SparseLU::SparseLU(const SparseMatrix& A, const ToleranceConfig& tol)
    : tol_(tol) {
    analyze(A);
    factorize(A);
}

void SparseLU::analyze(const SparseMatrix& A) {
    symbolic_analysis(A);
}

void SparseLU::factorize(const SparseMatrix& A, const ToleranceConfig& tol) {
    tol_ = tol;
    numeric_factorization(A);
}

void SparseLU::refactorize(const SparseMatrix& A, const ToleranceConfig& tol) {
    tol_ = tol;
    numeric_factorization(A);
}

void SparseLU::symbolic_analysis(const SparseMatrix& A) {
    std::size_t n = A.rows();
    std::size_t m = A.cols();

    symbolic_.row_perm.resize(n);
    symbolic_.col_perm.resize(m);
    std::iota(symbolic_.row_perm.begin(), symbolic_.row_perm.end(), 0);
    
    std::vector<std::size_t> col_deg(m, 0);
    if (A.order() == StorageOrder::CSR) {
        for (std::size_t k = 0; k < A.nnz(); ++k) {
            col_deg[A.col_indices()[k]]++;
        }
    }
    std::iota(symbolic_.col_perm.begin(), symbolic_.col_perm.end(), 0);
    std::sort(symbolic_.col_perm.begin(), symbolic_.col_perm.end(), [&](std::size_t a, std::size_t b) {
        if (col_deg[a] != col_deg[b]) return col_deg[a] < col_deg[b];
        return a < b;
    });

    symbolic_.L_row_ptr.assign(n + 1, 0);
    symbolic_.U_row_ptr.assign(n + 1, 0);

    std::vector<std::vector<std::size_t>> L_pattern(n);
    std::vector<std::vector<std::size_t>> U_pattern(n);

    std::vector<std::size_t> col_counts(m, 0);
    if (A.order() == StorageOrder::CSR) {
        for (std::size_t k = 0; k < A.nnz(); ++k) {
            col_counts[A.col_indices()[k]]++;
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        std::vector<std::size_t> cols;
        if (A.order() == StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                cols.push_back(A.col_indices()[k]);
            }
        }
        std::sort(cols.begin(), cols.end());

        for (std::size_t j : cols) {
            if (j <= i) {
                L_pattern[i].push_back(j);
            } else {
                U_pattern[i].push_back(j);
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        symbolic_.L_row_ptr[i + 1] = symbolic_.L_row_ptr[i] + L_pattern[i].size();
        symbolic_.U_row_ptr[i + 1] = symbolic_.U_row_ptr[i] + U_pattern[i].size();
    }

    symbolic_.L_col_indices.resize(symbolic_.L_row_ptr[n]);
    symbolic_.U_col_indices.resize(symbolic_.U_row_ptr[n]);

    for (std::size_t i = 0; i < n; ++i) {
        std::vector<std::size_t> cols;
        if (A.order() == StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                cols.push_back(A.col_indices()[k]);
            }
        }
        std::sort(cols.begin(), cols.end());

        std::size_t L_idx = symbolic_.L_row_ptr[i];
        std::size_t U_idx = symbolic_.U_row_ptr[i];
        for (std::size_t j : cols) {
            if (j <= i) {
                symbolic_.L_col_indices[L_idx++] = j;
            } else {
                symbolic_.U_col_indices[U_idx++] = j;
            }
        }
    }

    compute_etree();
}

void SparseLU::numeric_factorization(const SparseMatrix& A) {
    const std::size_t n = A.rows();
    row_scale_.assign(n, 1.0);
    col_scale_.assign(n, 1.0);
    if (n > 0) {
        for (std::size_t i = 0; i < n; ++i) {
            double rmax = 0.0;
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                rmax = std::max(rmax, std::abs(A.values()[k]));
            }
            if (rmax > 1e-12) row_scale_[i] = 1.0 / rmax;
        }
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                double val = std::abs(A.values()[k]) * row_scale_[i];
                col_scale_[j] = std::max(col_scale_[j], val);
            }
        }
        for (std::size_t j = 0; j < n; ++j) {
            if (col_scale_[j] > 1e-12) col_scale_[j] = 1.0 / col_scale_[j];
            else col_scale_[j] = 1.0;
        }
    }

    SparseMatrix scaled_A = A;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = scaled_A.row_ptr()[i]; k < scaled_A.row_ptr()[i + 1]; ++k) {
            std::size_t j = scaled_A.col_indices()[k];
            scaled_A.mutable_values()[k] *= row_scale_[i] * col_scale_[j];
        }
    }

    const char* dense_env = std::getenv("HYPERNOVA_DENSE_FACTOR");
    if (dense_env != nullptr && std::strcmp(dense_env, "1") == 0) {
        numeric_factorization_dense(scaled_A);
        return;
    }
    numeric_factorization_sparse(scaled_A);
}

void SparseLU::numeric_factorization_dense(const SparseMatrix& A) {
std::cerr << "DENSE FACTORIZATION!\n";
    const std::size_t n = A.rows();
    dense_n_ = n;
    L_values_.clear();
    U_values_.clear();
    dense_L_.assign(n * n, 0.0);
    dense_U_.assign(n * n, 0.0);
    reciprocals_.assign(n, 0.0);

    auto start_time = std::chrono::high_resolution_clock::now();

    if (n == 0) {
        symbolic_.row_perm.clear();
        symbolic_.col_perm.clear();
        symbolic_.L_row_ptr.assign(1, 0);
        symbolic_.U_row_ptr.assign(1, 0);
        symbolic_.L_col_indices.clear();
        symbolic_.U_col_indices.clear();
        auto end_time = std::chrono::high_resolution_clock::now();
        stats_.factorization_time_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        stats_.nnz_L = 0;
        stats_.nnz_U = 0;
        stats_.rank = 0;
        stats_.singular = false;
        return;
    }

    std::vector<double> M(n * n, 0.0);
    if (A.order() == StorageOrder::CSR) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                if (j < n) M[i * n + j] = A.values()[k];
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        dense_L_[i * n + i] = 1.0;
    }

    std::vector<std::size_t> pivot_row(n);
    std::iota(pivot_row.begin(), pivot_row.end(), 0);
    std::size_t rank = 0;
    bool singular = false;

    for (std::size_t cstep = 0; cstep < n; ++cstep) {
        std::size_t col = symbolic_.col_perm.empty() ? cstep : symbolic_.col_perm[cstep];
        std::size_t piv = cstep;
        double best = std::abs(M[pivot_row[cstep] * n + col]);
        for (std::size_t r = cstep + 1; r < n; ++r) {
            const double v = std::abs(M[pivot_row[r] * n + col]);
            if (v > best) {
                best = v;
                piv = r;
            }
        }
        std::swap(pivot_row[cstep], pivot_row[piv]);
        if (piv != cstep) {
            for (std::size_t k = 0; k < cstep; ++k) {
                std::size_t k_col = symbolic_.col_perm.empty() ? k : symbolic_.col_perm[k];
                std::swap(dense_L_[cstep * n + k_col], dense_L_[piv * n + k_col]);
            }
        }

        const double pivot = M[pivot_row[cstep] * n + col];
        if (std::abs(pivot) <= std::max(tol_.singular_tol(), 1e-14)) {
            singular = true;
            continue;
        }

        dense_U_[col * n + col] = pivot;
        for (std::size_t c = cstep + 1; c < n; ++c) {
            dense_U_[col * n + c] = M[pivot_row[cstep] * n + c];
        }

        for (std::size_t r = cstep + 1; r < n; ++r) {
            const double mult = M[pivot_row[r] * n + col] / pivot;
            dense_L_[r * n + col] = mult;
            for (std::size_t c = cstep; c < n; ++c) {
                M[pivot_row[r] * n + c] -= mult * M[pivot_row[cstep] * n + c];
            }
        }
        ++rank;
    }

    for (std::size_t i = 0; i < rank; ++i) {
        const double piv = dense_U_[i * n + i];
        if (std::abs(piv) > tol_.singular_tol()) {
            reciprocals_[i] = 1.0 / piv;
        } else {
            reciprocals_[i] = 0.0;
            singular = true;
        }
    }
    for (std::size_t i = rank; i < n; ++i) {
        reciprocals_[i] = 0.0;
    }

    symbolic_.row_perm = pivot_row;


    std::vector<std::vector<std::size_t>> L_pattern(n);
    std::vector<std::vector<std::size_t>> U_pattern(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            const double v = (j == i) ? 1.0 : dense_L_[i * n + j];
            if (std::abs(v) > tol_.zero_tol()) {
                L_pattern[i].push_back(j);
            }
        }
        for (std::size_t j = i + 1; j < n; ++j) {
            if (std::abs(dense_U_[i * n + j]) > tol_.zero_tol()) {
                U_pattern[i].push_back(j);
            }
        }
    }

    symbolic_.L_row_ptr.assign(n + 1, 0);
    symbolic_.U_row_ptr.assign(n + 1, 0);
    std::size_t nnz_L = 0;
    std::size_t nnz_U = 0;
    for (std::size_t i = 0; i < n; ++i) {
        symbolic_.L_row_ptr[i + 1] = symbolic_.L_row_ptr[i] + L_pattern[i].size();
        symbolic_.U_row_ptr[i + 1] = symbolic_.U_row_ptr[i] + U_pattern[i].size();
        nnz_L += L_pattern[i].size();
        nnz_U += U_pattern[i].size();
    }
    symbolic_.L_col_indices.resize(nnz_L);
    symbolic_.U_col_indices.resize(nnz_U);
    L_values_.assign(nnz_L, 0.0);
    U_values_.assign(nnz_U, 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        std::size_t idx = symbolic_.L_row_ptr[i];
        for (std::size_t j : L_pattern[i]) {
            symbolic_.L_col_indices[idx] = j;
            L_values_[idx] = (j == i) ? 1.0 : dense_L_[i * n + j];
            ++idx;
        }
        idx = symbolic_.U_row_ptr[i];
        for (std::size_t j : U_pattern[i]) {
            symbolic_.U_col_indices[idx] = j;
            U_values_[idx] = dense_U_[i * n + j];
            ++idx;
        }
    }

    // Column-oriented (CSC) patterns enable O(nnz) transpose triangular
    // substitutions in solve_transpose.
    std::vector<std::size_t> cntL(n, 0), cntU(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            ++cntL[symbolic_.L_col_indices[k]];
        }
        for (std::size_t k = symbolic_.U_row_ptr[i]; k < symbolic_.U_row_ptr[i + 1]; ++k) {
            ++cntU[symbolic_.U_col_indices[k]];
        }
    }
    symbolic_.L_col_ptr.assign(n + 1, 0);
    symbolic_.U_col_ptr.assign(n + 1, 0);
    for (std::size_t j = 0; j < n; ++j) {
        symbolic_.L_col_ptr[j + 1] = symbolic_.L_col_ptr[j] + cntL[j];
        symbolic_.U_col_ptr[j + 1] = symbolic_.U_col_ptr[j] + cntU[j];
    }
    symbolic_.L_row_indices.assign(nnz_L, 0);
    symbolic_.U_row_indices.assign(nnz_U, 0);
    symbolic_.L_csc_values.assign(nnz_L, 0.0);
    symbolic_.U_csc_values.assign(nnz_U, 0.0);
    {
        std::vector<std::size_t> posL = symbolic_.L_col_ptr;
        std::vector<std::size_t> posU = symbolic_.U_col_ptr;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
                const std::size_t p = posL[symbolic_.L_col_indices[k]]++;
                symbolic_.L_row_indices[p] = i;
                symbolic_.L_csc_values[p] = L_values_[k];
            }
            for (std::size_t k = symbolic_.U_row_ptr[i]; k < symbolic_.U_row_ptr[i + 1]; ++k) {
                const std::size_t p = posU[symbolic_.U_col_indices[k]]++;
                symbolic_.U_row_indices[p] = i;
                symbolic_.U_csc_values[p] = U_values_[k];
            }
        }
    }

    compute_etree();

    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.factorization_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();
    stats_.nnz_L = nnz_L;
    stats_.nnz_U = nnz_U;
    stats_.rank = static_cast<int>(rank);
    stats_.singular = singular;
    stats_.condition_estimate = (!singular && rank == n) ? estimate_condition_number(*this, A) : 0.0;
}

void SparseLU::numeric_factorization_sparse(const SparseMatrix& A) {
std::cerr << "SPARSE FACTORIZATION!\n";
    const std::size_t n = A.rows();
    dense_n_ = n;
    L_values_.clear();
    U_values_.clear();
    reciprocals_.assign(n, 0.0);

    auto start_time = std::chrono::high_resolution_clock::now();

    if (n == 0) {
        symbolic_.row_perm.clear();
        symbolic_.col_perm.clear();
        symbolic_.L_row_ptr.assign(1, 0);
        symbolic_.U_row_ptr.assign(1, 0);
        symbolic_.L_col_indices.clear();
        symbolic_.U_col_indices.clear();
        symbolic_.L_col_ptr.assign(1, 0);
        symbolic_.U_col_ptr.assign(1, 0);
        symbolic_.L_row_indices.clear();
        symbolic_.U_row_indices.clear();
        symbolic_.L_csc_values.clear();
        symbolic_.U_csc_values.clear();
        auto end_time = std::chrono::high_resolution_clock::now();
        stats_.factorization_time_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        stats_.nnz_L = 0;
        stats_.nnz_U = 0;
        stats_.rank = 0;
        stats_.singular = false;
        compute_etree();
        return;
    }

    // Default initialize the empty topology so downstream readers are valid
    // even if an early branch bails out.

    symbolic_.row_perm.assign(n, 0);
    symbolic_.L_row_ptr.assign(n + 1, 0);
    symbolic_.U_row_ptr.assign(n + 1, 0);

    // Column-oriented (CSC) view of A for left-looking column access.
    std::vector<std::size_t> col_ptr(n + 1, 0);
    std::vector<std::size_t> col_row(A.nnz());
    std::vector<double> col_val(A.nnz());
    {
        std::vector<std::size_t> counts(n, 0);
        for (std::size_t k = 0; k < A.nnz(); ++k) ++counts[A.col_indices()[k]];
        for (std::size_t j = 0; j < n; ++j) col_ptr[j + 1] = col_ptr[j] + counts[j];
        std::vector<std::size_t> next = col_ptr;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                const std::size_t j = A.col_indices()[k];
                col_row[next[j]] = i;
                col_val[next[j]] = A.values()[k];
                ++next[j];
            }
        }
    }

    // Position <-> physical row bookkeeping. Position i is assigned the
    // physical pivot row of EliminationStep i once chosen.
    std::vector<std::size_t> pivot_row(n);
    std::iota(pivot_row.begin(), pivot_row.end(), 0);
    std::vector<std::size_t> pos_of(n);
    std::iota(pos_of.begin(), pos_of.end(), 0);

    // L as columns (rows are strictly below the diagonal, position labels),
    // U as columns (rows are strictly above the diagonal, position labels).
    // Diagonals: L[i][i] == 1 implicit; U[i][i] == pivot kept in reciprocals_.
    // left-looking elimination produces the j-th column of U and L from the
    // forward substitution y = L^{-1} A[:,j]: y[p] for p <= j is the upper
    // entry U[p][j], y[p] for p > j the multiplier source L[p][j].
    std::vector<std::vector<std::size_t>> Lcol_rows(n);
    std::vector<std::vector<double>> Lcol_vals(n);
    std::vector<std::vector<std::size_t>> Ucol_rows(n);
    std::vector<std::vector<double>> Ucol_vals(n);

    // Per-column forward-substitution workspace: y[p] holds the running value
    // at position p, queued tracks membership in the current column's sparse
    // working set (stamp-based removal-free clearing).
    std::vector<double> y(n, 0.0);
    std::vector<unsigned int> queued(n, 0);
    std::set<std::size_t> work;

    std::size_t rank = 0;
    bool singular = false;

    for (std::size_t col = 0; col < n; ++col) {
        const unsigned int stamp = static_cast<unsigned int>(col + 1);

        // Fresh working values for this column; the pivot scan reads the whole
        // active range and must never see stale entries from earlier columns.
        y.assign(n, 0.0);

        std::vector<std::size_t> active_pos;
        active_pos.reserve(64);

        // Scatter A[:, col] into y by current position of each physical row.
        for (std::size_t k = col_ptr[col]; k < col_ptr[col + 1]; ++k) {
            const std::size_t p = pos_of[col_row[k]];
            if (queued[p] != stamp) {
                queued[p] = stamp;
                y[p] = 0.0;
                work.insert(p);
                active_pos.push_back(p);
            }
            y[p] += col_val[k];
        }

        // Left-looking forward solve: y := L^{-1} (P*), processing positions in
        // strictly ascending order. Every contribution into y[p] arrives from
        // a strictly smaller position (L is unit-lower), so ascending order
        // finalizes each entry exactly once.
        while (!work.empty()) {
            auto it = work.begin();
            const std::size_t p = *it;
            work.erase(it);
            if (queued[p] != stamp) continue;  // already finalized this column
            queued[p] = 0;
            const double yp = y[p];
            if (yp == 0.0) continue;
            const auto& lrows = Lcol_rows[p];
            const auto& lvals = Lcol_vals[p];
            for (std::size_t q = 0; q < lrows.size(); ++q) {
                const std::size_t r = lrows[q];
                if (queued[r] != stamp) {
                    queued[r] = stamp;
                    y[r] = 0.0;
                    work.insert(r);
                    active_pos.push_back(r);
                }
                y[r] -= lvals[q] * yp;
            }
        }

        // Partial pivoting: scan active candidate positions to find max magnitude pivot.
        std::size_t piv = col;
        double best = std::abs(y[col]);
        for (std::size_t p : active_pos) {
            if (p > col) {
                const double v = std::abs(y[p]);
                if (v > best) {
                    best = v;
                    piv = p;
                }
            }
        }

        // Relabel positions col <-> piv everywhere (mirrors the reference dense
        // row permutation): L column row labels across previously factored
        // columns, then the working values.
        if (piv != col) {
            for (std::size_t c = 0; c < col; ++c) {
                for (std::size_t q = 0; q < Lcol_rows[c].size(); ++q) {
                    if (Lcol_rows[c][q] == col) {
                        Lcol_rows[c][q] = piv;
                    } else if (Lcol_rows[c][q] == piv) {
                        Lcol_rows[c][q] = col;
                    }
                }
            }
            std::swap(y[col], y[piv]);
            std::swap(pivot_row[col], pivot_row[piv]);
            pos_of[pivot_row[col]] = col;
            pos_of[pivot_row[piv]] = piv;
        }

        const double pivot = y[col];
        if (std::abs(pivot) <= std::max(tol_.singular_tol(), 1e-14)) {
            singular = true;
            continue;
        }

        // U column col: strictly upper entries (rows < col) from the forward solve.
        // The pivot diagonal is stored separately in reciprocals_.
        {
            auto& uc = Ucol_rows[col];
            auto& uv = Ucol_vals[col];
            for (std::size_t p = 0; p < col; ++p) {
                if (std::abs(y[p]) > tol_.zero_tol()) {
                    uc.push_back(p);
                    uv.push_back(y[p]);
                }
            }
        }

        // L column col: multipliers over rows strictly below the pivot.
        {
            auto& lr = Lcol_rows[col];
            auto& lv = Lcol_vals[col];
            const double inv = 1.0 / pivot;
            for (std::size_t p = col + 1; p < n; ++p) {
                const double lval = y[p] * inv;
                if (std::abs(lval) > tol_.zero_tol()) {
                    lr.push_back(p);
                    lv.push_back(lval);
                }
            }
        }

        reciprocals_[col] = 1.0 / pivot;
        ++rank;
    }

    symbolic_.row_perm = std::move(pivot_row);

    publish_factors(A, rank, singular, Lcol_rows, Lcol_vals, Ucol_rows, Ucol_vals);

    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.factorization_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();
    stats_.rank = static_cast<int>(rank);
    stats_.singular = singular;
    if (!singular && rank == n) {
        stats_.condition_estimate = estimate_condition_number(*this, A);
    } else {
        stats_.condition_estimate = 0.0;
    }
}

void SparseLU::publish_factors(const SparseMatrix& A,
                               std::size_t rank,
                               bool singular,
                               const std::vector<std::vector<std::size_t>>& Lcol_rows,
                               const std::vector<std::vector<double>>& Lcol_vals,
                               const std::vector<std::vector<std::size_t>>& Ucol_rows,
                               const std::vector<std::vector<double>>& Ucol_vals) {
    const std::size_t n = A.rows();
    (void)rank;
    (void)singular;

    // Compact numeric nonzeros of the L columns into (row, col, value) tuples.
    struct Entry {
        std::size_t row;
        std::size_t col;
        double val;
    };
    std::vector<Entry> L_entries;
    for (std::size_t col = 0; col < n; ++col) {
        for (std::size_t q = 0; q < Lcol_rows[col].size(); ++q) {
            if (Lcol_vals[col][q] != 0.0) {
                L_entries.push_back({Lcol_rows[col][q], col, Lcol_vals[col][q]});
            }
        }
    }

    // CSR L rows: unit diagonal at (i, i) plus recorded lower entries.
    std::vector<std::size_t> L_uncompressed(n + 1, 0);
    for (const auto& e : L_entries) ++L_uncompressed[e.row + 1];
    for (std::size_t i = 0; i < n; ++i) {
        L_uncompressed[i + 1] += L_uncompressed[i] + 1;  // +1 unit diagonal
    }
    symbolic_.L_row_ptr.resize(n + 1);
    symbolic_.L_col_indices.resize(L_uncompressed[n]);
    L_values_.assign(L_uncompressed[n], 0.0);
    std::vector<std::size_t> L_pos = L_uncompressed;
    for (std::size_t i = 0; i < n; ++i) {
        symbolic_.L_row_ptr[i] = L_uncompressed[i];
        const std::size_t p = L_pos[i]++;
        symbolic_.L_col_indices[p] = i;
        L_values_[p] = 1.0;
    }
    for (const auto& e : L_entries) {
        const std::size_t p = L_pos[e.row]++;
        symbolic_.L_col_indices[p] = e.col;
        L_values_[p] = e.val;
    }
    symbolic_.L_row_ptr[n] = L_pos[n];

    // U column-major entries → CSR rows via transpose.
    // For each column j, Ucol_rows[j] holds rows p < j with |value| > zero_tol.
    // Transpose iterates columns ascending → within each row the column labels
    // arrive in ascending order (matching the dense LU convention).
    std::vector<std::size_t> U_row_sizes(n, 0);
    for (std::size_t col = 0; col < n; ++col) {
        for (std::size_t q = 0; q < Ucol_rows[col].size(); ++q) {
            ++U_row_sizes[Ucol_rows[col][q]];
        }
    }
    std::size_t nnz_U = 0;
    symbolic_.U_row_ptr.assign(n + 1, 0);
    for (std::size_t i = 0; i < n; ++i) {
        symbolic_.U_row_ptr[i] = nnz_U;
        nnz_U += U_row_sizes[i];
    }
    symbolic_.U_row_ptr[n] = nnz_U;
    symbolic_.U_col_indices.resize(nnz_U);
    U_values_.assign(nnz_U, 0.0);
    {
        std::vector<std::size_t> pos = symbolic_.U_row_ptr;
        for (std::size_t col = 0; col < n; ++col) {
            for (std::size_t q = 0; q < Ucol_rows[col].size(); ++q) {
                const std::size_t p = pos[Ucol_rows[col][q]]++;
                symbolic_.U_col_indices[p] = col;
                U_values_[p] = Ucol_vals[col][q];
            }
        }
    }

    // Column-oriented (CSC) transposes for O(nnz) transpose substitutions.
    std::vector<std::size_t> cntL(n, 0), cntU(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            ++cntL[symbolic_.L_col_indices[k]];
        }
        for (std::size_t k = symbolic_.U_row_ptr[i]; k < symbolic_.U_row_ptr[i + 1]; ++k) {
            ++cntU[symbolic_.U_col_indices[k]];
        }
    }
    symbolic_.L_col_ptr.assign(n + 1, 0);
    symbolic_.U_col_ptr.assign(n + 1, 0);
    for (std::size_t j = 0; j < n; ++j) {
        symbolic_.L_col_ptr[j + 1] = symbolic_.L_col_ptr[j] + cntL[j];
        symbolic_.U_col_ptr[j + 1] = symbolic_.U_col_ptr[j] + cntU[j];
    }
    symbolic_.L_row_indices.assign(symbolic_.L_col_ptr[n], 0);
    symbolic_.L_csc_values.assign(symbolic_.L_col_ptr[n], 0.0);
    symbolic_.U_row_indices.assign(symbolic_.U_col_ptr[n], 0);
    symbolic_.U_csc_values.assign(symbolic_.U_col_ptr[n], 0.0);
    {
        std::vector<std::size_t> posL = symbolic_.L_col_ptr;
        std::vector<std::size_t> posU = symbolic_.U_col_ptr;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
                const std::size_t p = posL[symbolic_.L_col_indices[k]]++;
                symbolic_.L_row_indices[p] = i;
                symbolic_.L_csc_values[p] = L_values_[k];
            }
            for (std::size_t k = symbolic_.U_row_ptr[i]; k < symbolic_.U_row_ptr[i + 1]; ++k) {
                const std::size_t p = posU[symbolic_.U_col_indices[k]]++;
                symbolic_.U_row_indices[p] = i;
                symbolic_.U_csc_values[p] = U_values_[k];
            }
        }
    }

    compute_etree();
}

void SparseLU::compute_etree() {
    std::size_t n = symbolic_.L_row_ptr.size() - 1;
    symbolic_.etree.assign(n, n);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                symbolic_.etree[j] = i;
            }
        }
    }
}

void SparseLU::solve(std::vector<double>& x) const {
    const std::size_t n = dense_n_;
    if (n == 0 || x.size() != n) return;

    apply_row_perm(x);

    // Forward substitution: L y = x (L unit lower triangular, CSR). In place.
    {
        const auto& rp = symbolic_.L_row_ptr;
        const auto& ci = symbolic_.L_col_indices;
        const auto& lv = L_values_;
        for (std::size_t i = 0; i < n; ++i) {
            double acc = 0.0;
            for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
                const std::size_t j = ci[k];
                if (j != i) acc += lv[k] * x[j];
            }
            x[i] -= acc;
        }
    }

    // Backward substitution: U x = y, diagonal handled by reciprocals_.
    {
        const auto& rp = symbolic_.U_row_ptr;
        const auto& ci = symbolic_.U_col_indices;
        const auto& uv = U_values_;
        for (int ii = static_cast<int>(n) - 1; ii >= 0; --ii) {
            const std::size_t i = static_cast<std::size_t>(ii);
            double acc = 0.0;
            for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
                acc += uv[k] * x[ci[k]];
            }
            if (reciprocals_[i] != 0.0) {
                x[i] = (x[i] - acc) * reciprocals_[i];
            } else {
                x[i] -= acc;
            }
        }
    }

    if (!symbolic_.col_perm.empty()) apply_col_perm_inv(x);
    apply_eta_forward(x);
}

void SparseLU::apply_eta_forward(std::vector<double>& x) const {
    for (const auto& eta : eta_chain_) {
        const double xq = x[static_cast<std::size_t>(eta.column)];
        if (xq == 0.0) continue;
        for (std::size_t k = 0; k < eta.rows.size(); ++k) {
            x[eta.rows[k]] -= eta.vals[k] * xq;
        }
    }
}

void SparseLU::solve_transpose(std::vector<double>& x) const {
    const std::size_t n = dense_n_;
    if (n == 0 || x.size() != n) return;

    apply_eta_transpose(x);
    if (!symbolic_.col_perm.empty()) apply_col_perm(x);

    // z = U^{-T} b using the CSC pattern of U (rows with U[k][i], k < i).
    {
        const auto& cp = symbolic_.U_col_ptr;
        const auto& ri = symbolic_.U_row_indices;
        const auto& uv = symbolic_.U_csc_values;
        for (std::size_t i = 0; i < n; ++i) {
            double acc = 0.0;
            for (std::size_t k = cp[i]; k < cp[i + 1]; ++k) {
                acc += uv[k] * x[ri[k]];
            }
            if (reciprocals_[i] != 0.0) {
                x[i] = (x[i] - acc) * reciprocals_[i];
            } else {
                x[i] -= acc;
            }
        }
    }

    // w = L^{-T} z using the CSC pattern of L (rows k > i).
    {
        const auto& cp = symbolic_.L_col_ptr;
        const auto& ri = symbolic_.L_row_indices;
        const auto& lv = symbolic_.L_csc_values;
        for (int ii = static_cast<int>(n) - 1; ii >= 0; --ii) {
            const std::size_t i = static_cast<std::size_t>(ii);
            double acc = 0.0;
            for (std::size_t k = cp[i]; k < cp[i + 1]; ++k) {
                const std::size_t r = ri[k];
                if (r != i) acc += lv[k] * x[r];
            }
            x[i] -= acc;
        }
    }

    apply_row_perm_inv(x);
}

void SparseLU::apply_eta_transpose(std::vector<double>& x) const {
    for (auto it = eta_chain_.rbegin(); it != eta_chain_.rend(); ++it) {
        double dot = 0.0;
        for (std::size_t k = 0; k < it->rows.size(); ++k) {
            dot += it->vals[k] * x[it->rows[k]];
        }
        x[static_cast<std::size_t>(it->column)] -= dot;
    }
}

void SparseLU::solve_multiple(std::vector<std::vector<double>>& X) const {
    for (auto& x : X) {
        solve(x);
    }
}

FactorizationStats SparseLU::stats() const {
    return stats_;
}

std::unique_ptr<SparseFactorization> SparseLU::clone() const {
    auto copy = std::make_unique<SparseLU>();
    copy->symbolic_ = symbolic_;
    copy->L_values_ = L_values_;
    copy->U_values_ = U_values_;
    copy->reciprocals_ = reciprocals_;
    copy->dense_L_ = dense_L_;
    copy->dense_U_ = dense_U_;
    copy->dense_n_ = dense_n_;
    copy->eta_chain_ = eta_chain_;
    copy->stats_ = stats_;
    copy->tol_ = tol_;
    return copy;
}

bool SparseLU::apply_eta(int column, const std::vector<double>& direction) {
    const std::size_t n = dense_n_;
    const bool prof = std::getenv("HYPERNOVA_SIMPLEX_PROF") != nullptr;
    if (n == 0) { if (prof) std::cerr << "[eta-reject] n==0\n"; return false; }
    if (column < 0 || static_cast<std::size_t>(column) >= n) { if (prof) std::cerr << "[eta-reject] col=" << column << " n=" << n << "\n"; return false; }
    if (direction.size() != n) { if (prof) std::cerr << "[eta-reject] dir_sz=" << direction.size() << " n=" << n << "\n"; return false; }

    const double dq = direction[static_cast<std::size_t>(column)];
    if (!std::isfinite(dq) || std::abs(dq) <= tol_.singular_tol()) {
        if (prof) std::cerr << "[eta-reject] dq=" << dq << " singular_tol=" << tol_.singular_tol() << "\n";
        clear_eta_chain();
        return false;
    }

    EtaUpdate eta;
    eta.column = column;
    bool had_nonzero = false;
    for (std::size_t i = 0; i < n; ++i) {
        const double e = (i == static_cast<std::size_t>(column)) ? 1.0 : 0.0;
        const double w = (direction[i] - e) / dq;
        if (w == 0.0) continue;
        eta.rows.push_back(i);
        eta.vals.push_back(w);
        had_nonzero = true;
    }
    if (!had_nonzero) {
        // direction == e_column: the entering column already equals the
        // replaced basis column, so the basis matrix is unchanged and the
        // update is the identity. Accept as a no-op without touching the chain.
        return true;
    }

    eta_chain_.push_back(std::move(eta));
    return true;
}

void SparseLU::clear_eta_chain() {
    eta_chain_.clear();
}

void SparseLU::reconstruct_factor_space_matrix(std::vector<double>& M) const {
    std::size_t n = dense_n_;
    M.assign(n * n, 0.0);

    std::vector<double> Ld(n * n, 0.0);
    std::vector<double> Ud(n * n, 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        Ld[i * n + i] = 1.0;
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < i) Ld[i * n + j] = L_values_[k];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = symbolic_.U_row_ptr[i]; k < symbolic_.U_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.U_col_indices[k];
            Ud[i * n + j] = U_values_[k];
        }
        if (reciprocals_[i] != 0.0) {
            Ud[i * n + i] = 1.0 / reciprocals_[i];
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double sum = 0.0;
            std::size_t stop = std::min(i + 1, n);
            for (std::size_t k = 0; k < stop; ++k) {
                sum += Ld[i * n + k] * Ud[k * n + j];
            }
            M[i * n + j] = sum;
        }
    }
}

void SparseLU::refactorize_from_dense(const std::vector<double>& M) {
    std::size_t n = symbolic_.row_perm.size();
    dense_n_ = n;
    std::vector<numerical::Triplet> triplets;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double val = M[i * n + j];
            if (std::abs(val) > tol_.zero_tol()) {
                triplets.emplace_back(i, j, val);
            }
        }
    }
    numerical::SparseMatrix Bmat = numerical::SparseMatrix::from_triplets(n, n, triplets);
    symbolic_analysis(Bmat);
    numeric_factorization(Bmat);
}

void SparseLU::update_column(std::size_t col, const std::vector<double>& new_col) {
    std::size_t n = symbolic_.row_perm.size();
    if (n == 0 || col >= n) return;
    if (new_col.size() != n) return;

    std::vector<double> M;
    reconstruct_factor_space_matrix(M);

    for (std::size_t i = 0; i < n; ++i) {
        M[i * n + col] = new_col[i];
    }

    refactorize_from_dense(M);
}

void SparseLU::update_row(std::size_t row, const std::vector<double>& new_row) {
    std::size_t n = symbolic_.row_perm.size();
    if (n == 0 || row >= n) return;
    if (new_row.size() != n) return;

    std::vector<double> M;
    reconstruct_factor_space_matrix(M);

    for (std::size_t j = 0; j < n; ++j) {
        M[row * n + j] = new_row[j];
    }

    refactorize_from_dense(M);
}

void SparseLU::apply_row_perm(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[i] = x[symbolic_.row_perm[i]];
    }
    x = std::move(tmp);
}

void SparseLU::apply_col_perm(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[i] = x[symbolic_.col_perm[i]];
    }
    x = std::move(tmp);
}

void SparseLU::apply_row_perm_inv(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[symbolic_.row_perm[i]] = x[i];
    }
    x = std::move(tmp);
}

void SparseLU::apply_col_perm_inv(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[symbolic_.col_perm[i]] = x[i];
    }
    x = std::move(tmp);
}

// ============================================================================
// SparseCholesky Implementation
// ============================================================================

SparseCholesky::SparseCholesky(const SparseMatrix& A, const ToleranceConfig& tol)
    : tol_(tol) {
    analyze(A);
    factorize(A);
}

void SparseCholesky::analyze(const SparseMatrix& A) {
    symbolic_analysis(A);
}

void SparseCholesky::factorize(const SparseMatrix& A, const ToleranceConfig& tol) {
    tol_ = tol;
    numeric_factorization(A);
}

void SparseCholesky::refactorize(const SparseMatrix& A, const ToleranceConfig& tol) {
    tol_ = tol;
    numeric_factorization(A);
}

void SparseCholesky::symbolic_analysis(const SparseMatrix& A) {
    compute_md_order_and_pattern(A, symbolic_.perm, symbolic_.inv_perm,
                                 symbolic_.L_row_ptr, symbolic_.L_col_indices, interrupt_cb_);

    compute_elimination_tree();
    compute_postorder();
}

void SparseCholesky::numeric_factorization(const SparseMatrix& A) {
    std::size_t n = symbolic_.perm.size();
    std::size_t nnz_L = symbolic_.L_row_ptr[n];

    L_values_.assign(nnz_L, 0.0);
    D_values_.assign(n, 0.0);

    stats_ = FactorizationStats();
    stats_.singular = false;

    auto start_time = std::chrono::high_resolution_clock::now();

    if (n == 0) {
        auto end_time = std::chrono::high_resolution_clock::now();
        stats_.factorization_time_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        stats_.nnz_L = 0;
        stats_.rank = 0;
        stats_.singular = false;
        stats_.condition_estimate = 0.0;
        return;
    }

    const auto B = permute_symmetric(A, symbolic_.inv_perm);

    for (std::size_t i = 0; i < n; ++i) {
        if ((i & 31u) == 0u && interrupted()) {
            auto end_time = std::chrono::high_resolution_clock::now();
            stats_.factorization_time_ms =
                std::chrono::duration<double, std::milli>(end_time - start_time).count();
            stats_.nnz_L = nnz_L;
            stats_.rank = static_cast<int>(i);
            stats_.singular = true;
            return;
        }
        double diag = permuted_value(B, i, i);

        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                double aij = permuted_value(B, i, j);
                double sum = aij;
                std::size_t k_j = symbolic_.L_row_ptr[j];
                std::size_t k_i = symbolic_.L_row_ptr[i];
                while (k_j < symbolic_.L_row_ptr[j + 1] && k_i < k) {
                    if (symbolic_.L_col_indices[k_j] == symbolic_.L_col_indices[k_i]) {
                        sum -= L_values_[k_j] * L_values_[k_i] * D_values_[symbolic_.L_col_indices[k_j]];
                        ++k_j;
                        ++k_i;
                    } else if (symbolic_.L_col_indices[k_j] < symbolic_.L_col_indices[k_i]) {
                        ++k_j;
                    } else {
                        ++k_i;
                    }
                }
                L_values_[k] = sum / D_values_[j];
            } else if (j == i) {
                double sum = 0.0;
                for (std::size_t kk = symbolic_.L_row_ptr[i]; kk < k; ++kk) {
                    sum += L_values_[kk] * L_values_[kk] * D_values_[symbolic_.L_col_indices[kk]];
                }
                double val = diag - sum;
                if (val <= tol_.singular_tol()) {
                    val = tol_.singular_tol();
                    stats_.singular = true;
                }
                D_values_[i] = val;
                L_values_[k] = 1.0;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.factorization_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    stats_.nnz_L = nnz_L;
    stats_.rank = static_cast<int>(n);
    stats_.condition_estimate = (!stats_.singular) ? estimate_condition_number(*this, A) : 0.0;
}

void SparseCholesky::compute_elimination_tree() {
    std::size_t n = symbolic_.L_row_ptr.size() - 1;
    symbolic_.parent.assign(n, n);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                symbolic_.parent[j] = std::min(symbolic_.parent[j], i);
            }
        }
    }
}

void SparseCholesky::compute_postorder() {
    std::size_t n = symbolic_.L_row_ptr.size() - 1;
    symbolic_.postorder.resize(n);
    std::vector<char> visited(n, 0);
    std::size_t idx = 0;
    (void)idx;

    std::function<void(std::size_t)> dfs = [&](std::size_t u) {
        visited[u] = 1;
        for (std::size_t k = symbolic_.L_row_ptr[u]; k < symbolic_.L_row_ptr[u + 1]; ++k) {
            std::size_t v = symbolic_.L_col_indices[k];
            if (v < u && !visited[v]) {
                dfs(v);
            }
        }
        symbolic_.postorder[idx++] = u;
    };

    for (std::size_t i = 0; i < n; ++i) {
        if (!visited[i]) dfs(i);
    }
}

void SparseCholesky::solve(std::vector<double>& x) const {
    std::size_t n = symbolic_.perm.size();

    apply_perm(x);

    for (std::size_t i = 0; i < n; ++i) {
        double sum = x[i];
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                sum -= L_values_[k] * x[j];
            }
        }
        x[i] = sum;
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (D_values_[i] != 0.0) {
            x[i] /= D_values_[i];
        }
    }

    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                x[j] -= L_values_[k] * x[i];
            }
        }
    }

    apply_perm_inv(x);
}

void SparseCholesky::solve_transpose(std::vector<double>& x) const {
    solve(x);
}

void SparseCholesky::solve_multiple(std::vector<std::vector<double>>& X) const {
    for (auto& x : X) {
        solve(x);
    }
}

FactorizationStats SparseCholesky::stats() const {
    return stats_;
}

std::unique_ptr<SparseFactorization> SparseCholesky::clone() const {
    auto copy = std::make_unique<SparseCholesky>();
    copy->symbolic_ = symbolic_;
    copy->L_values_ = L_values_;
    copy->D_values_ = D_values_;
    copy->stats_ = stats_;
    copy->tol_ = tol_;
    return copy;
}

void SparseCholesky::apply_perm(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[i] = x[symbolic_.perm[i]];
    }
    x = std::move(tmp);
}

void SparseCholesky::apply_perm_inv(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[symbolic_.perm[i]] = x[i];
    }
    x = std::move(tmp);
}

// ============================================================================
// SparseLDLT Implementation
// ============================================================================

SparseLDLT::SparseLDLT(const SparseMatrix& A, const ToleranceConfig& tol)
    : tol_(tol) {
    analyze(A);
    factorize(A);
}

void SparseLDLT::analyze(const SparseMatrix& A) {
    symbolic_analysis(A);
}

void SparseLDLT::factorize(const SparseMatrix& A, const ToleranceConfig& tol) {
    tol_ = tol;
    numeric_factorization(A);
}

void SparseLDLT::refactorize(const SparseMatrix& A, const ToleranceConfig& tol) {
    tol_ = tol;
    numeric_factorization(A);
}

void SparseLDLT::symbolic_analysis(const SparseMatrix& A) {
    compute_md_order_and_pattern(A, symbolic_.perm, symbolic_.inv_perm,
                                 symbolic_.L_row_ptr, symbolic_.L_col_indices);

    compute_elimination_tree();
    compute_postorder();
}

void SparseLDLT::numeric_factorization(const SparseMatrix& A) {
    std::size_t n = symbolic_.perm.size();
    std::size_t nnz_L = symbolic_.L_row_ptr[n];

    L_values_.assign(nnz_L, 0.0);
    D_values_.assign(n, 0.0);

    stats_ = FactorizationStats();
    stats_.singular = false;

    auto start_time = std::chrono::high_resolution_clock::now();

    if (n == 0) {
        auto end_time = std::chrono::high_resolution_clock::now();
        stats_.factorization_time_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();
        stats_.nnz_L = 0;
        stats_.rank = 0;
        stats_.singular = false;
        stats_.condition_estimate = 0.0;
        return;
    }

    const auto B = permute_symmetric(A, symbolic_.inv_perm);

    for (std::size_t i = 0; i < n; ++i) {
        double diag = permuted_value(B, i, i);

        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                double aij = permuted_value(B, i, j);
                double sum = aij;
                std::size_t k_j = symbolic_.L_row_ptr[j];
                std::size_t k_i = symbolic_.L_row_ptr[i];
                while (k_j < symbolic_.L_row_ptr[j + 1] && k_i < k) {
                    if (symbolic_.L_col_indices[k_j] == symbolic_.L_col_indices[k_i]) {
                        sum -= L_values_[k_j] * L_values_[k_i] * D_values_[symbolic_.L_col_indices[k_j]];
                        ++k_j;
                        ++k_i;
                    } else if (symbolic_.L_col_indices[k_j] < symbolic_.L_col_indices[k_i]) {
                        ++k_j;
                    } else {
                        ++k_i;
                    }
                }
                if (std::abs(D_values_[j]) > tol_.singular_tol()) {
                    L_values_[k] = sum / D_values_[j];
                } else {
                    L_values_[k] = 0.0;
                }
            } else if (j == i) {
                double sum = 0.0;
                for (std::size_t kk = symbolic_.L_row_ptr[i]; kk < k; ++kk) {
                    sum += L_values_[kk] * L_values_[kk] * D_values_[symbolic_.L_col_indices[kk]];
                }
                double val = diag - sum;
                if (std::abs(val) <= tol_.singular_tol()) {
                    val = (val >= 0) ? tol_.singular_tol() : -tol_.singular_tol();
                    stats_.singular = true;
                }
                D_values_[i] = val;
                L_values_[k] = 1.0;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    stats_.factorization_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    stats_.nnz_L = nnz_L;
    stats_.rank = static_cast<int>(n);
    stats_.condition_estimate = (!stats_.singular) ? estimate_condition_number(*this, A) : 0.0;
}

void SparseLDLT::compute_elimination_tree() {
    std::size_t n = symbolic_.L_row_ptr.size() - 1;
    symbolic_.parent.assign(n, n);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                symbolic_.parent[j] = std::min(symbolic_.parent[j], i);
            }
        }
    }
}

void SparseLDLT::compute_postorder() {
    std::size_t n = symbolic_.L_row_ptr.size() - 1;
    symbolic_.postorder.resize(n);
    std::vector<char> visited(n, 0);
    std::size_t idx = 0;
    (void)idx;

    std::function<void(std::size_t)> dfs = [&](std::size_t u) {
        visited[u] = 1;
        for (std::size_t k = symbolic_.L_row_ptr[u]; k < symbolic_.L_row_ptr[u + 1]; ++k) {
            std::size_t v = symbolic_.L_col_indices[k];
            if (v < u && !visited[v]) {
                dfs(v);
            }
        }
        symbolic_.postorder[idx++] = u;
    };

    for (std::size_t i = 0; i < n; ++i) {
        if (!visited[i]) dfs(i);
    }
}

void SparseLDLT::solve(std::vector<double>& x) const {
    std::size_t n = symbolic_.perm.size();

    apply_perm(x);

    for (std::size_t i = 0; i < n; ++i) {
        double sum = x[i];
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                sum -= L_values_[k] * x[j];
            }
        }
        x[i] = sum;
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (D_values_[i] != 0.0) {
            x[i] /= D_values_[i];
        }
    }

    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        for (std::size_t k = symbolic_.L_row_ptr[i]; k < symbolic_.L_row_ptr[i + 1]; ++k) {
            std::size_t j = symbolic_.L_col_indices[k];
            if (j < static_cast<std::size_t>(i)) {
                x[j] -= L_values_[k] * x[i];
            }
        }
    }

    apply_perm_inv(x);
}

void SparseLDLT::solve_transpose(std::vector<double>& x) const {
    solve(x);
}

void SparseLDLT::solve_multiple(std::vector<std::vector<double>>& X) const {
    for (auto& x : X) {
        solve(x);
    }
}

FactorizationStats SparseLDLT::stats() const {
    return stats_;
}

std::unique_ptr<SparseFactorization> SparseLDLT::clone() const {
    auto copy = std::make_unique<SparseLDLT>();
    copy->symbolic_ = symbolic_;
    copy->L_values_ = L_values_;
    copy->D_values_ = D_values_;
    copy->stats_ = stats_;
    copy->tol_ = tol_;
    return copy;
}

void SparseLDLT::apply_perm(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[i] = x[symbolic_.perm[i]];
    }
    x = std::move(tmp);
}

void SparseLDLT::apply_perm_inv(std::vector<double>& x) const {
    std::vector<double> tmp(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        tmp[symbolic_.perm[i]] = x[i];
    }
    x = std::move(tmp);
}

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<SparseFactorization> create_factorization(FactorizationType type,
                                                           const SparseMatrix& A,
                                                           const ToleranceConfig& tol) {
    switch (type) {
        case FactorizationType::LU:
            return std::make_unique<SparseLU>(A, tol);
        case FactorizationType::Cholesky:
            return std::make_unique<SparseCholesky>(A, tol);
        case FactorizationType::LDLT:
            return std::make_unique<SparseLDLT>(A, tol);
    }
    return std::make_unique<SparseLU>(A, tol);
}

} // namespace hypernova::numerical