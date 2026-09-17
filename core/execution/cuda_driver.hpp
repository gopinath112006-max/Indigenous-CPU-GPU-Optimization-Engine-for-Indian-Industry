#pragma once

// Dynamic loader for the CUDA driver API (nvcuda.dll on Windows, libcuda.so.1
// on Linux). The driver ships with the NVIDIA display driver, so this gives a
// GPU backend with zero build-time CUDA toolkit dependency: PTX kernels are
// JIT-compiled at runtime by the driver itself.
//
// Only the small API surface HyperNova needs is bound; unknown failures are
// reported as "unavailable" rather than throwing, so callers can fall back to
// CPU transparently.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hypernova::execution::cuda {

// Raw driver-API handle types (opaque).
using CudaResult = int; // CUresult
using Device = int;     // CUdevice (int on both platforms)
using Context = void*;  // CUcontext
using Module = void*;            // CUmodule
using Function = void*;          // CUfunction
using DevicePtr = std::uint64_t; // CUdeviceptr

struct DeviceInfo {
    std::string name;
    int compute_major = 0;
    int compute_minor = 0;
    std::size_t total_memory = 0;
    std::size_t multiprocessor_count = 0;
    std::size_t clock_khz = 0;
    std::size_t memory_clock_khz = 0;
    std::size_t memory_bus_width = 0;
};

// Effective sustainable device bandwidth in GB/s, from spec attributes.
double effective_bandwidth_gbs(const DeviceInfo& info);

class DriverApi {
public:
    // Loads the driver library and binds symbols. Idempotent.
    // Returns false when no CUDA driver is present or binding failed.
    static bool load();

    // True once load() has succeeded.
    static bool loaded();

    static int device_count();
    static DeviceInfo device_info(int device);

    // Creates (once) and binds the primary context for `device`.
    static bool init_context(int device, std::string& error);

    // Loads a PTX image and JIT-compiles it. `error` receives the driver's
    // JIT error/info log on failure (parse errors, unsupported arch, ...).
    static bool load_module_ptx(const char* ptx_source, Module& module,
                                std::string& error);

    static bool get_function(Module module, const char* name, Function& fn);

    // Unloads a JIT-compiled module (invalidates its function handles).
    static void unload_module(Module module);

    // Allocates device memory (bytes). Returns 0 on failure.
    static DevicePtr allocate(std::size_t bytes);
    static void free(DevicePtr ptr);

    // Free / total device memory in bytes for the current context.
    static bool memory_info(std::size_t& free_bytes, std::size_t& total_bytes);

    static bool copy_h2d(DevicePtr dst, const void* src, std::size_t bytes);
    static bool copy_d2h(void* dst, DevicePtr src, std::size_t bytes);
    static bool memset_d8(DevicePtr dst, int value, std::size_t bytes);

    // Launches a 1-D grid of 1-D blocks. `params` is the canonical driver-API
    // kernelParams array: one pointer per kernel parameter, in declaration
    // order, each pointing at the argument value (must stay valid for the
    // duration of the call).
    static bool launch(Function fn, unsigned grid_x, unsigned block_x,
                       unsigned shared_bytes, void* const* params);

    static void synchronize();
    static bool synchronize_checked();

    // CUresult of the most recent failed driver call (0 if none). Enables
    // callers to attach a precise error string to their own failure paths.
    static CudaResult last_result();

    // Last error as human-readable string (cuGetErrorString).
    static std::string result_string(CudaResult result);
};

} // namespace hypernova::execution::cuda
