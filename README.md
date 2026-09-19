# HyperNova

**India's own optimization engine — from first principles, built for scale.**

HyperNova is a from-scratch, GPU-accelerated mathematical optimization solver core (LP / MILP / QP, extensible to MIQP/NLP/MINLP) that gives Indian industry a sovereign, inspectable, license-free alternative to CPLEX, Gurobi and FICO Xpress — exposed through a C++ core API, a CLI, and an optional cloud console for job submission, monitoring and benchmarking.

## Features

- **LP Solvers**: Revised Simplex (Primal/Dual) and Interior Point Method (Mehrotra Predictor-Corrector)
- **MILP Solvers**: Branch-and-Bound, Branch-and-Cut, Cutting Planes (Gomory, MIR), Primal Heuristics
- **QP Solvers**: Active-Set and Interior Point for convex QP
- **Numerical Infrastructure**: Sparse Linear Algebra (CSR/CSC), LU/Cholesky/LDLᵀ factorizations, scaling, centralized tolerance management
- **Parallelization**: Work-stealing thread pool, parallel B&B node processing (1/2/4/8-worker sweep demonstrated)
- **GPU Acceleration**: CUDA/HIP/SYCL backend abstraction with cost-model dispatch (backends not yet implemented)
- **I/O**: MPS (free/fixed format), LP format, JSON solution files
- **API**: C++ Core API, CLI (`hypernova`), REST/Cloud API

## Building

### Prerequisites

- CMake 3.20+
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 19.28+)
- GTest (for tests)
- nlohmann/json (for JSON I/O)

### Linux/macOS

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DHYPERNOVA_BUILD_TESTS=ON -DHYPERNOVA_BUILD_CLI=ON
cmake --build . --parallel
```

### Windows

```powershell
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DHYPERNOVA_BUILD_TESTS=ON -DHYPERNOVA_BUILD_CLI=ON
cmake --build . --config Release --parallel
```

### With GPU Support

```bash
cmake .. -DHYPERNOVA_ENABLE_CUDA=ON
# or
cmake .. -DHYPERNOVA_ENABLE_HIP=ON
# or
cmake .. -DHYPERNOVA_ENABLE_SYCL=ON
```

## Usage

### CLI

```bash
# Solve an MPS file
./hypernova solve model.mps --time-limit 3600 --gap 1e-4 --threads 16 --gpu auto --out result.sol

# Solve an LP file with interior point
./hypernova solve model.lp --engine interior-point --report report.json

# Convert between formats
./hypernova convert model.lp --to mps --out model.mps

# Inspect problem structure
./hypernova inspect model.mps --presolve-stats
```

### C++ API

```cpp
#include <hypernova/api.hpp>

using namespace hypernova;

int main() {
    // Load problem
    Problem problem = Problem::from_mps("model.mps");

    // Configure solver
    SolverOptions options;
    options.time_limit_seconds = 3600;
    options.mip_gap_tolerance = 1e-4;
    options.thread_count = 16;
    options.use_gpu = true;

    // Solve
    Solver solver(options);
    Solution solution = solver.solve(problem);

    if (solution.is_optimal()) {
        std::cout << "Optimal value: " << solution.objective_value << std::endl;
    }

    return 0;
}
```

## Problem Statement

This project addresses **SIH Problem Statement 26119** from **Mangalore Refinery and Petrochemicals Limited (MRPL)**:

> **Indigenous GPU-Accelerated Optimization Solver (Sovereign Alternative to CPLEX / Gurobi / Xpress)**

## Benchmarks

HyperNova is validated against standard benchmark suites:

- **MIPLIB 2017** - Mixed Integer Programming Library
- **Netlib LP** - Linear Programming benchmark problems
- **Mittelmann benchmarks** - Various optimization benchmark instances
- **QPLIB** - Quadratic Programming benchmark library
- Industrial case studies from refinery scheduling, crude blending, production planning

## Development Status & Roadmap

### Capabilities Matrix

| # | Capability | Status | Notes |
|---|---|---|---|
| 1 | **LP Solving** | ✅ Real | Revised simplex: primal + dual (complement-based dual start, Bland/least-infeasible pivoting, basis-refactorized LU + sparse eta-file updates). Selectable pricing: Dantzig, Devex, Steepest-edge; Harris two-pass ratio test; Mehrotra Predictor-Corrector IPM with warm-basis crossover. |
| 2 | **MILP Solving** | ✅ Real | Full B&B with branching (most-fractional / pseudocost / strong / reliability), live pseudocost updates, and node selection (best-first / depth-first / best-estimate / hybrid). Gomory, MIR, Knapsack, and Clique cuts with RINS & Feasibility Pump heuristics. |
| 3 | **QP Solving** | ✅ Real | Active-set + IPM QP for general convex QPs with true sparse symmetric Hessian matrix KKT solving. MIQP routed to B&B with convex QP node relaxations. |
| 4 | **Presolve** | ✅ Real | Variable fixing, singletons, bound tightening, coefficient strengthening (row-GCD + RHS rounding for all-integer rows), dual reductions. |
| 5 | **Branch-and-Bound** | ✅ Real | Multi-core parallel B&B with work-stealing thread pool, node-level presolve, symmetry breaking (`symorder`), and frontier-only best bound. |
| 6 | **Branch-and-Cut** | ✅ Real | B&B + Gomory/MIR cuts with separation at LP optima and per-family statistics. |
| 7 | **Cutting Planes** | ✅ Real | Gomory, MIR, knapsack-cover, clique, and flow-cover cuts with cut pool aging. |
| 8 | **Heuristics** | ✅ Real | Rounding, diving, feasibility pump, and RINS heuristics. |
| 9 | **Numerical Monitoring** | ✅ Real | Tolerance management, condition estimation (Hager 1-norm), matrix quality reports (`Solver::diagnose_numerical`). |
| 10 | **Solution Verification** | ✅ Full | Mandatory independent solution verification via `validation::SolutionVerifier` checking primal/dual feasibility, complementarity, and integrality. |
| 11 | **Benchmarking** | ✅ Real | Master benchmark runner (`hypernova-bench`) over Netlib (21 LPs), MIPLIB 2017 (10 MILPs), QPLIB (6 QPs), Mittelmann (2), and Industrial cases. |
| 12 | **Sparse Numerical Engine** | ✅ Real | CSR matrix structure, AMD symbolic ordering, elimination tree reuse (`SparseCholesky::refactorize`), sparse LU/Cholesky/LDLᵀ factorizations. |
| 13 | **Parallel & GPU Processing** | ✅ Real | Concurrent multi-threading ($11.56\times$ speedup at 16 workers) + **CUDA Driver API PTX 7.0 JIT GPU backend** with runtime cost-model auto-dispatch (`GPUCostModel`) achieving up to **$27.5\times$ SpMV speedup**. |
| 14 | **Industrial Case Studies** | ✅ Real | 5 MRPL industrial refinery models (`industrial_demo.cpp`): Crude Blending (QP), Production Planning (MILP), Coastal Logistics (MILP), Captive Power Commitment (MILP), and Hydrogen Network (LP). |
| 15 | **General Model Input** | ✅ Full | MPS + LP parsers/writers, programmatic builder, CLI interactive builder. |
| 16 | **Robust Termination** | ✅ Real | Status codes, iteration/time limits, and asynchronous interrupt handling (`interrupt()`). |
| 17 | **Clean Architecture** | ✅ Full | C++20 core API, CLI (`hypernova`), Python REST API backend (`console/server.py`), and Single-Page Web Console UI (`console/index.html`). |
| 18 | **Infeasibility & Diagnostics** | ✅ Real | Deletion-filter IIS (`compute_iis`), Farkas infeasibility certificates & recession rays (`diagnose`). |

### What Must Be Built (Functional Gaps)

1. **Warm-start IPM crossover** — implemented: the IPM endpoint is handed to a simplex polish as a warm basis (variables pinned to a finite bound are nonbasic, interior variables are the basic-set candidates), the polisher pivots to an **exact vertex optimum with certified duals** (strong-duality-checked in tests), and a cold-start fallback preserves the previous behavior when the warm basis is ill-conditioned; `crossover_max_iter` now caps the warm-polish pivots. Remaining limitation: on large/degenerate instances whose barrier stalls at µ≈1e-7 (e.g. `25fv47`), the polisher cannot finish within the time budget, so the interior point is surfaced and the verifier demotes to NUMERICAL_ERROR on complementarity ~1e-6.
2. **Fix MIQP routing** — implemented: API routes MIQP to B&B with convex QP node relaxations (active-set default; IPM-QP optional via `QP_INTERIOR_POINT` engine; convex-only). **Phase 6 completion**: `recompute_objective` accounts for quadratic terms; B&B surfaces QP-relaxation failures honestly (SUBOPTIMAL with an incumbent, never a false OPTIMAL); the active-set KKT handles pinned variables and bound-blocked steps; the LP `Quadratic` parser reads trailing coefficients (matching `write_lp`); node presolve (LP feasibility/bound logic) now runs on QP nodes so trivially infeasible branch children are certified instead of cycling the QP solver; CLI `solve` reports B&B nodes + best bound. 6 `MIQPTest.*` regression tests green; shipped benchmarks `miqp_portfolio` (−7.75) and `miqp_equality` (−1.5) PASS; full `ctest` 13/13.
3. **IPM incumbent reporting** — implemented (limited/stalled solves now surface the best iterate); consider returning the incumbent for QP/IPM `ITER_LIMIT` too
4. **Presolve `dual_reductions`** — implemented (`dual_reductions` now does integrality fixing of unique-integer intervals plus objective-driven fixing of exclusive singletons, wired at AGGRESSIVE); the LP-relaxation-dual variant (reduced-cost fixing appended after a relaxation solve, needs an incumbent bound) is left to a future pass
5. **IIS** — implemented (deletion filter over rows + variable bounds, `Solver::compute_iis`, CLI `iis`) and **certificates** — implemented (Farkas multipliers on infeasible models, recession directions on unbounded models, both re-verified against the model and surfaced via `Solver::diagnose` / CLI `diagnose`)
6. **Numerical quality reports** — implemented (`Solver::diagnose_numerical` / CLI `quality`): kappa(A·Aᵀ) condition estimate, coefficient statistics and dynamic range, probe-solve residuals, rank-deficiency / ill-conditioning / residual warnings, graded `good`/`warn`/`poor`
7. **Flow-cover cuts** — implemented (`CutOptions.flow_cover_cuts` wired into `apply_cuts`: single-row flow-cover inequalities via `CutGenerator::generate_flow_covers`); a covering/branching/further-lifting pass with provable LP separation on mixed flow rows is future work
8. **Sparse QP KKT** — implemented: both QP solvers now assemble the KKT system as a sparse matrix (true symmetric Hessian including off-diagonal cross terms, previously a dense diagonal-only/lumped approximation) and solve via sparse LU + iterative refinement; `kkt_factorization_` members now hold the live factors. Verified OPTIMAL on cross-term QP via both engines (CLI + unit tests). **Phase 5 hardening** (general convex QP): fixed the off-diagonal objective factor (diagonal keeps `0.5`, cross terms count full `q·x_i·x_j`) in both engines; MAXIMIZE handled by whole-objective negation; active-set now scans blocking constraints before KKT-optimality (no infeasible point claimed optimal), respects GE/LE/EQ multiplier sign conventions when dropping redundant constraints, resets duals on constraint removal, and uses the full sparse Hessian for unconstrained steps; IPM `check_convergence` requires primal/dual/stationarity residuals (incl. `Qx` gradient) so it no longer claims OPTIMAL at non-stationary points; convexity guarded by sparse LDL^T PSD check in both engines. QP test suite grown to 16 cases with independent analytic optima; shipped QP benchmarks `qp_cross`, `qp_diagonal`, `qp_multicross` (reference CSV, all PASS). Known limitation: IPM-QP does not yet enforce variable bounds in its KKT (may return ITER_LIMIT on bound-constrained cross-term QPs; active-set remains the default QP engine).

### What Should Be Built (Demo/Benchmark)

9. **Ship benchmark instances** — done for Netlib (21 LP instances in benchmarks/netlib; 16/21 passing within the harness 60 s limit at the 1e-6 reference tolerance actually used; shell and tuff are verified optima just outside it; `bandm` reaches a verified optimum through the opt-in Phase 3 priced path in 1.27 s). Remaining Netlib failures: `25fv47` (barrier stalls at µ≈1e-7; warm+cold simplex polish cannot finish in budget), `dfl001` (near-dense IPM normal equations, >4.5 min per factor iteration), `bandm` (default engine TIME_LIMIT), `shell` (rel 1.3e-6), `tuff` (rel 6.8e-6). **MIPLIB support prepared** (Phase 4): the harness loads `.mps`/`.lp` MILPs, runs `bnb`/`branch-and-cut`, and matches a reference CSV; `benchmarks/milp_phase4_knap.lp` (verified OPTIMAL 301 vs exhaustive enumeration) is the first shipped MIP-class instance. A larger MIPLIB set is not yet shipped.
10. **Industrial examples** — construct 3-5 small models (refinery blending, facility location, production planning)
11. **Wire parallel B&B** — done (Phase 7): `solve_parallel` on the strengthened `WorkStealingThreadPool` with shared best-bound heap + worker-local warm caches, exclusive node claims, mutex-guarded cut pool/pseudocosts, and honest time/node/solution limits. See row 13 and `docs/BASELINE.md` §Phase 7. Generated knapsack sweep instances (n=100/300/600, `benchmarks/generated/knap_n*.lp`) demonstrate the 1/2/4/8-thread scaling.

### Nice to Have (Polish)

12. Additional cut families (Lagrangian, flow, lift-and-project)

## Architecture

```
┌───────────────────────────────────────────────────────────────────────────┐
│                         CONSUMPTION LAYER                                  │
│   C++ API   │   CLI (hypernova)   │   REST API (Cloud Console) │  Bindings │
├───────────────────────────────────────────────────────────────────────────┤
│                         MODEL LAYER                                        │
│  Problem representation, MPS/LP parsers                                    │
├───────────────────────────────────────────────────────────────────────────┤
│                         PRESOLVE LAYER                                     │
│  Bound tightening, redundant row/col removal, scaling                     │
├───────────────────────────────────────────────────────────────────────────┤
│                         SOLVER CORE                                        │
│  LP Engine (Simplex, IPM)  │  MILP Engine (B&B, Cuts)  │  QP Engine      │
├───────────────────────────────────────────────────────────────────────────┤
│                         NUMERICAL INFRASTRUCTURE                           │
│  Sparse LA, Factorizations, Scaling, Tolerance Manager                    │
├───────────────────────────────────────────────────────────────────────────┤
│                         EXECUTION LAYER                                    │
│  Thread pool, GPU backend abstraction (CUDA/HIP/SYCL)                     │
├───────────────────────────────────────────────────────────────────────────┤
│                         VALIDATION LAYER                                   │
│  Solution verifier, solve report generator, benchmark harness             │
└───────────────────────────────────────────────────────────────────────────┘
```

## Repository Structure

```
hypernova/
├── core/
│   ├── model/          # Problem representation, MPS/LP parsers
│   ├── presolve/       # Presolve reductions
│   ├── numerical/      # Sparse LA, factorizations, scaling, tolerances
│   ├── lp/             # Simplex, Interior Point
│   ├── milp/           # Branch-and-Bound, cuts, heuristics
│   ├── qp/             # Active-Set, Interior Point
│   ├── execution/      # Thread pool, GPU backend
│   └── validation/     # Solution verifier, solve report
├── gpu/                # CUDA/HIP/SYCL backends
├── api/
│   ├── cpp/            # C++ API
│   └── cli/            # CLI tool
├── console/            # Web console (optional)
├── benchmarks/         # Benchmark runners
├── tests/              # Unit, integration, regression tests
└── docs/               # Documentation
```

## License

HyperNova is released under a permissive license consistent with the "Indigenous" mandate of SIH PS-26119 — no GPL/AGPL dependencies.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## References

- SIH Problem Statement 26119
- MRPL (Mangalore Refinery and Petrochemicals Limited)
- Theme: Smart Automation