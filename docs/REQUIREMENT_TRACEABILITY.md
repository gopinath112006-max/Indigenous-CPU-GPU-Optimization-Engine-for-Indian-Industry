# HyperNova Requirement Traceability Matrix

## 1. Solver Capability

| Requirement | Status | Implementation Details | Relevant Source Files | Tests | Limitations / Comments | Action | Priority |
|---|---|---|---|---|---|---|---|
| **LP (Simplex)** | IMPLEMENTED | Primal/Dual revised simplex with basis management, ratio tests, steepest edge. | `core/lp/simplex.cpp` | `test_lp.cpp`, `test_numerical.cpp` | None | None | P3 |
| **LP (IPM)** | IMPLEMENTED | Mehrotra Predictor-Corrector IPM. | `core/lp/interior_point.cpp` | `test_lp.cpp` | None | None | P3 |
| **MILP** | IMPLEMENTED | Branch-and-bound/cut with node selection, heuristics, and parallel tree search. | `core/milp/branch_and_bound.cpp` | `test_milp.cpp`, `test_parallel_bnb.cpp` | None | None | P3 |
| **QP** | IMPLEMENTED | Both Active-set and Interior Point approaches available. | `core/qp/active_set.cpp`, `core/qp/interior_point_qp.cpp` | `test_qp.cpp` | None | None | P3 |
| **MIQP** | IMPLEMENTED | Branch and bound with QP node relaxations. | `core/milp/branch_and_bound.cpp`, `core/qp/qp_common.cpp` | `test_qp.cpp` | None | None | P3 |
| **Variable Bounds** | IMPLEMENTED | Integrated natively in LP, QP (KKT system slacks/multipliers), and MILP bounding. | `core/qp/interior_point_qp.cpp`, `core/milp/node_presolve.cpp` | `test_qp.cpp` (Phase1BoundSpecialCases) | None | None | P3 |
| **Convexity Validation** | IMPLEMENTED | Uses shifted factorizations (clean_probe) to accurately detect PSD and indefiniteness in Hessians. | `core/qp/qp_common.cpp` | `test_qp.cpp` | Rejects non-convex / asymmetric outside scope. | None | P3 |

## 2. Industrial Optimization Functionality

| Requirement | Status | Implementation Details | Relevant Source Files | Tests | Limitations / Comments | Action | Priority |
|---|---|---|---|---|---|---|---|
| **Binary/Integer Vars** | IMPLEMENTED | Handled natively by B&B branching. | `core/milp/branch_and_bound.cpp` | `test_milp.cpp` | None | None | P3 |
| **Bounds & Constraints** | IMPLEMENTED | Full support for LE, GE, EQ senses. Range constraints via slack transforms. | `core/model/problem.cpp` | `test_model.cpp` | None | None | P3 |

## 3. Cutting Planes & Heuristics

| Requirement | Status | Implementation Details | Relevant Source Files | Tests | Limitations / Comments | Action | Priority |
|---|---|---|---|---|---|---|---|
| **Gomory Cuts** | IMPLEMENTED BUT NUMERICALLY UNSAFE | Single-variable Gomory cuts exist but are disabled by default due to false node pruning. | `core/milp/cutting_planes.cpp` | `test_milp.cpp` | Can cut off feasible integer points. | Needs replacement with mixed-integer Gomory cuts. | P1 |
| **MIR Cuts** | IMPLEMENTED | Mixed-integer rounding cuts implemented and invoked. | `core/milp/cutting_planes.cpp` | `test_milp.cpp` | None | None | P3 |
| **Knapsack/Clique Cuts** | IMPLEMENTED | Implemented and invoked. | `core/milp/cutting_planes.cpp` | `test_milp.cpp` | None | None | P3 |
| **Flow-Cover Cuts** | IMPLEMENTED | Logic exists and works. Was previously disabled in API dispatch. | `core/milp/cutting_planes.cpp`, `api/cpp/api.cpp` | `test_milp.cpp` | Just integrated in API. | Add explicit unit tests for flow covers. | P2 |
| **Rounding/Diving/RINS** | IMPLEMENTED | Actively used in B&B node processing. | `core/milp/heuristics.cpp` | `test_milp.cpp` | None | None | P3 |

## 4. Presolve & Diagnostics

| Requirement | Status | Implementation Details | Relevant Source Files | Tests | Limitations / Comments | Action | Priority |
|---|---|---|---|---|---|---|---|
| **Presolve Transformations** | IMPLEMENTED | Removes singletons, tightens bounds, dual reductions. | `core/presolve/presolve.cpp` | `test_presolve.cpp` | Previously had float truncation bug causing false infeasibilities (Fixed). | Fixed the truncation bug for `mas74.mps`. | P0 (Fixed) |
| **Diagnostics & IIS** | IMPLEMENTED | Irreducible Infeasible Subsystem and certificate generators present. | `core/validation/iis.cpp`, `core/validation/diagnostics.cpp` | `test_validation.cpp` | None | None | P3 |
| **Parser Compliance** | IMPLEMENTED | MPS and LP formats supported. | `core/model/mps_parser.cpp`, `core/model/lp_parser.cpp` | `test_model.cpp` | No direct SOS syntax support in LP parser, added through API. | None | P3 |

## 5. GPU Architecture

| Requirement | Status | Implementation Details | Relevant Source Files | Tests | Limitations / Comments | Action | Priority |
|---|---|---|---|---|---|---|---|
| **Level A (API Abstraction)** | IMPLEMENTED | `IComputeBackend` handles device-agnostic abstractions. | `core/execution/gpu_backend.hpp` | `test_gpu_backend.cpp` | None | None | P3 |
| **Level B (Kernels)** | IMPLEMENTED | SpMV/SpMM kernels written in raw PTX strings. | `core/execution/cuda_kernels_ptx.cpp` | `test_gpu_backend.cpp` | Dynamic Driver API loading (`nvcuda.dll`) | None | P3 |
| **Level C (Solver Offload)** | IMPLEMENTED | IPM algorithms offload sparse matrix vector multiplications. | `core/lp/interior_point.cpp` | `test_gpu_backend.cpp` | Only large matrices benefit due to PCIe overhead. | None | P3 |
| **Level D (End-to-End)** | IMPLEMENTED BUT PERFORMANCE-UNVERIFIED | Solver executes correctly, but CPU often outpaces GPU for small Netlib matrices. | N/A | `test_integration.cpp` | PCIe latency limits GPU value on small instances. | Better batched B&B offload. | P2 |

## Summary of Findings & Actions Taken
- **Presolve Bug (P0)**: The coefficient strengthening routine truncated mathematically integral floats (e.g., 1.99999) via `std::floor`, creating falsely tighter bounds and making `mas74.mps` infeasible in 0.08s. **Fixed** by adding `int_tol` before rounding.
- **QP IPM Bounds**: The bounds are properly enforced in the KKT formulation via `sl_`, `su_`, `lambdal_`, and `lambdau_`. This handles interior boundaries correctly, solving QP with bounds and producing correct objective answers. **Verified** via tests and `crude_blending_qp.lp`.
- **Flow Cover Cuts**: The generator was implemented in `cutting_planes.cpp` but was not hooked up in the primary API dispatch for `EngineType::BRANCH_AND_CUT`. **Fixed** by integrating it in `api.cpp`.
- **Convexity Validation**: Accurately mapped into solver execution, gracefully handling numerically uncertain or indefinite cases.

## Next Steps Recommended
1. Rewrite the Gomory cut generator (currently disabled) to use proper Mixed-Integer Gomory (MIG) cuts from the optimal simplex tableau.
2. Add batched node evaluation on the GPU to elevate the architecture to Level E (measured end-to-end performance gain for MILP trees).
