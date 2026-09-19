#pragma once

#include <memory>
#include <vector>
#include <string>
#include <cstddef>

namespace hypernova::execution {

enum class ComputeBackendType {
    CPU,
    CUDA,
    HIP,
    SYCL,
    AUTO // per-call cost-model dispatch between CPU and the best GPU backend
};

class IComputeBackend {
public:
    virtual ~IComputeBackend() = default;

    virtual ComputeBackendType type() const = 0;
    virtual std::string name() const = 0;
    virtual bool is_available() const = 0;
    virtual void initialize() = 0;
    virtual void finalize() = 0;

    virtual void spmv(const std::vector<double>& values,
                       const std::vector<std::size_t>& col_indices,
                       const std::vector<std::size_t>& row_ptr,
                       const std::vector<double>& x,
                       std::vector<double>& y) = 0;

    virtual void spmm(const std::vector<double>& values,
                       const std::vector<std::size_t>& col_indices,
                       const std::vector<std::size_t>& row_ptr,
                       const std::vector<double>& B,
                       std::vector<double>& C,
                       std::size_t ncols_B) = 0;

    virtual std::size_t device_memory() const = 0;
    virtual std::size_t free_memory() const = 0;

    virtual bool was_gpu_kernel_executed() const { return false; }
    virtual std::size_t gpu_kernel_executions() const { return 0; }
    virtual void reset_execution_stats() {}
};

class ComputeBackendFactory {
public:
    static std::unique_ptr<IComputeBackend> create_backend(ComputeBackendType type);
    static std::vector<ComputeBackendType> available_backends();
    static ComputeBackendType best_available_backend();
};

// Initializes a throwaway CUDA backend and returns why it is unavailable
// (empty string when a GPU is usable). Useful for diagnostics/CLI output.
std::string cuda_backend_probe_error();

class GPUCostModel {
public:
    struct Decision {
        ComputeBackendType backend = ComputeBackendType::CPU;
        double estimated_cpu_time_ms = 0.0;
        double estimated_gpu_time_ms = 0.0;
        std::string reason;
    };

    static Decision decide(const std::vector<double>& values,
                            const std::vector<std::size_t>& col_indices,
                            const std::vector<std::size_t>& row_ptr,
                            std::size_t nrows,
                            std::size_t ncols,
                            int batch_size = 1,
                            bool force_gpu = false);

    // Injects device parameters measured at runtime (a calibrated CUDA
    // backend feeds these into AutoBackend at initialization). Values <= 0
    // are ignored. Until a device rate is set, decide() always reports CPU:
    // the GPU estimate is only meaningful with measured bandwidths.
    static void set_device_params(double device_spmv_gbps, double h2d_gbps,
                                  double d2h_gbps);

private:
    static double estimate_cpu_spmv(const std::vector<double>& values,
                                     const std::vector<std::size_t>& col_indices,
                                     const std::vector<std::size_t>& row_ptr,
                                     std::size_t nrows);

    static double estimate_gpu_spmv(const std::vector<double>& values,
                                     const std::vector<std::size_t>& col_indices,
                                     const std::vector<std::size_t>& row_ptr,
                                     std::size_t nrows,
                                     int batch_size);
};

} // namespace hypernova::execution