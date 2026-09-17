// Temporary diagnostic probe for the CUDA driver backend (not part of the
// test suite). Prints the result of each driver-API step to isolate where
// backend initialization fails on this machine.
#include "cuda_driver.hpp"
#include "cuda_kernels_ptx.hpp"
#include "gpu_backend.hpp"

#include <iostream>

using namespace hypernova::execution::cuda;

int main() {
    std::cout << "1. driver load: " << DriverApi::load() << "\n";
    if (!DriverApi::load()) return 1;
    std::cout << "2. device count: " << DriverApi::device_count() << "\n";
    if (DriverApi::device_count() <= 0) return 2;
    DeviceInfo info = DriverApi::device_info(0);
    std::cout << "3. device 0: " << info.name << " sm_" << info.compute_major
              << info.compute_minor << " mem=" << (info.total_memory >> 20) << " MiB"
              << " sm_count=" << info.multiprocessor_count
              << " mem_clk=" << info.memory_clock_khz << "kHz"
              << " bus=" << info.memory_bus_width << "-bit"
              << " spec_bw=" << effective_bandwidth_gbs(info) << " GB/s\n";
    std::string error;
    bool ctx_ok = DriverApi::init_context(0, error);
    std::cout << "4. init_context: " << ctx_ok << (ctx_ok ? "" : (" -> " + error)) << "\n";
    if (!ctx_ok) return 4;
    Module module = nullptr;
    bool mod_ok = DriverApi::load_module_ptx(spmv_scalar_ptx().c_str(), module, error);
    std::cout << "5. load_module_ptx(spmv_scalar, " << spmv_scalar_ptx().size()
              << " bytes): " << mod_ok << (mod_ok ? "" : (" -> " + error)) << "\n";
    if (!mod_ok) return 5;
    Function fn = nullptr;
    std::cout << "6. get_function(spmv_scalar): "
              << DriverApi::get_function(module, kSpmvScalarKernel, fn) << "\n";
    std::size_t free_b = 0, total_b = 0;
    std::cout << "7. memory_info: " << DriverApi::memory_info(free_b, total_b)
              << " free=" << (free_b >> 20) << " MiB\n";
    std::cout << "ALL STEPS OK\n";

    // Ground-truth the WARP kernel too (the backend picks it automatically
    // for dense-ish rows; it has never been validated directly).
    {
        Module warp_module = nullptr;
        std::string jit_err;
        if (!DriverApi::load_module_ptx(spmv_warp_ptx().c_str(), warp_module, jit_err)) {
            std::cout << "W0. warp module: FAIL " << jit_err << "\n";
            return 30;
        }
        Function warp_fn = nullptr;
        if (!DriverApi::get_function(warp_module, kSpmvWarpKernel, warp_fn)) {
            std::cout << "W0. warp fn: FAIL\n";
            return 31;
        }
        const std::size_t n = 8;
        std::size_t rp2[n + 1] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        std::size_t ci2[n] = {0, 1, 2, 3, 4, 5, 6, 7};
        double val2[n] = {1, 2, 3, 4, 5, 6, 7, 8};
        double x2[n] = {1, 1, 1, 1, 1, 1, 1, 1};
        double y2[n] = {};
        DevicePtr d_rp = DriverApi::allocate(9 * sizeof(std::size_t));
        DevicePtr d_ci = DriverApi::allocate(n * sizeof(std::size_t));
        DevicePtr d_val = DriverApi::allocate(n * sizeof(double));
        DevicePtr d_x = DriverApi::allocate(n * sizeof(double));
        DevicePtr d_y = DriverApi::allocate(n * sizeof(double));
        bool c1 = DriverApi::copy_h2d(d_rp, rp2, 9 * sizeof(std::size_t));
        bool c2 = DriverApi::copy_h2d(d_ci, ci2, n * sizeof(std::size_t));
        bool c3 = DriverApi::copy_h2d(d_val, val2, n * sizeof(double));
        bool c4 = DriverApi::copy_h2d(d_x, x2, n * sizeof(double));
        bool c5 = DriverApi::memset_d8(d_y, 0, n * sizeof(double));
        std::cout << "W1. copies: " << c1 << c2 << c3 << c4 << c5 << "\n";
        std::uint64_t a_nrows = n, a_rp = d_rp, a_ci = d_ci, a_val = d_val, a_x = d_x,
                      a_y = d_y;
        void* params[] = {&a_nrows, &a_rp, &a_ci, &a_val, &a_x, &a_y};
        bool launched = DriverApi::launch(warp_fn, 1, 256, 0, params);
        std::cout << "W2. warp launch(grid=1): " << launched << " ["
                  << DriverApi::result_string(DriverApi::last_result()) << "]\n";
        if (launched) {
            bool synced = DriverApi::synchronize_checked();
            std::cout << "W3. sync: " << synced << " ["
                      << DriverApi::result_string(DriverApi::last_result()) << "]\n";
            bool got = DriverApi::copy_d2h(y2, d_y, n * sizeof(double));
            std::cout << "W4. d2h: " << got << " y = ";
            for (std::size_t i = 0; i < n; ++i) std::cout << y2[i] << " ";
            std::cout << "\n";
        }
        DriverApi::free(d_rp);
        DriverApi::free(d_ci);
        DriverApi::free(d_val);
        DriverApi::free(d_x);
        DriverApi::free(d_y);
        if (!launched) return 32;

        // Deterministic multi-lane test: row r = 64 nonzeros all equal
        // (r+1), x = 2 -> y[r] = 128*(r+1). Exercises full 32-lane
        // participation, the 2-iteration stride loop, and the butterfly.
        {
            const std::size_t rows = 2;
            const std::size_t per_row = 64;
            std::vector<std::size_t> rp3(rows + 1), ci3(rows * per_row);
            std::vector<double> val3(rows * per_row);
            for (std::size_t r = 0; r < rows; ++r) {
                rp3[r] = r * per_row;
                for (std::size_t j = 0; j < per_row; ++j) {
                    ci3[r * per_row + j] = j;
                    val3[r * per_row + j] = static_cast<double>(r + 1);
                }
            }
            rp3[rows] = rows * per_row;
            std::vector<double> x3(64, 2.0), y3(rows);
            DevicePtr d_rp = DriverApi::allocate(rp3.size() * sizeof(std::size_t));
            DevicePtr d_ci = DriverApi::allocate(ci3.size() * sizeof(std::size_t));
            DevicePtr d_val = DriverApi::allocate(val3.size() * sizeof(double));
            DevicePtr d_x = DriverApi::allocate(x3.size() * sizeof(double));
            DevicePtr d_y = DriverApi::allocate(rows * sizeof(double));
            DriverApi::copy_h2d(d_rp, rp3.data(), rp3.size() * sizeof(std::size_t));
            DriverApi::copy_h2d(d_ci, ci3.data(), ci3.size() * sizeof(std::size_t));
            DriverApi::copy_h2d(d_val, val3.data(), val3.size() * sizeof(double));
            DriverApi::copy_h2d(d_x, x3.data(), x3.size() * sizeof(double));
            DriverApi::memset_d8(d_y, 0, rows * sizeof(double));
            std::uint64_t n3 = rows, a_rp = d_rp, a_ci = d_ci, a_val = d_val, a_x = d_x,
                           a_y = d_y;
            void* params3[] = {&n3, &a_rp, &a_ci, &a_val, &a_x, &a_y};
            launched = DriverApi::launch(warp_fn, 1, 256, 0, params3);
            const bool synced3 = DriverApi::synchronize_checked();
            DriverApi::copy_d2h(y3.data(), d_y, rows * sizeof(double));
            std::cout << "D1. deterministic 2x64: launch=" << launched << " sync="
                      << synced3 << " ["
                      << DriverApi::result_string(DriverApi::last_result()) << "] y=";
            for (std::size_t i = 0; i < rows; ++i) std::cout << y3[i] << " ";
            std::cout << "(expect 128 256)\n";
            DriverApi::free(d_rp);
            DriverApi::free(d_ci);
            DriverApi::free(d_val);
            DriverApi::free(d_x);
            DriverApi::free(d_y);
            if (y3[0] != 128.0 || y3[1] != 256.0) return 40;
        }
        DriverApi::unload_module(warp_module);
    }

    // Ground-truth mini-launch through the exact driver-API path the backend
    // uses, printing every CUresult so a launch failure is attributable.
    {
        const std::size_t n = 8;
        const std::size_t nnz = 8;
        std::size_t rp[n + 1] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        std::size_t ci[nnz] = {0, 1, 2, 3, 4, 5, 6, 7};
        double val[nnz] = {1, 2, 3, 4, 5, 6, 7, 8};
        double x[n] = {1, 1, 1, 1, 1, 1, 1, 1};
        double y[n] = {};
        DevicePtr d_rp = DriverApi::allocate(9 * sizeof(std::size_t));
        DevicePtr d_ci = DriverApi::allocate(nnz * sizeof(std::size_t));
        DevicePtr d_val = DriverApi::allocate(nnz * sizeof(double));
        DevicePtr d_x = DriverApi::allocate(n * sizeof(double));
        DevicePtr d_y = DriverApi::allocate(n * sizeof(double));
        std::cout << "L1. allocs: " << (d_rp && d_ci && d_val && d_x && d_y ? "ok" : "FAIL")
                  << "\n";
        bool c1 = DriverApi::copy_h2d(d_rp, rp, 9 * sizeof(std::size_t));
        bool c2 = DriverApi::copy_h2d(d_ci, ci, nnz * sizeof(std::size_t));
        bool c3 = DriverApi::copy_h2d(d_val, val, nnz * sizeof(double));
        bool c4 = DriverApi::copy_h2d(d_x, x, n * sizeof(double));
        bool c5 = DriverApi::memset_d8(d_y, 0, n * sizeof(double));
        std::cout << "L2. copies: " << c1 << c2 << c3 << c4 << c5 << "\n";
        std::uint64_t a_nrows = n, a_rp = d_rp, a_ci = d_ci, a_val = d_val, a_x = d_x,
                      a_y = d_y;
        void* params[] = {&a_nrows, &a_rp, &a_ci, &a_val, &a_x, &a_y};
        bool launched = DriverApi::launch(fn, 1, 256, 0, params);
        std::cout << "L3. launch(grid=1): " << launched << " ["
                  << DriverApi::result_string(DriverApi::last_result()) << "]\n";
        if (launched) {
            bool synced = DriverApi::synchronize_checked();
            std::cout << "L4. sync: " << synced << " ["
                      << DriverApi::result_string(DriverApi::last_result()) << "]\n";
            bool got = DriverApi::copy_d2h(y, d_y, n * sizeof(double));
            std::cout << "L5. d2h: " << got << " y = ";
            for (std::size_t i = 0; i < n; ++i) std::cout << y[i] << " ";
            std::cout << "\n";
        }
        // Relaunch the same handle a second time (round-trip check).
        if (launched) {
            bool again = DriverApi::launch(fn, 1, 256, 0, params);
            DriverApi::synchronize_checked();
            std::cout << "L6. relaunch: " << again << "\n";
        }
        DriverApi::free(d_rp);
        DriverApi::free(d_ci);
        DriverApi::free(d_val);
        DriverApi::free(d_x);
        DriverApi::free(d_y);
        if (!launched) return 20;
    }

    // Repeated full-backend initializations, mirroring the factory's
    // probe-then-create chain.
    for (int round = 2; round <= 4; ++round) {
        const std::string err = hypernova::execution::cuda_backend_probe_error();
        std::cout << round << ". backend init: " << (err.empty() ? "OK" : ("FAIL: " + err))
                  << "\n";
        if (!err.empty()) return 10 + round;
    }
    return 0;
}
