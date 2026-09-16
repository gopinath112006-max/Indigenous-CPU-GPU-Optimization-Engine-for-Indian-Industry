#include "gpu_backend.hpp"
#include <algorithm>
#include <cmath>

namespace hypernova::execution {

class CPUBackend : public IComputeBackend {
public:
    ComputeBackendType type() const override { return ComputeBackendType::CPU; }
    std::string name() const override { return "CPU"; }
    bool is_available() const override { return true; }
    void initialize() override {}
    void finalize() override {}

    void spmv(const std::vector<double>& values,
               const std::vector<std::size_t>& col_indices,
               const std::vector<std::size_t>& row_ptr,
               const std::vector<double>& x,
               std::vector<double>& y) override {
        std::size_t nrows = row_ptr.size() - 1;
        y.assign(nrows, 0.0);
        for (std::size_t i = 0; i < nrows; ++i) {
            double sum = 0.0;
            for (std::size_t k = row_ptr[i]; k < row_ptr[i + 1]; ++k) {
                sum += values[k] * x[col_indices[k]];
            }
            y[i] = sum;
        }
    }

    void spmm(const std::vector<double>& values,
               const std::vector<std::size_t>& col_indices,
               const std::vector<std::size_t>& row_ptr,
               const std::vector<double>& B,
               std::vector<double>& C,
               std::size_t ncols_B) override {
        std::size_t nrows = row_ptr.size() - 1;
        C.assign(nrows * ncols_B, 0.0);
        for (std::size_t i = 0; i < nrows; ++i) {
            for (std::size_t k = row_ptr[i]; k < row_ptr[i + 1]; ++k) {
                std::size_t j = col_indices[k];
                double a = values[k];
                for (std::size_t c = 0; c < ncols_B; ++c) {
                    C[i * ncols_B + c] += a * B[j * ncols_B + c];
                }
            }
        }
    }

    std::size_t device_memory() const override { return 0; }
    std::size_t free_memory() const override { return 0; }
};

std::unique_ptr<IComputeBackend> ComputeBackendFactory::create_backend(ComputeBackendType type) {
    switch (type) {
        case ComputeBackendType::CPU:
            return std::make_unique<CPUBackend>();
        case ComputeBackendType::CUDA:
        case ComputeBackendType::HIP:
        case ComputeBackendType::SYCL:
        default:
            return std::make_unique<CPUBackend>();
    }
}

std::vector<ComputeBackendType> ComputeBackendFactory::available_backends() {
    return {ComputeBackendType::CPU};
}

ComputeBackendType ComputeBackendFactory::best_available_backend() {
    return ComputeBackendType::CPU;
}

GPUCostModel::Decision GPUCostModel::decide(const std::vector<double>& values,
                                              const std::vector<std::size_t>& col_indices,
                                              const std::vector<std::size_t>& row_ptr,
                                              std::size_t nrows,
                                              std::size_t ncols,
                                              int batch_size) {
    Decision decision;
    decision.backend = ComputeBackendType::CPU;

    double nnz = values.size();
    (void)ncols;
    double density = nnz / (nrows * ncols);
    (void)density;

    decision.estimated_cpu_time_ms = estimate_cpu_spmv(values, col_indices, row_ptr, nrows) * batch_size;
    decision.estimated_gpu_time_ms = estimate_gpu_spmv(values, col_indices, row_ptr, nrows, batch_size);

    if (decision.estimated_gpu_time_ms < decision.estimated_cpu_time_ms * 0.5 && nrows > 1000) {
        decision.backend = ComputeBackendType::CUDA;
        decision.reason = "GPU estimated to be >2x faster for large sparse matrix";
    } else {
        decision.reason = "CPU preferred for small/medium matrices or when GPU overhead dominates";
    }

    return decision;
}

double GPUCostModel::estimate_cpu_spmv(const std::vector<double>& values,
                                         const std::vector<std::size_t>& /*col_indices*/,
                                         const std::vector<std::size_t>& /*row_ptr*/,
                                         std::size_t /*nrows*/) {
    double nnz = values.size();
    return nnz * 0.001;
}

double GPUCostModel::estimate_gpu_spmv(const std::vector<double>& values,
                                         const std::vector<std::size_t>& /*col_indices*/,
                                         const std::vector<std::size_t>& /*row_ptr*/,
                                         std::size_t /*nrows*/,
                                         int batch_size) {
    double nnz = values.size();
    double launch_overhead = 0.05;
    double compute = nnz * 0.0001 * batch_size;
    return launch_overhead + compute;
}

} // namespace hypernova::execution