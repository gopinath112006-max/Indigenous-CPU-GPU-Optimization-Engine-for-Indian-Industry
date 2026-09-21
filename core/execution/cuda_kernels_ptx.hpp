#pragma once

#include <string>

namespace hypernova::execution::cuda {

// Names of the __global__ kernels inside the embedded PTX module.
extern const char* const kSpmvScalarKernel;
extern const char* const kSpmvWarpKernel;
extern const char* const kSpmmKernel;

// Embedded PTX (compute_70, PTX 7.0) sources, JIT-compiled by the driver at
// load time. compute_70 targets Pascal+ (sm_70 and newer all JIT PTX 7.0
// fine); sm_50..sm_60 also accept PTX 6.x/7.0 via forward-compatible JIT.
extern const std::string& spmv_scalar_ptx();
extern const std::string& spmv_warp_ptx();
extern const std::string& spmm_ptx();

extern const char* const kDotKernel;
extern const char* const kAxpyKernel;
extern const char* const kNormInfKernel;

extern const std::string& dot_ptx();
extern const std::string& axpy_ptx();
extern const std::string& norm_inf_ptx();

} // namespace hypernova::execution::cuda
