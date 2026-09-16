#include <gtest/gtest.h>
#include <numerical/sparse_matrix.hpp>
#include <numerical/tolerance.hpp>
#include <numerical/scaling.hpp>
#include <numerical/factorization.hpp>
#include <numerical/refinement.hpp>
#include <cmath>

using namespace hypernova::numerical;

TEST(SparseMatrixTest, CSRConstruction) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {0, 1, 2.0},
        {1, 0, 3.0}, {1, 2, 4.0},
        {2, 1, 5.0}, {2, 2, 6.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(3, 3, triplets, StorageOrder::CSR);

    EXPECT_EQ(A.rows(), 3);
    EXPECT_EQ(A.cols(), 3);
    EXPECT_EQ(A.nnz(), 6);
    EXPECT_EQ(A.order(), StorageOrder::CSR);

    EXPECT_DOUBLE_EQ(A.get(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(A.get(0, 1), 2.0);
    EXPECT_DOUBLE_EQ(A.get(1, 0), 3.0);
    EXPECT_DOUBLE_EQ(A.get(1, 2), 4.0);
    EXPECT_DOUBLE_EQ(A.get(2, 1), 5.0);
    EXPECT_DOUBLE_EQ(A.get(2, 2), 6.0);
}

TEST(SparseMatrixTest, MatrixVectorMultiply) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {0, 1, 2.0},
        {1, 0, 3.0}, {1, 2, 4.0},
        {2, 1, 5.0}, {2, 2, 6.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(3, 3, triplets, StorageOrder::CSR);
    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<double> y = A.multiply(x);

    EXPECT_DOUBLE_EQ(y[0], 1.0 * 1.0 + 2.0 * 2.0);
    EXPECT_DOUBLE_EQ(y[1], 3.0 * 1.0 + 4.0 * 3.0);
    EXPECT_DOUBLE_EQ(y[2], 5.0 * 2.0 + 6.0 * 3.0);
}

TEST(SparseMatrixTest, Transpose) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {0, 1, 2.0},
        {1, 0, 3.0}, {1, 2, 4.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 3, triplets, StorageOrder::CSR);
    SparseMatrix At = A.transposed();

    EXPECT_EQ(At.rows(), 3);
    EXPECT_EQ(At.cols(), 2);
    EXPECT_DOUBLE_EQ(At.get(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(At.get(1, 0), 2.0);
    EXPECT_DOUBLE_EQ(At.get(0, 1), 3.0);
    EXPECT_DOUBLE_EQ(At.get(2, 1), 4.0);
}

TEST(SparseMatrixTest, SumDuplicates) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {0, 0, 2.0},
        {1, 1, 3.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    EXPECT_DOUBLE_EQ(A.get(0, 0), 3.0);
    EXPECT_DOUBLE_EQ(A.get(1, 1), 3.0);
}

TEST(SparseMatrixTest, RemoveZeros) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {0, 1, 1e-20},
        {1, 0, 3.0}, {1, 1, 4.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    A.remove_zeros(1e-15);
    EXPECT_EQ(A.nnz(), 3);
}

TEST(ToleranceTest, Defaults) {
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    EXPECT_DOUBLE_EQ(tol.feasibility_tol(), 1e-9);
    EXPECT_DOUBLE_EQ(tol.optimality_tol(), 1e-9);
    EXPECT_DOUBLE_EQ(tol.integrality_tol(), 1e-6);
    EXPECT_TRUE(tol.is_zero(1e-16));
    EXPECT_FALSE(tol.is_zero(1e-14));
    EXPECT_TRUE(tol.is_feasible(1e-10));
    EXPECT_FALSE(tol.is_feasible(1e-8));
    EXPECT_TRUE(tol.is_integral(1.0 + 1e-7));
    EXPECT_FALSE(tol.is_integral(1.0 + 1e-5));
}

TEST(ToleranceTest, LooseDefaults) {
    ToleranceConfig tol = ToleranceConfig::loose_defaults();
    EXPECT_DOUBLE_EQ(tol.feasibility_tol(), 1e-7);
    EXPECT_DOUBLE_EQ(tol.optimality_tol(), 1e-7);
}

TEST(ToleranceTest, TightDefaults) {
    ToleranceConfig tol = ToleranceConfig::tight_defaults();
    EXPECT_DOUBLE_EQ(tol.feasibility_tol(), 1e-11);
    EXPECT_DOUBLE_EQ(tol.optimality_tol(), 1e-11);
}

TEST(ScalingTest, GeometricScaling) {
    std::vector<Triplet> triplets = {
        {0, 0, 1000.0}, {0, 1, 0.001},
        {1, 0, 0.001}, {1, 1, 1000.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    MatrixScaler scaler(ScalingMethod::GEOMETRIC);
    ScalingResult result = scaler.compute_scales(A);

    EXPECT_GT(result.max_row_scale, 0.0);
    EXPECT_GT(result.max_col_scale, 0.0);
    EXPECT_TRUE(result.converged);
    EXPECT_GT(result.iterations, 0);
}

TEST(ScalingTest, ApplyScales) {
    std::vector<Triplet> triplets = {
        {0, 0, 2.0}, {0, 1, 3.0},
        {1, 0, 4.0}, {1, 1, 5.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    std::vector<double> rhs = {10.0, 20.0};
    std::vector<double> obj = {1.0, 2.0};
    std::vector<double> lb = {0.0, 0.0};
    std::vector<double> ub = {100.0, 100.0};

    MatrixScaler scaler(ScalingMethod::GEOMETRIC);
    ScalingResult result = scaler.compute_scales(A);

    scaler.apply_scales(A, rhs, obj, lb, ub, result);

    EXPECT_NE(A.get(0, 0), 2.0);
    EXPECT_NE(A.get(1, 1), 5.0);
}

TEST(ScalingTest, CurtisReidScaling) {
    std::vector<Triplet> triplets = {
        {0, 0, 1000.0}, {0, 1, 0.001},
        {1, 0, 0.001}, {1, 1, 1000.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    MatrixScaler scaler(ScalingMethod::CURTIS_REID);
    ScalingResult result = scaler.compute_scales(A);

    EXPECT_GT(result.max_row_scale, 0.0);
    EXPECT_GT(result.max_col_scale, 0.0);
}

TEST(ScalingTest, EquilibrationScaling) {
    std::vector<Triplet> triplets = {
        {0, 0, 1000.0}, {0, 1, 0.001},
        {1, 0, 0.001}, {1, 1, 1000.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    MatrixScaler scaler(ScalingMethod::EQUILIBRATION);
    ScalingResult result = scaler.compute_scales(A);

    EXPECT_GT(result.max_row_scale, 0.0);
    EXPECT_GT(result.max_col_scale, 0.0);
}

TEST(FactorizationTest, LUCreate) {
    std::vector<Triplet> triplets = {
        {0, 0, 2.0}, {0, 1, 1.0},
        {1, 0, 1.0}, {1, 1, 2.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    auto lu = create_factorization(FactorizationType::LU, A, tol);
    EXPECT_EQ(lu->type(), FactorizationType::LU);

    std::vector<double> x = {1.0, 2.0};
    lu->solve(x);
    EXPECT_EQ(x.size(), 2);
}

TEST(FactorizationTest, CholeskyCreate) {
    std::vector<Triplet> triplets = {
        {0, 0, 4.0}, {0, 1, 1.0},
        {1, 0, 1.0}, {1, 1, 3.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    auto chol = create_factorization(FactorizationType::Cholesky, A, tol);
    EXPECT_EQ(chol->type(), FactorizationType::Cholesky);

    std::vector<double> x = {1.0, 2.0};
    chol->solve(x);
    EXPECT_EQ(x.size(), 2);
}

TEST(FactorizationTest, LDLTCreate) {
    std::vector<Triplet> triplets = {
        {0, 0, 4.0}, {0, 1, 1.0},
        {1, 0, 1.0}, {1, 1, 3.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    auto ldlt = create_factorization(FactorizationType::LDLT, A, tol);
    EXPECT_EQ(ldlt->type(), FactorizationType::LDLT);

    std::vector<double> x = {1.0, 2.0};
    ldlt->solve(x);
    EXPECT_EQ(x.size(), 2);
}

TEST(FactorizationTest, ConditionEstimateIdentity) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {1, 1, 1.0}, {2, 2, 1.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(3, 3, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    auto lu = create_factorization(FactorizationType::LU, A, tol);
    EXPECT_FALSE(lu->stats().singular);
    EXPECT_NEAR(1.0, lu->stats().condition_estimate, 1e-2);
}

TEST(FactorizationTest, ConditionEstimateIllConditioned) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {1, 1, 1e-6}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    auto lu = create_factorization(FactorizationType::LU, A, tol);
    EXPECT_FALSE(lu->stats().singular);
    EXPECT_GT(lu->stats().condition_estimate, 1e3);

    auto chol = create_factorization(FactorizationType::Cholesky, A, tol);
    EXPECT_FALSE(chol->stats().singular);
    EXPECT_GT(chol->stats().condition_estimate, 1e3);

    auto ldlt = create_factorization(FactorizationType::LDLT, A, tol);
    EXPECT_FALSE(ldlt->stats().singular);
    EXPECT_GT(ldlt->stats().condition_estimate, 1e3);
}

TEST(FactorizationTest, ConditionEstimateSingularIsZero) {
    std::vector<Triplet> triplets = {
        {0, 0, 1.0}, {0, 1, 1.0},
        {1, 0, 1.0}, {1, 1, 1.0}
    };

    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    auto lu = create_factorization(FactorizationType::LU, A, tol);
    EXPECT_TRUE(lu->stats().singular);
    EXPECT_DOUBLE_EQ(0.0, lu->stats().condition_estimate);
}

namespace {

SparseMatrix hilbert_matrix(std::size_t n) {
    std::vector<Triplet> triplets;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            triplets.push_back({i, j, 1.0 / static_cast<double>(i + j + 1)});
        }
    }
    return SparseMatrix::from_triplets(n, n, triplets, StorageOrder::CSR);
}

double relative_max_norm(const SparseMatrix& A, const std::vector<double>& x,
                         const std::vector<double>& b, std::vector<double>& r) {
    std::vector<double> ax = A.multiply(x);
    double norm_b = 0.0;
    for (double v : b) norm_b = std::max(norm_b, std::abs(v));
    double rel = 0.0;
    for (std::size_t i = 0; i < b.size(); ++i) {
        r[i] = b[i] - ax[i];
        rel = std::max(rel, std::abs(r[i]));
    }
    return rel / std::max(norm_b, 1e-300);
}

}  // namespace

TEST(FactorizationTest, IterativeRefinementConverges) {
    SparseMatrix A = hilbert_matrix(5);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    std::vector<double> x_true = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> b = A.multiply(x_true);

    auto lu = create_factorization(FactorizationType::LU, A, tol);

    std::vector<double> x = b;
    lu->solve(x);

    std::vector<double> r(b.size(), 0.0);
    double before = relative_max_norm(A, x, b, r);

    int corrections = iterative_refinement(*lu, A, x, b, 1e-12, 8);

    double after_rel = 0.0;
    {
        std::vector<double> ax = A.multiply(x);
        double norm_b = 0.0;
        for (double v : b) norm_b = std::max(norm_b, std::abs(v));
        for (std::size_t i = 0; i < b.size(); ++i) {
            after_rel = std::max(after_rel, std::abs(b[i] - ax[i]));
        }
        after_rel /= norm_b;
    }

    EXPECT_GE(corrections, 0);
    EXPECT_LE(after_rel, before + 1e-16);
    EXPECT_LT(after_rel, 1e-9);
}

TEST(FactorizationTest, IterativeRefinementSolutionQuality) {
    // Diag(1, 1e-8): ill-conditioned but non-singular; the refined solve should
    // stay close to the true solution within forward-error bounds.
    std::vector<Triplet> triplets = {{0, 0, 1.0}, {1, 1, 1e-8}};
    SparseMatrix A = SparseMatrix::from_triplets(2, 2, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    auto lu = create_factorization(FactorizationType::LU, A, tol);
    std::vector<double> b = {1.0, 1.0};
    std::vector<double> x = b;
    lu->solve(x);
    iterative_refinement(*lu, A, x, b, 1e-12, 8);

    EXPECT_NEAR(x[0], 1.0, 1e-7);
    EXPECT_NEAR(x[1], 1e8, 1e8 * 1e-4);
}

TEST(FactorizationTest, DenseSolveRefined) {
    const std::size_t n = 4;
    std::vector<double> H(n * n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            H[i * n + j] = 1.0 / static_cast<double>(i + j + 1);
        }
    }
    SparseMatrix As = hilbert_matrix(n);
    std::vector<double> x_true = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> b = As.multiply(x_true);

    std::vector<double> sol;
    int corrections = dense_solve_refined(H, n, b, sol, 1e-12, 8);

    EXPECT_GE(corrections, 0);
    ASSERT_EQ(sol.size(), n);
    double rel = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        rel = std::max(rel, std::abs(x_true[i] - sol[i]) / std::max(1.0, std::abs(x_true[i])));
    }
    EXPECT_LT(rel, 1e-6);
}

namespace {

// Deterministic pseudo-random number in [-1, 1).
double rand_sym_step() {
    static std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    state = 6364136223846793005ULL * state + 1442695040888963407ULL;
    return (static_cast<double>((state >> 11) & 0x1FFFFF) / 2097152.0) * 2.0 - 1.0;
}

// Build B^T * B + diag_shift * I, a symmetric positive-definite matrix,
// stored in full CSR form (both triangles).
SparseMatrix random_spd(std::size_t n, double diag_shift) {
    std::vector<double> B(n * n);
    for (std::size_t i = 0; i < n * n; ++i) {
        B[i] = rand_sym_step();
    }
    std::vector<Triplet> triplets;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double s = (i == j) ? diag_shift : 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                s += B[k * n + i] * B[k * n + j];
            }
            if (std::abs(s) > 1e-18) {
                triplets.emplace_back(i, j, s);
            }
        }
    }
    return SparseMatrix::from_triplets(n, n, triplets, StorageOrder::CSR);
}

// Relative max-norm residual of A*x == b.
double max_rel_residual(const SparseMatrix& A, const std::vector<double>& x,
                        const std::vector<double>& b) {
    const std::vector<double> ax = A.multiply(x);
    double norm_b = 0.0;
    for (double v : b) norm_b = std::max(norm_b, std::abs(v));
    double rel = 0.0;
    for (std::size_t i = 0; i < b.size(); ++i) {
        rel = std::max(rel, std::abs(b[i] - ax[i]));
    }
    return rel / std::max(norm_b, 1e-300);
}

// Reconstruct L*D*L^T from a concrete LDLT factorization and compare it with
// the permuted matrix (P*A*P^T) to validate the symbolic fill-in patterns.
double ldlt_recon_error(const SparseLDLT& fac, const SparseMatrix& A) {
    const std::size_t n = fac.perm().size();
    if (n == 0) return 0.0;
    const auto& perm = fac.perm();
    const auto& lrp = fac.L_row_ptr();
    const auto& lci = fac.L_col_indices();
    const auto& lv = fac.L_values();
    const auto& dv = fac.D_values();

    std::vector<double> Ld(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = lrp[i]; k < lrp[i + 1]; ++k) {
            Ld[i * n + lci[k]] = lv[k];
        }
    }

    double max_err = 0.0;
    double max_ref = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double recon = 0.0;
            for (std::size_t k = 0; k <= std::min(i, j); ++k) {
                recon += Ld[i * n + k] * dv[k] * Ld[j * n + k];
            }
            const double expected = A.get(perm[i], perm[j]);
            max_ref = std::max(max_ref, std::abs(expected));
            max_err = std::max(max_err, std::abs(recon - expected));
        }
    }
    return max_err / std::max(max_ref, 1.0);
}

}  // namespace

TEST(FactorizationTest, SparseLDLTFillInPattern) {
    // Cycle graph 0-1-3-2-0 encoded as a 4x4 SPD matrix. The elimination graph
    // (with an off-diagonal (1,2) pattern pair) requires symbolic fill: the L
    // pattern must be strictly larger than the lower triangle of A. With the
    // minimum-degree ordering used here the factor pattern contains (2,1).
    std::vector<Triplet> triplets = {
        {0, 0, 4.0}, {0, 1, 1.0}, {0, 2, 1.0},
        {1, 0, 1.0}, {1, 1, 4.0}, {1, 3, 1.0},
        {2, 0, 1.0}, {2, 2, 4.0}, {2, 3, 1.0},
        {3, 1, 1.0}, {3, 2, 1.0}, {3, 3, 4.0}
    };
    SparseMatrix A = SparseMatrix::from_triplets(4, 4, triplets, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    SparseLDLT ldlt(A, tol);
    ASSERT_FALSE(ldlt.stats().singular);

    const std::size_t nnz_lower = 8;  // 4 diagonal + 4 stored off-diagonal below
    const std::size_t nnz_L = ldlt.L_row_ptr()[4];
    EXPECT_GT(nnz_L, nnz_lower) << "fill-in must enlarge the lower-triangular pattern";

    bool has_fill = false;
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t k = ldlt.L_row_ptr()[i]; k < ldlt.L_row_ptr()[i + 1]; ++k) {
            const std::size_t j = ldlt.L_col_indices()[k];
            bool stored_lower = false;
            for (std::size_t kk = A.row_ptr()[i]; kk < A.row_ptr()[i + 1]; ++kk) {
                if (A.col_indices()[kk] == j) { stored_lower = true; break; }
            }
            if (j < i && !stored_lower) has_fill = true;
        }
    }
    EXPECT_TRUE(has_fill) << "factor must contain at least one symbolic fill entry";

    // The fill pattern must reproduce A's factors: reconstruct and compare.
    EXPECT_LT(ldlt_recon_error(ldlt, A), 1e-10);

    // And the solve must be accurate.
    std::vector<double> x_true = {1.0, -2.0, 3.0, 4.0};
    std::vector<double> b = A.multiply(x_true);
    std::vector<double> x = b;
    ldlt.solve(x);
    EXPECT_LT(max_rel_residual(A, x, b), 1e-10);

    auto chol = create_factorization(FactorizationType::Cholesky, A, tol);
    EXPECT_FALSE(chol->stats().singular);
    x = b;
    chol->solve(x);
    EXPECT_LT(max_rel_residual(A, x, b), 1e-10);
}

TEST(FactorizationTest, SparseCholeskyRefactorizeReuse) {
    // Pattern-fixed relaxation-style SPD matrices (Laplacian + diagonal shift).
    // The sparsity pattern is identical across refactorizes; only the values
    // change. This mirrors the IPM normal-equation reuse, where the symbolic
    // analysis must be amortized while numeric values evolve.
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();
    const std::size_t n = 8;
    const std::vector<std::pair<std::size_t, std::size_t>> pairs = {
        {0, 1}, {0, 4}, {1, 2}, {1, 5}, {2, 3}, {2, 6}, {3, 7}, {4, 5}, {5, 6}, {6, 7}
    };

    auto make_spd = [&](double w, double diag_extra) {
        std::vector<Triplet> tri;
        for (const auto& [i, j] : pairs) {
            tri.emplace_back(i, j, -w);
            tri.emplace_back(j, i, -w);
            tri.emplace_back(i, i, w);
            tri.emplace_back(j, j, w);
        }
        for (std::size_t i = 0; i < n; ++i) tri.emplace_back(i, i, diag_extra);
        return SparseMatrix::from_triplets(n, n, tri, StorageOrder::CSR);
    };

    const SparseMatrix A1 = make_spd(1.0, 4.0);
    SparseCholesky chol(A1, tol);
    EXPECT_FALSE(chol.stats().singular);

    for (int k = 1; k <= 4; ++k) {
        // Same pattern, new values (shifting weight and diagonal shift).
        const SparseMatrix Ak = make_spd(0.5 + 0.25 * k, 3.0 + k);
        chol.refactorize(Ak, tol);
        EXPECT_FALSE(chol.stats().singular) << "k=" << k;

        std::vector<double> x_true(n);
        for (std::size_t i = 0; i < n; ++i) x_true[i] = 1.0 + 0.1 * i + 0.1 * k;
        const std::vector<double> b = Ak.multiply(x_true);

        std::vector<double> x = b;
        chol.solve(x);
        EXPECT_LT(max_rel_residual(Ak, x, b), 1e-9) << "reused solve, k=" << k;

        auto fresh = create_factorization(FactorizationType::Cholesky, Ak, tol);
        std::vector<double> xf = b;
        fresh->solve(xf);
        double mx = 0.0, rel = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            mx = std::max(mx, std::abs(x[i]));
            rel = std::max(rel, std::abs(x[i] - xf[i]));
        }
        EXPECT_LT(rel / std::max(mx, 1e-300), 1e-8)
            << "reused factorization must match a fresh one, k=" << k;
    }
}

TEST(FactorizationTest, SparseCholeskyRandomSPD) {
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    for (std::size_t n = 3; n <= 12; ++n) {
        const SparseMatrix A = random_spd(n, static_cast<double>(n));

        std::vector<double> x_true(n);
        for (std::size_t i = 0; i < n; ++i) x_true[i] = rand_sym_step();
        const std::vector<double> b = A.multiply(x_true);

        auto chol = create_factorization(FactorizationType::Cholesky, A, tol);
        EXPECT_FALSE(chol->stats().singular) << "n=" << n;
        EXPECT_EQ(chol->stats().rank, static_cast<int>(n)) << "n=" << n;
        std::vector<double> x = b;
        chol->solve(x);
        EXPECT_LT(max_rel_residual(A, x, b), 1e-9) << "n=" << n;

        auto ldlt = create_factorization(FactorizationType::LDLT, A, tol);
        EXPECT_FALSE(ldlt->stats().singular) << "n=" << n;
        EXPECT_EQ(ldlt->stats().rank, static_cast<int>(n)) << "n=" << n;
        x = b;
        ldlt->solve(x);
        EXPECT_LT(max_rel_residual(A, x, b), 1e-9) << "n=" << n;
    }
}

// Independent dense Gaussian solver used as the reference for the eta-chain
// cross-check below.
std::vector<double> dense_gauss_solve(const std::vector<double>& mat, const std::vector<double>& rhs,
                                      std::size_t n) {
    std::vector<double> g = mat;
    std::vector<double> b = rhs;
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t piv = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(g[r * n + col]) > std::abs(g[piv * n + col])) piv = r;
        }
        if (piv != col) {
            for (std::size_t c = 0; c < n; ++c) std::swap(g[col * n + c], g[piv * n + c]);
            std::swap(b[col], b[piv]);
        }
        const double d = g[col * n + col];
        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = g[r * n + col] / d;
            for (std::size_t c = col; c < n; ++c) g[r * n + c] -= f * g[col * n + c];
            b[r] -= f * b[col];
        }
    }
    std::vector<double> x(n);
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        double s = b[static_cast<std::size_t>(i)];
        for (std::size_t c = static_cast<std::size_t>(i) + 1; c < n; ++c) {
            s -= g[static_cast<std::size_t>(i) * n + c] * x[c];
        }
        x[static_cast<std::size_t>(i)] =
            s / g[static_cast<std::size_t>(i) * n + static_cast<std::size_t>(i)];
    }
    return x;
}

TEST(FactorizationTest, SparseLUEtaChainMatchesDenseRefactor) {
    const std::size_t n = 10;
    const std::size_t steps = 30;
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();

    std::vector<Triplet> tri;
    std::vector<double> M(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const double v =
                (i == j) ? (8.0 + static_cast<double>(n)) : rand_sym_step() * 0.3;
            tri.emplace_back(i, j, v);
            M[i * n + j] = v;
        }
    }
    const SparseMatrix A = SparseMatrix::from_triplets(n, n, tri, StorageOrder::CSR);
    auto lu = create_factorization(FactorizationType::LU, A, tol);
    ASSERT_FALSE(lu->stats().singular);

    auto rel_forward_resid =
        [&](const std::vector<double>& mat, const std::vector<double>& x,
            const std::vector<double>& b) {
            std::vector<double> ax(n, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = 0; j < n; ++j) ax[i] += mat[i * n + j] * x[j];
            }
            double nrm = 0.0, rel = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                nrm = std::max(nrm, std::abs(b[i]));
                rel = std::max(rel, std::abs(ax[i] - b[i]));
            }
            return rel / std::max(nrm, 1e-300);
        };

    for (std::size_t step = 0; step < steps; ++step) {
        const std::size_t q = step % n;
        std::vector<double> a_new(n);
        for (std::size_t i = 0; i < n; ++i) a_new[i] = rand_sym_step() * 2.0;
        a_new[q] += 15.0;

        std::vector<double> d = dense_gauss_solve(M, a_new, n);
        ASSERT_TRUE(lu->apply_eta(static_cast<int>(q), d))
            << "eta update rejected at step=" << step;
        ASSERT_EQ(lu->eta_chain_size(), step + 1) << "step=" << step;

        for (std::size_t i = 0; i < n; ++i) M[i * n + q] = a_new[i];

        std::vector<double> b(n);
        for (std::size_t i = 0; i < n; ++i) b[i] = rand_sym_step() * 2.0;

        std::vector<double> x = b;
        lu->solve(x);
        EXPECT_LT(rel_forward_resid(M, x, b), 1e-8) << "forward solve, step=" << step;

        std::vector<double> t = b;
        lu->solve_transpose(t);
        std::vector<double> mt(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) mt[i] += M[j * n + i] * t[j];
        }
        double nrm = 0.0, rel = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            nrm = std::max(nrm, std::abs(b[i]));
            rel = std::max(rel, std::abs(mt[i] - b[i]));
        }
        EXPECT_LT(rel / std::max(nrm, 1e-300), 1e-8) << "transpose solve, step=" << step;
    }

    // A clone must carry the same eta chain.
    auto clone = lu->clone();
    std::vector<double> b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = rand_sym_step() * 2.0;
    std::vector<double> x = b;
    clone->solve(x);
    EXPECT_LT(rel_forward_resid(M, x, b), 1e-8) << "clone forward solve";

    lu->clear_eta_chain();
    EXPECT_EQ(lu->eta_chain_size(), 0u);
}

TEST(FactorizationTest, SparseLUEtaRejectsSingularDirection) {
    std::vector<Triplet> tri = {
        {0, 0, 4.0}, {0, 1, 1.0}, {1, 0, 1.0}, {1, 1, 4.0}
    };
    const SparseMatrix A = SparseMatrix::from_triplets(2, 2, tri, StorageOrder::CSR);
    ToleranceConfig tol = ToleranceConfig::industrial_defaults();
    auto lu = create_factorization(FactorizationType::LU, A, tol);

    std::vector<double> d = {0.5, 1.0};
    EXPECT_TRUE(lu->apply_eta(1, d));
    EXPECT_EQ(lu->eta_chain_size(), 1u);

    std::vector<double> dzero = {0.0, 0.0};
    EXPECT_FALSE(lu->apply_eta(1, dzero));
    EXPECT_EQ(lu->eta_chain_size(), 0u);

    std::vector<double> dwrong(1, 1.0);
    EXPECT_FALSE(lu->apply_eta(0, dwrong));
}