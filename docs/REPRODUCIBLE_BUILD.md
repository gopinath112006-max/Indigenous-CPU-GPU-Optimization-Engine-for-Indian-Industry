# Reproducible Build & Verification Guide

## Requirements
- **OS:** Windows / Linux / macOS
- **Compiler:** GCC 11+ / Clang 14+ / MSVC 2022
- **Build System:** CMake 3.20+
- **GPU (Optional):** NVIDIA Driver (nvcuda.dll / libcuda.so.1). CUDA Toolkit is NOT required.

## Standard Release Build
`ash
git clone <hypernova-repo>
cd hypernova
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel 16
`

## Running the Verification Suite
To verify the mathematical accuracy of the solver:
`ash
cd build
ctest -j16 --output-on-failure
`
Expect exactly 20 CTest suites to pass, validating all LP, QP, MILP, GPU, and parser capabilities.

## Automated Verification Script
You can use 	ools/build_and_test.ps1 (or .sh) to automatically wipe the CMake cache, rebuild from scratch, and execute the CTest suite.
