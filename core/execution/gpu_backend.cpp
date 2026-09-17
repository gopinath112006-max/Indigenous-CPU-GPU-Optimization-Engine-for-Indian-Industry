// CUDA backend for IComputeBackend, built on the CUDA driver API with
// embedded PTX kernels. Requires no CUDA toolkit at build time: nvcuda.dll /
// libcuda.so.1 ships with the NVIDIA display driver, and the PTX is JIT
// compiled when the module is loaded.
//
// Upload policy: the cost model assumes the matrix is resident on the device.
// The first spmv()/spmm() call for a given matrix uploads A once (O(nnz) H2D)
// and amortizes it across every subsequent product, matching how solvers use
// a fixed constraint matrix across many KKT/normal-equation products. x/B and
// y/C are streamed per call.
//
// Error policy: a backend handed out by the factory has passed probing; once
// running, launch/copy failures throw std::runtime_error so callers never
// silently consume zero-filled results.

#include "gpu_backend.hpp"
#include "cuda_driver.hpp"
#include "cuda_kernels_ptx.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hypernova::execution {

using cuda::DevicePtr;
using cuda::DriverApi;

namespace {

// 256 threads/block: 8 warps, good occupancy across supported devices.
constexpr unsigned kSpmvBlock = 256;
constexpr unsigned kWarpPerBlock = kSpmvBlock / 32;
// Warp kernel amortizes better once rows average a handful of nonzeros.
constexpr double kWarpRowThreshold = 8.0;

// Parameter layouts must match the PTX kernels' .param lists exactly: one
// 64-bit word per parameter, passed to cuLaunchKernel as a params array.

// Parameter buffer layouts must match the PTX kernels' .param lists exactly.
struct SpmvArgs {
    std::uint64_t nrows;
    std::uint64_t rp;
    std::uint64_t ci;
    std::uint64_t val;
    std::uint64_t x;
    std::uint64_t y;
};

struct SpmmArgs {
    std::uint64_t nrows;
    std::uint64_t ncb;
    std::uint64_t rp;
    std::uint64_t ci;
    std::uint64_t val;
    std::uint64_t b;
    std::uint64_t c;
};

struct DeviceMatrix {
    DevicePtr row_ptr = 0;
    DevicePtr col_idx = 0;
    DevicePtr values = 0;
    std::size_t nnz = 0;
    std::size_t nrows = 0;
};

void free_matrix(DeviceMatrix& m) {
    if (m.row_ptr) DriverApi::free(m.row_ptr);
    if (m.col_idx) DriverApi::free(m.col_idx);
    if (m.values) DriverApi::free(m.values);
    m = DeviceMatrix{};
}

// Reference CPU implementation shared by every backend choice.
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
        const std::size_t nrows = row_ptr.size() >= 1 ? row_ptr.size() - 1 : 0;
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
        const std::size_t nrows = row_ptr.size() >= 1 ? row_ptr.size() - 1 : 0;
        C.assign(nrows * ncols_B, 0.0);
        for (std::size_t i = 0; i < nrows; ++i) {
            for (std::size_t k = row_ptr[i]; k < row_ptr[i + 1]; ++k) {
                const std::size_t j = col_indices[k];
                const double a = values[k];
                for (std::size_t c = 0; c < ncols_B; ++c) {
                    C[i * ncols_B + c] += a * B[j * ncols_B + c];
                }
            }
        }
    }

    std::size_t device_memory() const override { return 0; }
    std::size_t free_memory() const override { return 0; }
};

class CudaDriverBackend : public IComputeBackend {
public:
    CudaDriverBackend() = default;
    ~CudaDriverBackend() override {
        release_device_data();
        release_modules();
    }

    CudaDriverBackend(const CudaDriverBackend&) = delete;
    CudaDriverBackend& operator=(const CudaDriverBackend&) = delete;

    ComputeBackendType type() const override { return ComputeBackendType::CUDA; }
    std::string name() const override {
        if (!info_.name.empty()) return "CUDA(" + info_.name + ")";
        return "CUDA";
    }
    bool is_available() const override { return available_; }

    void initialize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) return;
        initialized_ = true;

        if (!DriverApi::load()) {
            last_error_ = "CUDA driver not available (nvcuda.dll / libcuda.so.1)";
            return;
        }
        const int count = DriverApi::device_count();
        if (count <= 0) {
            last_error_ = "no CUDA-capable device found";
            return;
        }
        // Pick the device with the largest memory (best heuristic for a solver).
        int best = 0;
        cuda::DeviceInfo best_info;
        for (int d = 0; d < count; ++d) {
            cuda::DeviceInfo di = DriverApi::device_info(d);
            if (di.total_memory > best_info.total_memory) {
                best = d;
                best_info = di;
            }
        }
        std::string error;
        if (!DriverApi::init_context(best, error)) {
            last_error_ = error;
            return;
        }
        device_ = best;
        info_ = best_info;
        available_ = true;
        calibrate();
    }

    void finalize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        release_device_data();
        release_modules();
    }

    void spmv(const std::vector<double>& values,
              const std::vector<std::size_t>& col_indices,
              const std::vector<std::size_t>& row_ptr,
              const std::vector<double>& x,
              std::vector<double>& y) override {
        std::lock_guard<std::mutex> lock(mutex_);
        require_available("spmv");
        if (values.empty() || row_ptr.size() < 2) {
            y.assign(row_ptr.size() >= 1 ? row_ptr.size() - 1 : 0, 0.0);
            return;
        }
        DeviceMatrix& m = resident_matrix(resident_, values, col_indices, row_ptr);
        if (!launch_spmv(m, x, y)) {
            throw std::runtime_error("CUDA spmv failed: " + last_error_);
        }
    }

    void spmm(const std::vector<double>& values,
              const std::vector<std::size_t>& col_indices,
              const std::vector<std::size_t>& row_ptr,
              const std::vector<double>& B,
              std::vector<double>& C,
              std::size_t ncols_B) override {
        std::lock_guard<std::mutex> lock(mutex_);
        require_available("spmm");
        const std::size_t nrows = row_ptr.size() >= 1 ? row_ptr.size() - 1 : 0;
        if (values.empty() || row_ptr.size() < 2 || ncols_B == 0) {
            C.assign(nrows * ncols_B, 0.0);
            return;
        }
        DeviceMatrix& m = resident_matrix(resident_spmm_, values, col_indices, row_ptr);
        if (!launch_spmm(m, B, C, ncols_B)) {
            throw std::runtime_error("CUDA spmm failed: " + last_error_);
        }
    }

    std::size_t device_memory() const override {
        std::size_t free_b = 0, total_b = 0;
        if (DriverApi::memory_info(free_b, total_b)) return total_b;
        return info_.total_memory;
    }

    std::size_t free_memory() const override {
        std::size_t free_b = 0, total_b = 0;
        if (DriverApi::memory_info(free_b, total_b)) return free_b;
        return 0;
    }

    // Measured numbers (nanosecond-accurate wall timing on this device) that
    // the cost model consumes via GPUCostModel::set_device_params.
    double measured_spmv_gbps() const { return measured_spmv_gbps_; }
    double measured_h2d_gbps() const { return calibrated_h2d_gbps_; }
    double measured_d2h_gbps() const { return measured_d2h_gbps_; }
    const std::string& last_error() const { return last_error_; }

private:
    void require_available(const char* op) {
        if (!available_) {
            throw std::runtime_error(std::string("CUDA backend not available for ") + op +
                                     ": " + last_error_);
        }
    }

    // Records `step` plus the driver's last CUresult as the failure reason.
    bool fail(const char* step) {
        last_error_ = std::string(step) + " [" +
                      DriverApi::result_string(DriverApi::last_result()) + "]";
        return false;
    }

    // Returns the device-resident copy of the matrix keyed by the host
    // values pointer, uploading (or re-uploading after a size change) once.
    DeviceMatrix& resident_matrix(std::unordered_map<const void*, DeviceMatrix>& cache,
                                  const std::vector<double>& values,
                                  const std::vector<std::size_t>& col_indices,
                                  const std::vector<std::size_t>& row_ptr) {
        const std::size_t nrows = row_ptr.size() - 1;
        auto it = cache.find(values.data());
        if (it != cache.end()) {
            DeviceMatrix& m = it->second;
            if (m.nnz == values.size() && m.nrows == nrows) return m;
            free_matrix(m);
            cache.erase(it);
        }
        DeviceMatrix m;
        if (!upload_matrix(values, col_indices, row_ptr, m)) {
            throw std::runtime_error("CUDA matrix upload failed: " + last_error_);
        }
        return cache.emplace(values.data(), m).first->second;
    }

    // Each embedded PTX source is a self-contained translation unit (own
    // .version/.target headers), so each must be JIT-compiled as its own
    // module; concatenating them produces an invalid image.
    bool ensure_module() {
        if (module_scalar_ && module_warp_ && module_spmm_) return true;
        release_modules();
        std::string error;
        if (!DriverApi::load_module_ptx(cuda::spmv_scalar_ptx().c_str(), module_scalar_,
                                        error)) {
            last_error_ = "PTX JIT failed (spmv_scalar): " + error;
            release_modules();
            return false;
        }
        if (!DriverApi::get_function(module_scalar_, cuda::kSpmvScalarKernel,
                                     spmv_scalar_)) {
            last_error_ = "kernel entry point missing: spmv_scalar";
            release_modules();
            return false;
        }
        if (!DriverApi::load_module_ptx(cuda::spmv_warp_ptx().c_str(), module_warp_,
                                        error)) {
            last_error_ = "PTX JIT failed (spmv_warp): " + error;
            release_modules();
            return false;
        }
        if (!DriverApi::get_function(module_warp_, cuda::kSpmvWarpKernel, spmv_warp_)) {
            last_error_ = "kernel entry point missing: spmv_warp";
            release_modules();
            return false;
        }
        if (!DriverApi::load_module_ptx(cuda::spmm_ptx().c_str(), module_spmm_, error)) {
            last_error_ = "PTX JIT failed (spmm): " + error;
            release_modules();
            return false;
        }
        if (!DriverApi::get_function(module_spmm_, cuda::kSpmmKernel, spmm_fn_)) {
            last_error_ = "kernel entry point missing: spmm";
            release_modules();
            return false;
        }
        return true;
    }

    void release_modules() {
        // cuModuleUnload invalidates entry-point handles, so they go too.
        if (module_scalar_) DriverApi::unload_module(module_scalar_);
        if (module_warp_) DriverApi::unload_module(module_warp_);
        if (module_spmm_) DriverApi::unload_module(module_spmm_);
        module_scalar_ = module_warp_ = module_spmm_ = nullptr;
        spmv_scalar_ = spmv_warp_ = spmm_fn_ = nullptr;
    }

    bool upload_matrix(const std::vector<double>& values,
                       const std::vector<std::size_t>& col_indices,
                       const std::vector<std::size_t>& row_ptr,
                       DeviceMatrix& out) {
        if (!ensure_module()) return false;
        out.nrows = row_ptr.size() - 1;
        out.nnz = values.size();
        out.row_ptr = DriverApi::allocate(row_ptr.size() * sizeof(std::size_t));
        out.col_idx = DriverApi::allocate(out.nnz * sizeof(std::size_t));
        out.values = DriverApi::allocate(out.nnz * sizeof(double));
        if (!out.row_ptr || !out.col_idx || !out.values) {
            fail("device allocation during matrix upload");
            free_matrix(out);
            return false;
        }
        if (!DriverApi::copy_h2d(out.row_ptr, row_ptr.data(),
                                 row_ptr.size() * sizeof(std::size_t))) {
            fail("H2D copy of row_ptr");
            free_matrix(out);
            return false;
        }
        if (!DriverApi::copy_h2d(out.col_idx, col_indices.data(),
                                 out.nnz * sizeof(std::size_t))) {
            fail("H2D copy of col_indices");
            free_matrix(out);
            return false;
        }
        if (!DriverApi::copy_h2d(out.values, values.data(), out.nnz * sizeof(double))) {
            fail("H2D copy of values");
            free_matrix(out);
            return false;
        }
        return true;
    }

    bool launch_spmv(const DeviceMatrix& m, const std::vector<double>& x,
                     std::vector<double>& y) {
        if (!ensure_module()) return false;
        DevicePtr d_x = DriverApi::allocate(x.size() * sizeof(double));
        DevicePtr d_y = DriverApi::allocate(m.nrows * sizeof(double));
        if (!d_x || !d_y) {
            fail("device allocation for spmv vectors");
            if (d_x) DriverApi::free(d_x);
            if (d_y) DriverApi::free(d_y);
            return false;
        }
        bool ok = DriverApi::copy_h2d(d_x, x.data(), x.size() * sizeof(double));
        if (!ok) fail("H2D copy of x");
        if (ok) {
            ok = DriverApi::memset_d8(d_y, 0, m.nrows * sizeof(double));
            if (!ok) fail("memset of y");
        }

        const double avg_nnz =
            m.nrows > 0 ? static_cast<double>(m.nnz) / static_cast<double>(m.nrows) : 0.0;
        const bool use_warp = avg_nnz >= kWarpRowThreshold;
        if (ok) {
            SpmvArgs args{m.nrows, m.row_ptr, m.col_idx, m.values, d_x, d_y};
            // One pointer per kernel parameter, in declaration order.
            void* params[] = {&args.nrows, &args.rp, &args.ci, &args.val,
                              &args.x, &args.y};
            const unsigned grid = static_cast<unsigned>(
                use_warp ? (m.nrows + kWarpPerBlock - 1) / kWarpPerBlock
                         : (m.nrows + kSpmvBlock - 1) / kSpmvBlock);
            ok = DriverApi::launch(use_warp ? spmv_warp_ : spmv_scalar_, grid, kSpmvBlock,
                                   0, params);
            if (!ok) fail(use_warp ? "spmv_warp kernel launch" : "spmv_scalar kernel launch");
        }
        if (ok) {
            ok = DriverApi::synchronize_checked();
            if (!ok) fail("post-launch synchronization");
        }
        if (ok) {
            y.resize(m.nrows);
            ok = DriverApi::copy_d2h(y.data(), d_y, m.nrows * sizeof(double));
            if (!ok) fail("D2H copy of spmv result");
        }
        DriverApi::free(d_x);
        DriverApi::free(d_y);
        return ok;
    }

    bool launch_spmm(const DeviceMatrix& m, const std::vector<double>& B,
                     std::vector<double>& C, std::size_t ncb) {
        if (!ensure_module()) return false;
        const std::size_t b_bytes = B.size() * sizeof(double);
        const std::size_t c_bytes = m.nrows * ncb * sizeof(double);
        DevicePtr d_b = DriverApi::allocate(b_bytes);
        DevicePtr d_c = DriverApi::allocate(c_bytes);
        if (!d_b || !d_c) {
            fail("device allocation for spmm buffers");
            if (d_b) DriverApi::free(d_b);
            if (d_c) DriverApi::free(d_c);
            return false;
        }
        bool ok = DriverApi::copy_h2d(d_b, B.data(), b_bytes);
        if (!ok) fail("H2D copy of B");
        if (ok) {
            ok = DriverApi::memset_d8(d_c, 0, c_bytes);
            if (!ok) fail("memset of C");
        }
        if (ok) {
            SpmmArgs args{m.nrows, ncb, m.row_ptr, m.col_idx, m.values, d_b, d_c};
            void* params[] = {&args.nrows, &args.ncb, &args.rp, &args.ci,
                              &args.val, &args.b, &args.c};
            const unsigned grid = static_cast<unsigned>((m.nrows + kSpmvBlock - 1) / kSpmvBlock);
            ok = DriverApi::launch(spmm_fn_, grid, kSpmvBlock, 0, params);
            if (!ok) fail("spmm kernel launch");
        }
        if (ok) {
            ok = DriverApi::synchronize_checked();
            if (!ok) fail("post-launch synchronization");
        }
        if (ok) {
            C.resize(m.nrows * ncb);
            ok = DriverApi::copy_d2h(C.data(), d_c, c_bytes);
            if (!ok) fail("D2H copy of spmm result");
        }
        DriverApi::free(d_b);
        DriverApi::free(d_c);
        return ok;
    }

    void calibrate() {
        // Small synthetic problem: 4096 rows x 32 nnz/row (131k nonzeros).
        const std::size_t nrows = 4096;
        const std::size_t nnz_per_row = 32;
        const std::size_t nnz = nrows * nnz_per_row;
        std::vector<std::size_t> rp(nrows + 1);
        std::vector<std::size_t> ci(nnz);
        std::vector<double> val(nnz);
        std::vector<double> x(nrows, 1.0);
        for (std::size_t i = 0; i < nrows; ++i) {
            rp[i] = i * nnz_per_row;
            for (std::size_t j = 0; j < nnz_per_row; ++j) {
                std::size_t k = i * nnz_per_row + j;
                ci[k] = (i + j) % nrows;
                val[k] = 1.0;
            }
        }
        rp[nrows] = nnz;

        DeviceMatrix m;
        if (!upload_matrix(val, ci, rp, m)) {
            available_ = false;
            return;
        }

        std::vector<double> y(nrows);
        if (!launch_spmv(m, x, y)) { // warm-up (JIT, first-touch)
            // A calibration launch failure is a real backend defect (the
            // problem here is synthetic and trivially valid): surface it.
            last_error_ = "calibration launch failed: " + last_error_;
            free_matrix(m);
            available_ = false;
            return;
        }
        {
            constexpr int kReps = 5;
            auto t0 = std::chrono::steady_clock::now();
            for (int rep = 0; rep < kReps; ++rep) {
                if (!launch_spmv(m, x, y)) break;
            }
            auto t1 = std::chrono::steady_clock::now();
            const double total_ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (total_ms > 0.0) {
                const double bytes_per_spmv =
                    static_cast<double>(nnz) * (sizeof(double) + sizeof(std::size_t));
                measured_spmv_gbps_ =
                    (bytes_per_spmv * kReps) / (total_ms / 1e3) / 1e9;
            }
        }

        // H2D/D2H calibration on the same buffers.
        auto h2d_start = std::chrono::steady_clock::now();
        bool ok = DriverApi::copy_h2d(m.values, val.data(), nnz * sizeof(double));
        auto h2d_end = std::chrono::steady_clock::now();
        if (ok) {
            double ms =
                std::chrono::duration<double, std::milli>(h2d_end - h2d_start).count();
            if (ms > 0.0) {
                measured_h2d_gbps_ =
                    (static_cast<double>(nnz) * sizeof(double)) / (ms / 1e3) / 1e9;
            }
        }
        std::vector<double> val_out(nnz);
        auto d2h_start = std::chrono::steady_clock::now();
        ok = DriverApi::copy_d2h(val_out.data(), m.values, nnz * sizeof(double));
        auto d2h_end = std::chrono::steady_clock::now();
        if (ok) {
            double ms =
                std::chrono::duration<double, std::milli>(d2h_end - d2h_start).count();
            if (ms > 0.0) {
                measured_d2h_gbps_ =
                    (static_cast<double>(nnz) * sizeof(double)) / (ms / 1e3) / 1e9;
            }
        }
        free_matrix(m);

        // Spec-based bounds: pageable H2D copies rarely exceed ~half of the
        // theoretical link bandwidth, and SpMV rarely exceeds ~80% of peak.
        const double spec = cuda::effective_bandwidth_gbs(info_);
        calibrated_h2d_gbps_ = measured_h2d_gbps_;
        if (spec > 0.0) {
            calibrated_h2d_gbps_ = std::min(calibrated_h2d_gbps_, spec * 0.5);
            measured_d2h_gbps_ = std::min(measured_d2h_gbps_, spec * 0.5);
            if (measured_spmv_gbps_ <= 0.0) measured_spmv_gbps_ = spec * 0.7;
        }
        if (calibrated_h2d_gbps_ <= 0.0) calibrated_h2d_gbps_ = 6.0;
        if (measured_d2h_gbps_ <= 0.0) measured_d2h_gbps_ = 6.0;
        if (measured_spmv_gbps_ <= 0.0) measured_spmv_gbps_ = 50.0;
    }

    void release_device_data() {
        for (auto& [key, m] : resident_) {
            (void)key;
            free_matrix(m);
        }
        resident_.clear();
        for (auto& [key, m] : resident_spmm_) {
            (void)key;
            free_matrix(m);
        }
        resident_spmm_.clear();
    }

    bool initialized_ = false;
    bool available_ = false;
    int device_ = -1;
    cuda::Module module_scalar_ = nullptr;
    cuda::Module module_warp_ = nullptr;
    cuda::Module module_spmm_ = nullptr;
    cuda::Function spmv_scalar_ = nullptr;
    cuda::Function spmv_warp_ = nullptr;
    cuda::Function spmm_fn_ = nullptr;
    cuda::DeviceInfo info_;

    // One device-resident copy per distinct matrix (keyed by host values ptr).
    std::unordered_map<const void*, DeviceMatrix> resident_;
    std::unordered_map<const void*, DeviceMatrix> resident_spmm_;

    // Calibration / measurements.
    double measured_spmv_gbps_ = 0.0; // achieved device-only SpMV bandwidth
    double measured_h2d_gbps_ = 0.0;  // raw pageable H2D measurement
    double measured_d2h_gbps_ = 0.0;  // raw pageable D2H measurement
    double calibrated_h2d_gbps_ = 0.0; // bounded H2D estimate for the model

    std::string last_error_;
    mutable std::mutex mutex_;
};

// ---------------------------------------------------------------------------
// Auto backend: per-call cost-model dispatch between CPU and CUDA.
// ---------------------------------------------------------------------------

class AutoBackend : public IComputeBackend {
public:
    ComputeBackendType type() const override { return ComputeBackendType::AUTO; }

    std::string name() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return name_;
    }

    bool is_available() const override { return true; }

    void initialize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) return;
        initialized_ = true;

        cpu_ = std::make_unique<CPUBackend>();
        cpu_->initialize();

        if (ComputeBackendFactory::best_available_backend() == ComputeBackendType::CUDA) {
            auto backend = std::make_unique<CudaDriverBackend>();
            backend->initialize();
            if (backend->is_available()) {
                // Feed measured device parameters into the cost model.
                GPUCostModel::set_device_params(backend->measured_spmv_gbps(),
                                                backend->measured_h2d_gbps(),
                                                backend->measured_d2h_gbps());
                cuda_ = std::move(backend);
            }
        }
        name_ = cuda_ ? ("Auto(" + cuda_->name() + " | CPU fallback)")
                      : "Auto(CPU)";
    }

    void finalize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cuda_) cuda_->finalize();
        if (cpu_) cpu_->finalize();
    }

    void spmv(const std::vector<double>& values,
              const std::vector<std::size_t>& col_indices,
              const std::vector<std::size_t>& row_ptr,
              const std::vector<double>& x,
              std::vector<double>& y) override {
        const std::size_t nrows = row_ptr.size() >= 1 ? row_ptr.size() - 1 : 0;
        const auto decision = GPUCostModel::decide(values, col_indices, row_ptr, nrows,
                                                   /*ncols=*/0, /*batch_size=*/1);
        if (decision.backend == ComputeBackendType::CUDA && cuda_ &&
            cuda_->is_available()) {
            try {
                cuda_->spmv(values, col_indices, row_ptr, x, y);
                return;
            } catch (const std::exception&) {
                // Runtime GPU failure: fall back to CPU for this call.
            }
        }
        cpu_->spmv(values, col_indices, row_ptr, x, y);
    }

    void spmm(const std::vector<double>& values,
              const std::vector<std::size_t>& col_indices,
              const std::vector<std::size_t>& row_ptr,
              const std::vector<double>& B,
              std::vector<double>& C,
              std::size_t ncols_B) override {
        const std::size_t nrows = row_ptr.size() >= 1 ? row_ptr.size() - 1 : 0;
        const auto decision = GPUCostModel::decide(values, col_indices, row_ptr, nrows,
                                                   /*ncols=*/ncols_B, /*batch_size=*/ncols_B);
        if (decision.backend == ComputeBackendType::CUDA && cuda_ &&
            cuda_->is_available()) {
            try {
                cuda_->spmm(values, col_indices, row_ptr, B, C, ncols_B);
                return;
            } catch (const std::exception&) {
                // Runtime GPU failure: fall back to CPU for this call.
            }
        }
        cpu_->spmm(values, col_indices, row_ptr, B, C, ncols_B);
    }

    std::size_t device_memory() const override {
        return cuda_ ? cuda_->device_memory() : 0;
    }

    std::size_t free_memory() const override {
        return cuda_ ? cuda_->free_memory() : 0;
    }

private:
    std::unique_ptr<CPUBackend> cpu_;
    std::unique_ptr<CudaDriverBackend> cuda_;
    std::string name_ = "Auto(CPU)";
    bool initialized_ = false;
    mutable std::mutex mutex_;
};

} // namespace

// ---------------------------------------------------------------------------
// GPUCostModel
// ---------------------------------------------------------------------------

namespace {

struct ModelParams {
    double h2d_gbps = 6.0;         // PCIe 3.0 x16 effective pageable share
    double d2h_gbps = 6.0;
    double device_spmv_gbps = 0.0; // measured; injected by a calibrated backend
    double cpu_gbps = 8.0;         // sustained single-thread SpMV stream rate
    double launch_overhead_ms = 0.01;
    double per_call_overhead_ms = 0.02; // sync + alloc + result staging
};

ModelParams& model_params() {
    static ModelParams p;
    return p;
}

std::mutex& model_mutex() {
    static std::mutex m;
    return m;
}

} // namespace

void GPUCostModel::set_device_params(double device_spmv_gbps, double h2d_gbps,
                                     double d2h_gbps) {
    std::lock_guard<std::mutex> lock(model_mutex());
    auto& p = model_params();
    if (device_spmv_gbps > 0.0) p.device_spmv_gbps = device_spmv_gbps;
    if (h2d_gbps > 0.0) p.h2d_gbps = h2d_gbps;
    if (d2h_gbps > 0.0) p.d2h_gbps = d2h_gbps;
}

double GPUCostModel::estimate_cpu_spmv(const std::vector<double>& values,
                                       const std::vector<std::size_t>& col_indices,
                                       const std::vector<std::size_t>& row_ptr,
                                       std::size_t nrows) {
    (void)col_indices;
    (void)row_ptr;
    const double nnz = static_cast<double>(values.size());
    // Bytes streamed: values + indices + gathered x + stored y + row_ptr.
    const double bytes = nnz * (sizeof(double) + sizeof(std::size_t) + sizeof(double)) +
                         nrows * (sizeof(double) + sizeof(std::size_t));
    std::lock_guard<std::mutex> lock(model_mutex());
    return bytes / (model_params().cpu_gbps * 1e9) * 1e3; // ms
}

double GPUCostModel::estimate_gpu_spmv(const std::vector<double>& values,
                                       const std::vector<std::size_t>& col_indices,
                                       const std::vector<std::size_t>& row_ptr,
                                       std::size_t nrows,
                                       int batch_size) {
    (void)col_indices;
    const double nnz = static_cast<double>(values.size());
    std::lock_guard<std::mutex> lock(model_mutex());
    const auto& p = model_params();
    const double matrix_bytes = nnz * (sizeof(double) + sizeof(std::size_t)) +
                                row_ptr.size() * sizeof(std::size_t);
    const double vector_bytes = nrows * (sizeof(double) + sizeof(std::size_t));
    // Matrix upload paid once per batch; x/y stream on every call.
    const double upload_ms =
        (matrix_bytes / (p.h2d_gbps * 1e9)) * 1e3 +
        (vector_bytes / (p.h2d_gbps * 1e9) + vector_bytes / (p.d2h_gbps * 1e9)) * 1e3 *
            static_cast<double>(batch_size);
    const double kernel_ms =
        (nnz * (2.0 * sizeof(double) + sizeof(std::size_t)) / (p.device_spmv_gbps * 1e9)) *
        1e3 * static_cast<double>(batch_size);
    return upload_ms + kernel_ms + p.launch_overhead_ms + p.per_call_overhead_ms;
}

GPUCostModel::Decision GPUCostModel::decide(const std::vector<double>& values,
                                            const std::vector<std::size_t>& col_indices,
                                            const std::vector<std::size_t>& row_ptr,
                                            std::size_t nrows,
                                            std::size_t ncols,
                                            int batch_size) {
    (void)ncols;
    Decision decision;
    const double nnz = static_cast<double>(values.size());

    // Without a measured device rate the GPU estimate is meaningless;
    // decide() then always reports CPU (device_spmv_gbps == 0 makes the
    // kernel term infinite, which the speedup check rejects). The rate is
    // sampled under lock, released before calling the estimators (they lock
    // model_mutex themselves; std::mutex is not recursive).
    const double dev_rate = [&] {
        std::lock_guard<std::mutex> lock(model_mutex());
        return model_params().device_spmv_gbps;
    }();
    if (dev_rate <= 0.0) {
        decision.reason = "no calibrated GPU device; CPU dispatch";
        decision.estimated_cpu_time_ms =
            estimate_cpu_spmv(values, col_indices, row_ptr, nrows) * batch_size;
        decision.estimated_gpu_time_ms = 0.0;
        return decision;
    }

    decision.estimated_cpu_time_ms =
        estimate_cpu_spmv(values, col_indices, row_ptr, nrows) *
        static_cast<double>(batch_size);
    decision.estimated_gpu_time_ms =
        estimate_gpu_spmv(values, col_indices, row_ptr, nrows, batch_size);

    // GPU only wins when the workload is large enough to amortize
    // launch + transfer latency AND the estimate is meaningfully better
    // (>= 1.5x) than CPU. This is a pure performance model: whether a GPU
    // backend is actually attached is the caller's concern (AutoBackend
    // falls back to CPU when none is).
    const bool big_enough = nnz >= 50000.0 && nrows >= 2000;
    const double speedup = decision.estimated_gpu_time_ms > 0.0
                               ? decision.estimated_cpu_time_ms /
                                     decision.estimated_gpu_time_ms
                               : 0.0;

    if (big_enough && speedup >= 1.5) {
        decision.backend = ComputeBackendType::CUDA;
        decision.reason = "GPU estimated ~" + std::to_string(speedup).substr(0, 4) +
                          "x faster (nnz=" +
                          std::to_string(static_cast<long long>(nnz)) +
                          ", matrix resident, batch=" +
                          std::to_string(batch_size) + ")";
    } else if (!big_enough) {
        decision.reason = "problem too small to amortize GPU launch/transfer overhead";
    } else {
        decision.reason = "GPU estimate not faster enough than CPU (~" +
                          std::to_string(speedup).substr(0, 4) + "x)";
    }
    return decision;
}

// ---------------------------------------------------------------------------
// ComputeBackendFactory
// ---------------------------------------------------------------------------

namespace {

std::mutex& factory_mutex() {
    static std::mutex m;
    return m;
}

bool cuda_backend_usable() {
    static bool cached = false;
    static bool usable = false;
    std::lock_guard<std::mutex> lock(factory_mutex());
    if (!cached) {
        CudaDriverBackend probe;
        probe.initialize();
        usable = probe.is_available();
        cached = true;
    }
    return usable;
}

} // namespace

std::unique_ptr<IComputeBackend> ComputeBackendFactory::create_backend(ComputeBackendType type) {
    switch (type) {
        case ComputeBackendType::AUTO:
            return std::make_unique<AutoBackend>();
        case ComputeBackendType::CUDA:
            if (cuda_backend_usable()) {
                auto backend = std::make_unique<CudaDriverBackend>();
                backend->initialize();
                if (backend->is_available()) return backend;
            }
            return std::make_unique<CPUBackend>();
        case ComputeBackendType::CPU:
        case ComputeBackendType::HIP:
        case ComputeBackendType::SYCL:
        default:
            return std::make_unique<CPUBackend>();
    }
}

std::vector<ComputeBackendType> ComputeBackendFactory::available_backends() {
    std::vector<ComputeBackendType> out{ComputeBackendType::CPU};
    if (cuda_backend_usable()) out.push_back(ComputeBackendType::CUDA);
    return out;
}

ComputeBackendType ComputeBackendFactory::best_available_backend() {
    if (cuda_backend_usable()) return ComputeBackendType::CUDA;
    return ComputeBackendType::CPU;
}

std::string cuda_backend_probe_error() {
    CudaDriverBackend probe;
    probe.initialize();
    return probe.is_available() ? std::string() : probe.last_error();
}

} // namespace hypernova::execution
