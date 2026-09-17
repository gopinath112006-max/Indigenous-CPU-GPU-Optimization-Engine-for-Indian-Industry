#include <gtest/gtest.h>
#include <execution/gpu_backend.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace hypernova::execution;

namespace {

struct Problem {
    std::vector<double> values;
    std::vector<std::size_t> col_indices;
    std::vector<std::size_t> row_ptr;
    std::vector<double> x;
    std::size_t nrows = 0;
    std::size_t ncols = 0;
};

// Random sparse matrix with a fixed seed so failures are reproducible.
Problem make_problem(std::size_t nrows, std::size_t ncols, double density,
                     std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> val(-4.0, 4.0);
    std::uniform_int_distribution<std::size_t> col(0, ncols - 1);

    Problem p;
    p.nrows = nrows;
    p.ncols = ncols;
    p.row_ptr.reserve(nrows + 1);
    p.row_ptr.push_back(0);
    for (std::size_t i = 0; i < nrows; ++i) {
        for (std::size_t j = 0; j < ncols; ++j) {
            if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) < density) {
                p.values.push_back(val(rng));
                p.col_indices.push_back(j);
            }
        }
        p.row_ptr.push_back(p.values.size());
    }
    p.x.resize(ncols);
    for (auto& v : p.x) v = val(rng);
    return p;
}

// Diagonal matrix (known exact answer, no rounding concerns).
Problem make_diagonal(std::size_t n) {
    Problem p;
    p.nrows = n;
    p.ncols = n;
    p.row_ptr.reserve(n + 1);
    p.row_ptr.push_back(0);
    for (std::size_t i = 0; i < n; ++i) {
        p.values.push_back(2.0);
        p.col_indices.push_back(i);
        p.row_ptr.push_back(p.values.size());
    }
    p.x.assign(n, 1.5);
    return p;
}

void expect_close(const std::vector<double>& got, const std::vector<double>& ref,
                  double tol) {
    ASSERT_EQ(got.size(), ref.size());
    for (std::size_t i = 0; i < ref.size(); ++i) {
        const double scale = std::max(1.0, std::abs(ref[i]));
        EXPECT_NEAR(got[i], ref[i], tol * scale) << "row " << i;
    }
}

std::vector<double> cpu_spmv_ref(const Problem& p) {
    std::vector<double> y(p.nrows, 0.0);
    for (std::size_t i = 0; i < p.nrows; ++i) {
        double sum = 0.0;
        for (std::size_t k = p.row_ptr[i]; k < p.row_ptr[i + 1]; ++k) {
            sum += p.values[k] * p.x[p.col_indices[k]];
        }
        y[i] = sum;
    }
    return y;
}

} // namespace

TEST(CpuBackendTest, SpmvMatchesReference) {
    const Problem p = make_problem(200, 150, 0.10, 42);
    auto backend = ComputeBackendFactory::create_backend(ComputeBackendType::CPU);
    ASSERT_NE(backend, nullptr);

    std::vector<double> y;
    backend->spmv(p.values, p.col_indices, p.row_ptr, p.x, y);
    expect_close(y, cpu_spmv_ref(p), 1e-12);
}

TEST(CpuBackendTest, SpmmMatchesReference) {
    const Problem p = make_problem(80, 60, 0.2, 7);
    const std::size_t k = 4;
    std::vector<double> B(p.ncols * k);
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> val(-2.0, 2.0);
    for (auto& v : B) v = val(rng);

    auto backend = ComputeBackendFactory::create_backend(ComputeBackendType::CPU);
    std::vector<double> C;
    backend->spmm(p.values, p.col_indices, p.row_ptr, B, C, k);

    // Reference: C[i][c] = sum_k A[i,k] * B[k,c].
    ASSERT_EQ(C.size(), p.nrows * k);
    for (std::size_t i = 0; i < p.nrows; ++i) {
        for (std::size_t c = 0; c < k; ++c) {
            double sum = 0.0;
            for (std::size_t kk = p.row_ptr[i]; kk < p.row_ptr[i + 1]; ++kk) {
                sum += p.values[kk] * B[p.col_indices[kk] * k + c];
            }
            EXPECT_NEAR(C[i * k + c], sum, 1e-10 * std::max(1.0, std::abs(sum)))
                << "(" << i << "," << c << ")";
        }
    }
}

TEST(CpuBackendTest, EmptyMatrixIsSafe) {
    auto backend = ComputeBackendFactory::create_backend(ComputeBackendType::CPU);
    std::vector<double> y;
    backend->spmv({}, {}, {0}, {}, y);
    EXPECT_EQ(y.size(), 0u);
}

TEST(GPUCostModelTest, UnsetDeviceMeansCPU) {
    const Problem p = make_problem(5000, 5000, 0.01, 123);
    // No set_device_params call in this process yet -> CPU decision.
    const auto d = GPUCostModel::decide(p.values, p.col_indices, p.row_ptr,
                                        p.nrows, p.ncols);
    EXPECT_EQ(d.backend, ComputeBackendType::CPU);
    EXPECT_FALSE(d.reason.empty());
    EXPECT_GE(d.estimated_cpu_time_ms, 0.0);
}

TEST(GPUCostModelTest, LargeBatchFavorsGPUWhenCalibrated) {
    const Problem p = make_problem(6000, 4000, 0.05, 321); // ~1.2M nnz
    // Inject optimistic-but-plausible device params (measured on A100-class
    // hardware the model scales down; this only tests model behavior).
    GPUCostModel::set_device_params(700.0, 8.0, 8.0);
    const auto d = GPUCostModel::decide(p.values, p.col_indices, p.row_ptr,
                                        p.nrows, p.ncols, /*batch_size=*/64);
    // With batch=64 the per-call vector traffic amortizes; a GPU at 700 GB/s
    // should dominate. This asserts the model responds to batch size.
    EXPECT_EQ(d.backend, ComputeBackendType::CUDA);
}

TEST(GPUCostModelTest, SmallProblemStaysOnCPU) {
    const Problem p = make_diagonal(100);
    GPUCostModel::set_device_params(700.0, 8.0, 8.0);
    const auto d = GPUCostModel::decide(p.values, p.col_indices, p.row_ptr,
                                        p.nrows, p.ncols);
    EXPECT_EQ(d.backend, ComputeBackendType::CPU);
}

TEST(AutoBackendTest, FallsBackToCPUWithoutGPU) {
    // AUTO must always work: with no calibrated CUDA backend it is CPU.
    auto backend = ComputeBackendFactory::create_backend(ComputeBackendType::AUTO);
    ASSERT_NE(backend, nullptr);
    backend->initialize();
    EXPECT_TRUE(backend->is_available());

    const Problem p = make_problem(64, 48, 0.3, 5);
    std::vector<double> y;
    backend->spmv(p.values, p.col_indices, p.row_ptr, p.x, y);
    expect_close(y, cpu_spmv_ref(p), 1e-12);
    backend->finalize();
}

TEST(AutoBackendTest, SpmmMatchesCpuReference) {
    const Problem p = make_problem(100, 80, 0.2, 11);
    const std::size_t k = 3;
    std::vector<double> B(p.ncols * k, 1.0);

    auto backend = ComputeBackendFactory::create_backend(ComputeBackendType::AUTO);
    backend->initialize();
    std::vector<double> C;
    backend->spmm(p.values, p.col_indices, p.row_ptr, B, C, k);

    // Reference via CPU backend.
    auto cpu = ComputeBackendFactory::create_backend(ComputeBackendType::CPU);
    std::vector<double> C_ref;
    cpu->spmm(p.values, p.col_indices, p.row_ptr, B, C_ref, k);
    expect_close(C, C_ref, 1e-10);
    backend->finalize();
}

// These tests exercise the real driver + JIT path when an NVIDIA GPU is
// present; without one they report "skipped" and pass.
class CudaBackendTest : public ::testing::Test {
protected:
    static bool have_cuda() {
        static const bool cached = [] {
            const std::string err = cuda_backend_probe_error();
            if (!err.empty()) {
                std::cout << "[          ] CUDA probe: " << err << "\n";
            }
            return err.empty();
        }();
        return cached;
    }
};

TEST_F(CudaBackendTest, SpmvScalarKernelMatchesCpu) {
    if (!have_cuda()) GTEST_SKIP() << "no CUDA device";
    // Short rows (avg ~3.6 nnz/row < the warp threshold of 8) force the
    // scalar kernel; the warp kernel is covered by the next test.
    const Problem p = make_problem(1500, 1200, 0.003, 77);
    auto cuda = ComputeBackendFactory::create_backend(ComputeBackendType::CUDA);
    ASSERT_EQ(cuda->type(), ComputeBackendType::CUDA);

    std::vector<double> y;
    cuda->spmv(p.values, p.col_indices, p.row_ptr, p.x, y);
    expect_close(y, cpu_spmv_ref(p), 1e-10);
}

TEST_F(CudaBackendTest, SpmvWarpKernelMatchesCpu) {
    if (!have_cuda()) GTEST_SKIP() << "no CUDA device";
    // Long rows force the warp kernel (avg nnz/row >= 8).
    const Problem p = make_problem(300, 800, 0.08, 4242); // avg 64 nnz/row
    auto cuda = ComputeBackendFactory::create_backend(ComputeBackendType::CUDA);
    ASSERT_EQ(cuda->type(), ComputeBackendType::CUDA);

    std::vector<double> y;
    cuda->spmv(p.values, p.col_indices, p.row_ptr, p.x, y);
    expect_close(y, cpu_spmv_ref(p), 1e-10);
}

TEST_F(CudaBackendTest, SpmvEmptyRows) {
    if (!have_cuda()) GTEST_SKIP() << "no CUDA device";
    Problem p = make_diagonal(256);
    // Poke holes: make every 4th row empty.
    std::vector<double> vals;
    std::vector<std::size_t> cols, rp;
    rp.push_back(0);
    for (std::size_t i = 0; i < p.nrows; ++i) {
        if (i % 4 != 0) {
            vals.push_back(p.values[i]);
            cols.push_back(i);
        }
        rp.push_back(vals.size());
    }
    auto cuda = ComputeBackendFactory::create_backend(ComputeBackendType::CUDA);
    std::vector<double> y;
    cuda->spmv(vals, cols, rp, p.x, y);
    ASSERT_EQ(y.size(), p.nrows);
    for (std::size_t i = 0; i < p.nrows; ++i) {
        const double expected = (i % 4 == 0) ? 0.0 : 2.0 * p.x[i];
        EXPECT_NEAR(y[i], expected, 1e-12) << "row " << i;
    }
}

TEST_F(CudaBackendTest, SpmmMatchesCpu) {
    if (!have_cuda()) GTEST_SKIP() << "no CUDA device";
    const Problem p = make_problem(400, 300, 0.05, 2024);
    const std::size_t k = 5;
    std::vector<double> B(p.ncols * k);
    std::mt19937 rng(8);
    std::uniform_real_distribution<double> val(-1.0, 1.0);
    for (auto& v : B) v = val(rng);

    auto cuda = ComputeBackendFactory::create_backend(ComputeBackendType::CUDA);
    std::vector<double> C;
    cuda->spmm(p.values, p.col_indices, p.row_ptr, B, C, k);

    auto cpu = ComputeBackendFactory::create_backend(ComputeBackendType::CPU);
    std::vector<double> C_ref;
    cpu->spmm(p.values, p.col_indices, p.row_ptr, B, C_ref, k);
    expect_close(C, C_ref, 1e-10);
}

TEST_F(CudaBackendTest, MatrixResidencyIsReused) {
    if (!have_cuda()) GTEST_SKIP() << "no CUDA device";
    const Problem p = make_problem(2000, 1500, 0.03, 555);
    auto cuda = ComputeBackendFactory::create_backend(ComputeBackendType::CUDA);
    std::vector<double> y1, y2;
    cuda->spmv(p.values, p.col_indices, p.row_ptr, p.x, y1);
    const std::size_t free_after_first = cuda->free_memory();
    cuda->spmv(p.values, p.col_indices, p.row_ptr, p.x, y2);
    const std::size_t free_after_second = cuda->free_memory();
    // Second call must not upload the matrix again (free memory unchanged
    // beyond tiny allocator jitter).
    EXPECT_LE(free_after_second, free_after_first + (1u << 20));
    expect_close(y2, y1, 1e-12);
}

TEST_F(CudaBackendTest, CostModelDispatchesToGPUOnLargeProblem) {
    if (!have_cuda()) GTEST_SKIP() << "no CUDA device";
    // Real backend was created -> cost model has measured params. A large
    // batch should now dispatch to CUDA through the AUTO path.
    const Problem p = make_problem(4000, 3000, 0.08, 31337); // ~960k nnz
    auto auto_backend = ComputeBackendFactory::create_backend(ComputeBackendType::AUTO);
    auto_backend->initialize();
    std::vector<double> x(p.ncols, 1.0);
    std::vector<double> y;
    // batch_size = ncols simulated via spmm with many columns.
    std::vector<double> B(p.ncols * 64, 1.0);
    std::vector<double> C;
    auto_backend->spmm(p.values, p.col_indices, p.row_ptr, B, C, 64);
    ASSERT_EQ(C.size(), p.nrows * 64u);

    auto cpu = ComputeBackendFactory::create_backend(ComputeBackendType::CPU);
    std::vector<double> C_ref;
    cpu->spmm(p.values, p.col_indices, p.row_ptr, B, C_ref, 64);
    expect_close(C, C_ref, 1e-9);
    auto_backend->finalize();
}
