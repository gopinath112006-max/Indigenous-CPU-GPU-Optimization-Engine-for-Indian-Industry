# HyperNova SIH Evaluator Q&A

### Architecture

**Why C++20?**
C++20 provides zero-overhead abstractions for numerical memory management, parallel execution topologies (thread pools, concurrency), and standard concepts which replace template metaprogramming complexity.

**Why this solver architecture?**
HyperNova is built as an independent matrix solver (Level D). It completely owns the mathematical execution graph without delegating to third-party libraries, ensuring IP sovereignty.

**Why revised simplex?**
Revised Primal/Dual Simplex ensures exact optimum bases necessary for generating valid Mixed-Integer cuts. An IPM-only solver struggles to provide a "warm-start" basis when bounding branch-and-bound nodes.

**Why IPM?**
The Mehrotra Predictor-Corrector Interior Point Method is necessary for large-scale continuous LP bounding and convex QP workloads where boundary walking (Simplex) suffers exponential worst-case iteration counts.

**Why branch-and-cut?**
Plain Branch-and-Bound explodes exponentially. Interleaving cutting planes (like MIG and MIR) geometrically reduces the integer search space before node-branching even occurs.

**Why MIG?**
True Mixed-Integer Gomory cuts utilize the fractional tableau rows of continuous and integer variables to generate mathematically rigorous bounds that avoid dropping the true optimal solution.

**Why CUDA Driver API?**
By using 
vcuda.dll / libcuda.so.1 dynamically via the CUDA Driver API rather than the CUDA Runtime API (cudart), HyperNova embeds the PTX JIT kernels directly inside the executable. This means industrial clients do not need to install the massive 4GB CUDA toolkit; they just need a standard NVIDIA display driver.

### GPU

**What exactly runs on GPU?**
The bottleneck operations of the Mehrotra IPM: Sparse Matrix-Vector (SpMV) multiplications and dense vector dot-products/inf-norms. The constraint matrix A remains resident on the GPU.

**What does not?**
Simplex tableau updates and Branch-and-Bound node trees remain on the CPU, as they suffer from warp-divergence and frequent sparse pivoting which perform poorly on GPU architectures.

**What is CPU fallback?**
If no compatible GPU is detected, ComputeBackendFactory transparently hands the solver a CPUBackend which replicates the math natively.

**What remains unverified?**
Level E: The exact hardware speedup factor on a dedicated SIH Datacenter GPU. CI environments lack the hardware required to beat PCIe-transfer overheads.

### Mathematics

**How is convexity detected?**
HyperNova performs a symbolic and numeric positive-semidefinite (PSD) check on the Q matrix during model ingestion, blocking non-convex quadratic models from entering the convex IPM/Active-Set engines.

### Indigenous Technology

**Which components are implemented from scratch?**
100% of the mathematical algorithms (Simplex, IPM, Branch-and-Bound, Cuts, active-set QP, PTX Kernels, Matrix Factorization algorithms).

**Which external dependencies exist?**
Zero mathematical dependencies. 
lohmann_json is used for REST serialization, and gtest for the test harness. 

### Reliability & Performance

**How are regressions prevented?**
A frozen baseline (0.1.0-rc1) tested against 21 CI test suites, randomized dense MILP property generators, and verified Netlib/MIPLIB benchmarks.
