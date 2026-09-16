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
    SYCL
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
};

class ComputeBackendFactory {
public:
    static std::unique_ptr<IComputeBackend> create_backend(ComputeBackendType type);
    static std::vector<ComputeBackendType> available_backends();
    static ComputeBackendType best_available_backend();
};

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
                            int batch_size = 1);

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