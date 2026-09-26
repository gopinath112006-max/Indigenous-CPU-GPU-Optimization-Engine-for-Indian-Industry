# HyperNova — Indigenous GPU-Accelerated Optimization Solver

HyperNova is an independently implemented mathematical optimization engine designed to provide a license-free and inspectable solver core.

## 1. Capabilities
HyperNova targets Linear Programming (LP) and Mixed-Integer Linear Programming (MILP) workflows. 
The optimization engine does not depend on an external mathematical solver; the project natively implements Parallel Branch-and-Bound, Revised Simplex, and Mehrotra IPM.

HyperNova uses a hybrid CPU/GPU architecture. Solver control flow remains CPU-bound, while selected computationally intensive operations such as sparse matrix-vector multiplication and vector operations can be offloaded to dynamically loaded CUDA/PTX kernels.

## 2. Quick Start
```bash
git clone https://github.com/your-org/HyperNova.git
cd HyperNova
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
ctest --test-dir . -R "test_lp|test_milp|test_api" --output-on-failure
```

## 3. Beginner Guide
Please refer to [docs/BEGINNER_GUIDE.md](docs/BEGINNER_GUIDE.md) for a comprehensive tutorial on using the CLI, C++ API, and REST Server.

## 4. Documentation
- [Capabilities](docs/CAPABILITIES.md)
- [GPU Status](docs/GPU_STATUS.md)
- [Solver Statuses](docs/SOLVER_STATUS.md)
- [Known Limitations](docs/LIMITATIONS.md)
- [Reproducibility](docs/REPRODUCIBILITY.md)
- [Evidence Matrix](docs/EVIDENCE_MATRIX.md)
