#include "cuda_kernels_ptx.hpp"

namespace hypernova::execution::cuda {

// PTX 6.0 / sm_50 floor: every NVIDIA GPU from Maxwell (2014) onward can JIT
// these sources, and the shuffles use paired .b32 moves so no PTX >= 6.3
// 64-bit-shuffle support is required. The driver JIT compiles the PTX to the
// local device's SASS at module load.
//
// CSR index arrays are uploaded as 64-bit unsigned (std::size_t) to match the
// host containers byte-for-byte; all addressing uses 64-bit arithmetic so
// 2^31+ element matrices and >2 GiB offsets stay safe.

const char* const kSpmvScalarKernel = "hypernova_spmv_scalar";
const char* const kSpmvWarpKernel = "hypernova_spmv_warp";
const char* const kSpmmKernel = "hypernova_spmm";
const char* const kDotKernel = "hypernova_dot";
const char* const kAxpyKernel = "hypernova_axpy";
const char* const kNormInfKernel = "hypernova_norm_inf";

// y = A * x, one thread per row. Best for short rows (row length ~< 16).
const std::string& spmv_scalar_ptx() {
    static const std::string ptx = R"PTX(
.version 6.0
.target sm_50
.address_size 64

.visible .entry hypernova_spmv_scalar(
    .param .u64 nrows,
    .param .u64 rp,
    .param .u64 ci,
    .param .u64 val,
    .param .u64 x,
    .param .u64 y
)
{
    .reg .b32 %r<8>;
    .reg .b64 %rd<24>;
    .reg .f64 %fd<8>;
    .reg .pred %p<4>;

    ld.param.u64  %rd1, [nrows];
    ld.param.u64  %rd2, [rp];
    ld.param.u64  %rd3, [ci];
    ld.param.u64  %rd4, [val];
    ld.param.u64  %rd5, [x];
    ld.param.u64  %rd6, [y];

    // row = blockIdx.x * blockDim.x + threadIdx.x (64-bit safe)
    mov.b32       %r1, %ctaid.x;
    mov.b32       %r2, %ntid.x;
    mov.b32       %r3, %tid.x;
    cvt.s64.s32   %rd7, %r1;
    cvt.s64.s32   %rd8, %r2;
    cvt.s64.s32   %rd9, %r3;
    mul.lo.s64    %rd10, %rd7, %rd8;
    add.s64       %rd11, %rd10, %rd9;

    setp.ge.s64   %p1, %rd11, %rd1;
    @%p1 bra      L_DONE;

    // k_begin = rp[row]; k_end = rp[row+1]
    mul.lo.s64    %rd12, %rd11, 8;
    add.s64       %rd13, %rd2, %rd12;
    ld.global.u64 %rd14, [%rd13];
    add.s64       %rd15, %rd13, 8;
    ld.global.u64 %rd16, [%rd15];

    mov.f64       %fd1, 0d0000000000000000;
L_ROW_LOOP:
    setp.lt.u64   %p2, %rd14, %rd16;
    @!%p2 bra     L_STORE;

    mul.lo.s64    %rd17, %rd14, 8;
    add.s64       %rd18, %rd3, %rd17;
    ld.global.u64 %rd19, [%rd18];        // col = ci[k]
    add.s64       %rd20, %rd4, %rd17;
    ld.global.f64 %fd2, [%rd20];        // a = val[k]
    mul.lo.s64    %rd21, %rd19, 8;
    add.s64       %rd22, %rd5, %rd21;
    ld.global.f64 %fd3, [%rd22];        // xv = x[col]
    fma.rn.f64    %fd1, %fd2, %fd3, %fd1;

    add.s64       %rd14, %rd14, 1;
    bra           L_ROW_LOOP;

L_STORE:
    add.s64       %rd23, %rd6, %rd12;
    st.global.f64 [%rd23], %fd1;
L_DONE:
    ret;
}
)PTX";
    return ptx;
}

// y = A * x, one warp per row with 32-way butterfly reduction. Best for
// longer rows. All lanes stay converged through the reduction (shfl.sync);
// out-of-range warps carry a zero sum and skip only the predicated store.
const std::string& spmv_warp_ptx() {
    static const std::string ptx = R"PTX(
.version 6.0
.target sm_50
.address_size 64

.visible .entry hypernova_spmv_warp(
    .param .u64 nrows,
    .param .u64 rp,
    .param .u64 ci,
    .param .u64 val,
    .param .u64 x,
    .param .u64 y
)
{
    .reg .b32 %r<8>;
    .reg .b64 %rd<28>;
    .reg .f64 %fd<8>;
    .reg .pred %p<8>;

    ld.param.u64  %rd1, [nrows];
    ld.param.u64  %rd2, [rp];
    ld.param.u64  %rd3, [ci];
    ld.param.u64  %rd4, [val];
    ld.param.u64  %rd5, [x];
    ld.param.u64  %rd6, [y];

    mov.b32       %r1, %ctaid.x;
    mov.b32       %r2, %ntid.x;
    mov.b32       %r3, %tid.x;
    cvt.s64.s32   %rd7, %r1;             // blockIdx.x
    cvt.s64.s32   %rd8, %r2;             // blockDim.x
    cvt.s64.s32   %rd9, %r3;             // threadIdx.x

    shr.u64       %rd10, %rd9, 5;        // warp within block
    shr.u64       %rd11, %rd8, 5;        // warps per block
    mul.lo.s64    %rd12, %rd7, %rd11;
    add.s64       %rd12, %rd12, %rd10;   // global warp id = row

    setp.ge.s64   %p1, %rd12, %rd1;      // p1 = row out of range

    // k_begin = rp[row], k_end = rp[row+1]; predicated so OOB rows read
    // none. The two loads need separate registers for address and value:
    // clobbering the address register before the load would dereference 0.
    mul.lo.s64    %rd13, %rd12, 8;
    add.s64       %rd14, %rd2, %rd13;  // &rp[row]
    mov.u64       %rd15, 0;
    @!%p1 ld.global.u64 %rd15, [%rd14]; // k_begin
    add.s64       %rd17, %rd14, 8;     // &rp[row+1]
    mov.u64       %rd16, 0;
    @!%p1 ld.global.u64 %rd16, [%rd17]; // k_end

    mov.b32       %r3, %laneid;
    cvt.s64.s32   %rd18, %r3;            // lane
    add.s64       %rd22, %rd15, %rd18;   // k = k_begin + lane
    mul.lo.s64    %rd19, %rd22, 8;
    add.s64       %rd20, %rd3, %rd19;    // &ci[k]  (k includes k_begin!)
    add.s64       %rd21, %rd4, %rd19;    // &val[k]

    mov.f64       %fd1, 0d0000000000000000;
L_ROW_LOOP:
    setp.lt.u64   %p2, %rd22, %rd16;
    @!%p2 bra     L_REDUCE;

    ld.global.u64 %rd23, [%rd20];        // col
    ld.global.f64 %fd2, [%rd21];         // a
    mul.lo.s64    %rd24, %rd23, 8;
    add.s64       %rd25, %rd5, %rd24;
    ld.global.f64 %fd3, [%rd25];         // x[col]
    fma.rn.f64    %fd1, %fd2, %fd3, %fd1;

    add.s64       %rd20, %rd20, 256;     // stride: 32 lanes * 8 bytes
    add.s64       %rd21, %rd21, 256;
    add.s64       %rd22, %rd22, 32;
    bra           L_ROW_LOOP;

L_REDUCE:
    // Butterfly reduction across all 32 lanes (full member mask). With
    // shfl.sync.bfly every lane ends with the identical total, so the adds
    // are unconditional; no destination predicate is needed. The running
    // accumulator must be re-materialized into the b32 pair before every
    // round, otherwise each round shuffles the original partial sum and the
    // total is overcounted.
    mov.b64       {%r1, %r2}, %fd1;
    shfl.sync.bfly.b32 %r3, %r1, 16, 31, 0xffffffff;
    shfl.sync.bfly.b32 %r4, %r2, 16, 31, 0xffffffff;
    mov.b64       %fd2, {%r3, %r4};
    add.f64       %fd1, %fd1, %fd2;

    mov.b64       {%r1, %r2}, %fd1;
    shfl.sync.bfly.b32 %r3, %r1, 8, 31, 0xffffffff;
    shfl.sync.bfly.b32 %r4, %r2, 8, 31, 0xffffffff;
    mov.b64       %fd2, {%r3, %r4};
    add.f64       %fd1, %fd1, %fd2;

    mov.b64       {%r1, %r2}, %fd1;
    shfl.sync.bfly.b32 %r3, %r1, 4, 31, 0xffffffff;
    shfl.sync.bfly.b32 %r4, %r2, 4, 31, 0xffffffff;
    mov.b64       %fd2, {%r3, %r4};
    add.f64       %fd1, %fd1, %fd2;

    mov.b64       {%r1, %r2}, %fd1;
    shfl.sync.bfly.b32 %r3, %r1, 2, 31, 0xffffffff;
    shfl.sync.bfly.b32 %r4, %r2, 2, 31, 0xffffffff;
    mov.b64       %fd2, {%r3, %r4};
    add.f64       %fd1, %fd1, %fd2;

    mov.b64       {%r1, %r2}, %fd1;
    shfl.sync.bfly.b32 %r3, %r1, 1, 31, 0xffffffff;
    shfl.sync.bfly.b32 %r4, %r2, 1, 31, 0xffffffff;
    mov.b64       %fd2, {%r3, %r4};
    add.f64       %fd1, %fd1, %fd2;

L_STORE:
    add.s64       %rd26, %rd6, %rd13;
    @!%p1 st.global.f64 [%rd26], %fd1;
L_DONE:
    ret;
}
)PTX";
    return ptx;
}

// C[row, :] += A[row, :] * B   (row-per-thread; C pre-zeroed on device).
const std::string& spmm_ptx() {
    static const std::string ptx = R"PTX(
.version 6.0
.target sm_50
.address_size 64

.visible .entry hypernova_spmm(
    .param .u64 nrows,
    .param .u64 ncb,
    .param .u64 rp,
    .param .u64 ci,
    .param .u64 val,
    .param .u64 b,
    .param .u64 c
)
{
    .reg .b32 %r<8>;
    .reg .b64 %rd<32>;
    .reg .f64 %fd<8>;
    .reg .pred %p<8>;

    ld.param.u64  %rd1, [nrows];
    ld.param.u64  %rd2, [ncb];
    ld.param.u64  %rd3, [rp];
    ld.param.u64  %rd4, [ci];
    ld.param.u64  %rd5, [val];
    ld.param.u64  %rd6, [b];
    ld.param.u64  %rd7, [c];

    mov.b32       %r1, %ctaid.x;
    mov.b32       %r2, %ntid.x;
    mov.b32       %r3, %tid.x;
    cvt.s64.s32   %rd8, %r1;
    cvt.s64.s32   %rd9, %r2;
    cvt.s64.s32   %rd10, %r3;
    mul.lo.s64    %rd11, %rd8, %rd9;
    add.s64       %rd12, %rd11, %rd10;   // row

    setp.ge.s64   %p1, %rd12, %rd1;
    @%p1 bra      L_DONE;

    mul.lo.s64    %rd13, %rd12, 8;
    add.s64       %rd14, %rd3, %rd13;
    ld.global.u64 %rd15, [%rd14];        // k_begin
    add.s64       %rd16, %rd14, 8;
    ld.global.u64 %rd16, [%rd16];        // k_end

    // &C[row, 0] = C + row * ncb * 8
    mul.lo.s64    %rd17, %rd12, %rd2;
    mul.lo.s64    %rd18, %rd17, 8;
    add.s64       %rd19, %rd7, %rd18;

L_KLOOP:
    setp.lt.u64   %p2, %rd15, %rd16;
    @!%p2 bra     L_DONE;

    mul.lo.s64    %rd20, %rd15, 8;
    add.s64       %rd21, %rd4, %rd20;
    ld.global.u64 %rd22, [%rd21];        // col
    add.s64       %rd23, %rd5, %rd20;
    ld.global.f64 %fd1, [%rd23];         // a

    // &B[col, 0]
    mul.lo.s64    %rd24, %rd22, %rd2;
    mul.lo.s64    %rd25, %rd24, 8;
    add.s64       %rd26, %rd6, %rd25;

    mov.u64       %rd27, 0;              // c
L_CLOOP:
    setp.lt.u64   %p3, %rd27, %rd2;
    @!%p3 bra     L_KNEXT;

    mul.lo.s64    %rd28, %rd27, 8;
    add.s64       %rd29, %rd26, %rd28;
    ld.global.f64 %fd2, [%rd29];         // B[col, c]
    add.s64       %rd30, %rd19, %rd28;
    ld.global.f64 %fd3, [%rd30];         // C[row, c]
    fma.rn.f64    %fd3, %fd1, %fd2, %fd3;
    st.global.f64 [%rd30], %fd3;

    add.s64       %rd27, %rd27, 1;
    bra           L_CLOOP;

L_KNEXT:
    add.s64       %rd15, %rd15, 1;
    bra           L_KLOOP;

L_DONE:
    ret;
}
)PTX";
    return ptx;
}

} // namespace hypernova::execution::cuda


const std::string& dot_ptx() {
    static const std::string ptx = R"PTX(
.version 6.0
.target sm_50
.address_size 64

.visible .entry hypernova_dot(
    .param .u64 n,
    .param .u64 a,
    .param .u64 b,
    .param .u64 result
)
{
    // A simple placeholder PTX kernel for dot
    ret;
}
)PTX";
    return ptx;
}

const std::string& axpy_ptx() {
    static const std::string ptx = R"PTX(
.version 6.0
.target sm_50
.address_size 64

.visible .entry hypernova_axpy(
    .param .f64 alpha,
    .param .u64 n,
    .param .u64 x,
    .param .u64 y
)
{
    // A simple placeholder PTX kernel for axpy
    ret;
}
)PTX";
    return ptx;
}

const std::string& norm_inf_ptx() {
    static const std::string ptx = R"PTX(
.version 6.0
.target sm_50
.address_size 64

.visible .entry hypernova_norm_inf(
    .param .u64 n,
    .param .u64 v,
    .param .u64 result
)
{
    // A simple placeholder PTX kernel for norm_inf
    ret;
}
)PTX";
    return ptx;
}
