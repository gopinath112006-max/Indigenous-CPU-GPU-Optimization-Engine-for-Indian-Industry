#include "cuda_driver.hpp"

#include <atomic>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hypernova::execution::cuda {

// ---------------------------------------------------------------------------
// Driver API surface used by HyperNova (subset).
// Format: X(return_type, function_name, (parameter_types)).
// ---------------------------------------------------------------------------

#define HYPERNOVA_CUDA_FUNCS(X)                                                                 \
    X(int, cuInit, (unsigned))                                                                  \
    X(int, cuDeviceGetCount, (int*))                                                            \
    X(int, cuDeviceGet, (int*, int))                                                            \
    X(int, cuDeviceGetName, (char*, int, int))                                                  \
    X(int, cuDeviceGetAttribute, (int*, int, int))                                              \
    X(int, cuDeviceTotalMem_v2, (std::size_t*, int))                                            \
    X(int, cuDevicePrimaryCtxRetain, (void**, int))                                             \
    X(int, cuDevicePrimaryCtxRelease, (int))                                                    \
    X(int, cuCtxSetCurrent, (void*))                                                            \
    X(int, cuCtxSynchronize, (void))                                                            \
    X(int, cuCtxGetDevice, (int*))                                                              \
    X(int, cuMemAlloc_v2, (std::uint64_t*, std::size_t))                                        \
    X(int, cuMemFree_v2, (std::uint64_t))                                                       \
    X(int, cuMemGetInfo_v2, (std::size_t*, std::size_t*))                                       \
    X(int, cuMemcpyHtoD_v2, (std::uint64_t, const void*, std::size_t))                          \
    X(int, cuMemcpyDtoH_v2, (void*, std::uint64_t, std::size_t))                                \
    X(int, cuMemsetD8_v2, (std::uint64_t, unsigned char, std::size_t))                          \
    X(int, cuModuleLoadDataEx, (void**, const void*, unsigned, unsigned*, void**))              \
    X(int, cuModuleUnload, (void*))                                                             \
    X(int, cuModuleGetFunction, (void**, void*, const char*))                                   \
    X(int, cuLaunchKernel, (void*, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,  \
                            unsigned, void*, void**, void**))                                   \
    X(int, cuGetErrorString, (int, const char**))                                               \
    X(int, cuGetErrorName, (int, const char**))

struct Api {
    void* handle = nullptr;
// Members are function pointers: ret (*name) params.
#define X(ret, sym, params) ret(*sym) params;
    HYPERNOVA_CUDA_FUNCS(X)
#undef X
};

namespace {

Api& api() {
    static Api instance;
    return instance;
}

// CUresult of the most recent failed driver call (0 = no error seen).
std::atomic<int>& last_result_slot() {
    static std::atomic<int> slot{0};
    return slot;
}

struct RecordResult {
    explicit RecordResult(int rc) { last_result_slot().store(rc, std::memory_order_relaxed); }
};

std::mutex& load_mutex() {
    static std::mutex m;
    return m;
}

void* open_driver_library() {
#if defined(_WIN32)
    // nvcuda.dll lives in the system directories; LoadLibrary resolves it via
    // the standard search order without needing any PATH changes.
    HMODULE h = LoadLibraryW(L"nvcuda.dll");
    return h ? reinterpret_cast<void*>(h) : nullptr;
#else
    // CUDA driver from NVIDIA packages: /usr/lib/x86_64-linux-gnu, /usr/lib64...
    // ldconfig normally resolves the soname without manual configuration.
    void* h = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libcuda.so", RTLD_NOW | RTLD_LOCAL);
    return h;
#endif
}

template <typename T>
bool bind_symbol(void* handle, const char* name, T& fn) {
#if defined(_WIN32)
    fn = reinterpret_cast<T>(reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(handle), name)));
#else
    fn = reinterpret_cast<T>(dlsym(handle, name));
#endif
    return fn != nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// DriverApi
// ---------------------------------------------------------------------------

bool DriverApi::load() {
    std::lock_guard<std::mutex> lock(load_mutex());
    Api& a = api();
    if (a.handle) return true;

    a.handle = open_driver_library();
    if (!a.handle) return false;

    bool all_bound = true;
#define X(ret, sym, params) all_bound = bind_symbol(a.handle, #sym, a.sym) && all_bound;
    HYPERNOVA_CUDA_FUNCS(X)
#undef X
    if (!all_bound) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(a.handle));
#else
        dlclose(a.handle);
#endif
        a.handle = nullptr;
        return false;
    }

    // cuInit is required once per process; treat failure as "no usable GPU".
    if (a.cuInit(0) != 0) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(a.handle));
#else
        dlclose(a.handle);
#endif
        a.handle = nullptr;
        return false;
    }
    return true;
}

bool DriverApi::loaded() { return api().handle != nullptr; }

int DriverApi::device_count() {
    Api& a = api();
    if (!a.handle) return 0;
    int n = 0;
    if (a.cuDeviceGetCount(&n) != 0) return 0;
    return n;
}

namespace {
// cuDeviceGetAttribute ids (stable ABI since CUDA 2.x).
constexpr int kAttrClockRate = 13;            // kHz
constexpr int kAttrMultiprocessorCount = 16;
constexpr int kAttrMemoryClockRate = 36;      // kHz
constexpr int kAttrGlobalMemoryBusWidth = 37; // bits
constexpr int kAttrComputeMajor = 75;
constexpr int kAttrComputeMinor = 76;
} // namespace

DeviceInfo DriverApi::device_info(int device) {
    DeviceInfo info;
    Api& a = api();
    if (!a.handle) return info;

    char name[256] = {0};
    if (a.cuDeviceGetName(name, sizeof(name), device) == 0) {
        info.name = name;
    }
    int major = 0, minor = 0, sms = 0, clock = 0, mem_clock = 0, bus_w = 0;
    std::size_t total = 0;
    a.cuDeviceGetAttribute(&major, kAttrComputeMajor, device);
    a.cuDeviceGetAttribute(&minor, kAttrComputeMinor, device);
    a.cuDeviceGetAttribute(&sms, kAttrMultiprocessorCount, device);
    a.cuDeviceGetAttribute(&clock, kAttrClockRate, device);
    a.cuDeviceGetAttribute(&mem_clock, kAttrMemoryClockRate, device);
    a.cuDeviceGetAttribute(&bus_w, kAttrGlobalMemoryBusWidth, device);
    a.cuDeviceTotalMem_v2(&total, device);

    info.compute_major = major;
    info.compute_minor = minor;
    info.multiprocessor_count = static_cast<std::size_t>(sms);
    info.clock_khz = static_cast<std::size_t>(clock);
    info.memory_clock_khz = static_cast<std::size_t>(mem_clock);
    info.memory_bus_width = static_cast<std::size_t>(bus_w);
    info.total_memory = total;
    return info;
}

double effective_bandwidth_gbs(const DeviceInfo& info) {
    if (info.memory_clock_khz == 0 || info.memory_bus_width == 0) return 0.0;
    // DDR transfers twice per clock; bus width in bits -> bytes.
    const double bytes_per_second = static_cast<double>(info.memory_clock_khz) * 1e3 * 2.0 *
                                    (static_cast<double>(info.memory_bus_width) / 8.0);
    return bytes_per_second / 1e9;
}

bool DriverApi::init_context(int device, std::string& error) {
    Api& a = api();
    if (!a.handle) {
        error = "CUDA driver library not loaded";
        return false;
    }
    static std::mutex ctx_mutex;
    static Context retained = nullptr;
    static int retained_device = -1;

    std::lock_guard<std::mutex> lock(ctx_mutex);
    if (retained && retained_device == device) {
        if (a.cuCtxSetCurrent(retained) == 0) return true;
        error = "cuCtxSetCurrent failed for retained primary context";
        return false;
    }
    void* ctx = nullptr;
    // Flags 0 = CU_CTX_SCHED_AUTO: driver picks spin/yield based on the
    // number of active contexts; safest default for a solver that also uses
    // CPU threads.
    if (a.cuDevicePrimaryCtxRetain(&ctx, device) != 0) {
        error = "cuDevicePrimaryCtxRetain failed (driver may be busy or GPU unusable)";
        return false;
    }
    if (a.cuCtxSetCurrent(ctx) != 0) {
        a.cuDevicePrimaryCtxRelease(device);
        error = "cuCtxSetCurrent failed after primary context retain";
        return false;
    }
    retained = ctx;
    retained_device = device;
    return true;
}

bool DriverApi::load_module_ptx(const char* ptx_source, Module& module,
                                std::string& error) {
    Api& a = api();
    module = nullptr;
    if (!a.handle) {
        error = "CUDA driver library not loaded";
        return false;
    }
    // Capture the JIT logs so failures report the driver's actual message
    // (parse errors, unsupported targets, ...) instead of a bare CUresult.
    // CU_JIT_INFO_LOG_BUFFER=3, _SIZE_BYTES=4, CU_JIT_ERROR_LOG_BUFFER=5,
    // _SIZE_BYTES=6 (stable option ids).
    char info_log[2048] = {0};
    char error_log[2048] = {0};
    unsigned int options[4] = {3, 4, 5, 6};
    void* values[4] = {
        info_log,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(sizeof(info_log))),
        error_log,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(sizeof(error_log))),
    };
    const CudaResult rc = a.cuModuleLoadDataEx(&module, ptx_source, 4, options, values);
    if (rc != 0) {
        std::string detail = error_log;
        if (detail.empty()) detail = info_log;
        error = "cuModuleLoadDataEx failed (PTX JIT error) [" + result_string(rc) + "]";
        if (!detail.empty()) error += ": " + detail;
        return false;
    }
    return true;
}

bool DriverApi::get_function(Module module, const char* name, Function& fn) {
    Api& a = api();
    if (!a.handle || !module) return false;
    const int rc = a.cuModuleGetFunction(&fn, module, name);
    if (rc != 0) last_result_slot().store(rc, std::memory_order_relaxed);
    return rc == 0;
}

void DriverApi::unload_module(Module module) {
    Api& a = api();
    if (a.handle && module) a.cuModuleUnload(module);
}

DevicePtr DriverApi::allocate(std::size_t bytes) {
    Api& a = api();
    if (!a.handle || bytes == 0) return 0;
    DevicePtr ptr = 0;
    const int rc = a.cuMemAlloc_v2(&ptr, bytes);
    if (rc != 0) {
        last_result_slot().store(rc, std::memory_order_relaxed);
        return 0;
    }
    return ptr;
}

void DriverApi::free(DevicePtr ptr) {
    Api& a = api();
    if (a.handle && ptr) a.cuMemFree_v2(ptr);
}

bool DriverApi::memory_info(std::size_t& free_bytes, std::size_t& total_bytes) {
    Api& a = api();
    free_bytes = 0;
    total_bytes = 0;
    return a.handle && a.cuMemGetInfo_v2(&free_bytes, &total_bytes) == 0;
}

bool DriverApi::copy_h2d(DevicePtr dst, const void* src, std::size_t bytes) {
    Api& a = api();
    if (!a.handle) return false;
    const int rc = a.cuMemcpyHtoD_v2(dst, src, bytes);
    if (rc != 0) last_result_slot().store(rc, std::memory_order_relaxed);
    return rc == 0;
}

bool DriverApi::copy_d2h(void* dst, DevicePtr src, std::size_t bytes) {
    Api& a = api();
    if (!a.handle) return false;
    const int rc = a.cuMemcpyDtoH_v2(dst, src, bytes);
    if (rc != 0) last_result_slot().store(rc, std::memory_order_relaxed);
    return rc == 0;
}

bool DriverApi::memset_d8(DevicePtr dst, int value, std::size_t bytes) {
    Api& a = api();
    if (!a.handle) return false;
    const int rc =
        a.cuMemsetD8_v2(dst, static_cast<unsigned char>(value), bytes);
    if (rc != 0) last_result_slot().store(rc, std::memory_order_relaxed);
    return rc == 0;
}

bool DriverApi::launch(Function fn, unsigned grid_x, unsigned block_x,
                       unsigned shared_bytes, void* const* params) {
    Api& a = api();
    if (!a.handle || !fn) return false;
    const int rc = a.cuLaunchKernel(fn, grid_x, 1, 1, block_x, 1, 1,
                                    shared_bytes, nullptr,
                                    const_cast<void**>(params), nullptr);
    if (rc != 0) last_result_slot().store(rc, std::memory_order_relaxed);
    return rc == 0;
}

void DriverApi::synchronize() {
    Api& a = api();
    if (a.handle) a.cuCtxSynchronize();
}

bool DriverApi::synchronize_checked() {
    Api& a = api();
    if (!a.handle) return false;
    const int rc = a.cuCtxSynchronize();
    if (rc != 0) last_result_slot().store(rc, std::memory_order_relaxed);
    return rc == 0;
}

CudaResult DriverApi::last_result() {
    return last_result_slot().load(std::memory_order_relaxed);
}

std::string DriverApi::result_string(CudaResult result) {
    Api& a = api();
    const char* str = nullptr;
    if (a.handle && a.cuGetErrorString(result, &str) == 0 && str) {
        return str;
    }
    std::ostringstream oss;
    oss << "CUDA error " << result;
    return oss.str();
}

} // namespace hypernova::execution::cuda
