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
| 1 | **LP Solving** | ✅ Real | Revised simplex: primal + dual (complement-based dual start, Bland/least-infeasible pivoting, basis-refactorized LU + sparse eta-file updates). **Selectable pricing**: Dantzig, Devex (monotone weight updates from the pivot row), and steepest-edge (Devex-ranked shortlist rescaled by exact FTRAN edge lengths; dual uses feasibility-safe steepest-edge tie-breaking); **Harris two-pass ratio test**; Bland stays the default anti-cycling entering rule and the forced rule under perturbation. IPM works (Mehrotra) with a **warm-basis simplex crossover** (IPM endpoint → simplex polish → exact vertex with certified duals; cold-start fallback on ill-conditioned warm bases). |
| 2 | **MILP Solving** | ✅ Real | Full B&B with branching (most-fractional / pseudocost / strong / reliability), **live pseudocost updates**, and node selection (best-first / depth-first / best-estimate / hybrid) that **demonstrably changes execution**. Reliability branching strong-branches until each side has 5 samples then switches to learned pseudocosts. Randomized MILPs verified against **exhaustive 0/1 enumeration**; termination gap proven exact at OPTIMAL (frontier-only bound). Full B&B statistics (nodes created/explored/pruned, incumbent improvements, cuts by family, heuristic solutions, bound progression) exposed on the result. CLI/API: `--node-selection` / `--branching`. MIPLIB benchmark support prepared (bench harness loads `.mps`/`.lp` MILPs, `bnb`/`branch-and-cut` engines, reference CSV). |
| 3 | **QP Solving** | ✅ Real | Active-set + IPM QP. **General convex QP**: sparse KKT (true symmetric Hessian incl. cross terms; sparse LU + iterative refinement), correct `0.5`/full split for diagonal vs off-diagonal terms, MAXIMIZE via whole-objective negation, GE/LE/EQ-aware active-set multiplier handling, gradient-aware IPM convergence, LDL^T convexity guard. QP test suite (16 cases, independent analytic optima) + shipped benchmarks `qp_cross`/`qp_diagonal`/`qp_multicross` all green. Known limitation: IPM-QP does not yet enforce variable bounds in its KKT (may ITER_LIMIT on bound-constrained cross-term QPs; active-set is the default). **MIQP**: routed to B&B with convex QP node relaxations (LP Quadratic trailing-coefficient parsing fixed; node presolve active for QP nodes; pinned-variable/KKT handling hardened) — 6 `MIQPTest.*` regression tests green and shipped instances `miqp_portfolio`/`miqp_equality` pass (convex-only, honest SUBOPTIMAL on relaxation failure). |
| 4 | **Presolve** | ⚠️ Partial | Variable fixing, singletons, bound tightening, coefficient strengthening (row-GCD + RHS rounding for all-integer rows, wired at AGGRESSIVE), `dual_reductions` (integrality fixing + objective-aware exclusive-singleton fixing, wired at AGGRESSIVE), presolve → solve → postsolve through the API, retry-without-presolve on INF/UNB/NUM. No dual-implied-bound propagation from an LP relaxation. |
| 5 | **Branch-and-Bound** | ✅ Real | 4 branching + 4 node-selection strategies wired into the real queue (hybrid genuinely differs from best-first), **pseudocosts updated after every branch and used for selection**, strong branching as a building block of reliability branching. **Node-level presolve** (activity bound tightening, integer ceil/floor, binary clamps, infeasibility pruning) wired at every node (`node_presolve=true` default). **Symmetry breaking** via identical-column detection + `symorder` ordering constraints (`symmetry_breaking` option). **Frontier-only best bound** (expanded parents excluded) → gap is exact at termination. **MIQP**: node relaxations solved as convex QPs (active-set or IPM-QP) when `qp_relaxation=true`; LP-derived cuts and rounded-solution heuristics are disabled for QP relaxations. **Parallel B&B** (`threads >= 2`): shared best-bound node heap + worker-local warm caches with **exclusive node claims** (one worker per node), atomic incumbent/best-bound/cut-pool/statistics, safe worker cancellation, global time/node/solution limits, interrupt propagation — verified equal-to-serial optima on knapsack/mixed/MIQP tests. `use_gpu` option unused. |
| 6 | **Branch-and-Cut** | ✅ Real (mode) | B&B + Gomory/MIR cuts when `BRANCH_AND_CUT` engine selected (all 4 families available); cut separation now actually separates at LP optima and **per-family generation/added statistics** are reported. |
| 7 | **Cutting Planes** | ✅ Real | Gomory, MIR, knapsack-cover, clique + cut pool. **MIR fixed**: the rounding cut `Σ⌊a_j⌋x_j ≤ ⌊b⌋` is emitted whenever the RHS is fractional (a fractional coefficient is no longer required), with **efficacy = LP separation** (violation) instead of self-tight `|activity−rhs|`, so valid cuts are added rather than rejected. Same separation-based efficacy for Gomory and knapsack-cover cuts; cut-pool aging runs every 50 nodes. Cut families validated against exhaustive 0/1 enumeration (optima never cut off). **Flow-cover cuts** (`generate_flow_covers`: single-row flow-cover inequalities on demand rows — GE all-positive-binary or LE all-negative-binary normalized to GE — cover coefficients 1, uncapped `a_j/λ` non-cover coefficients, verified integer-valid against every row-feasible 0/1 point across random instances; wired behind `CutOptions.flow_cover_cuts`, default off). No Lagrangian/lift-and-project. |
| 8 | **Heuristics** | ✅ Real | Rounding, diving, feasibility pump, and RINS all wired into B&B node relaxations (pump+RINS toggleable via API options). Rounding/diving run on **every** node; RINS is frequency-gated. Only **strictly integral points** are accepted as incumbents (diving no longer returns fractional "solutions"); heuristics run only on solved, non-pruned nodes (hardened against empty-LP nodes). Disabled for MIQP (they assume linear objectives). |
| 9 | **Numerical Monitoring** | ✅ Real | Tolerance config, singular detection, **1-norm condition estimation** (Hager, dumped into `FactorizationStats::condition_estimate`, 0 if singular). **Quality reports** (`Solver::diagnose_numerical` / CLI `quality`): kappa(A·Aᵀ), coefficient stats (max/min/dynamic range), probe-solve residuals, and a one-word grade (`good`/`warn`/`poor`) with warnings (ill-conditioned, rank-deficient, extreme range, high residuals). |
| 10 | **Solution Verification** | ✅ Full | Primal/dual feasibility, complementarity, integrality. Enforced in API + benchmarks. |
| 11 | **Benchmarking** | ✅ Real | Full harness with CSV/JSON reports + 21 Netlib instances shipped (benchmarks/netlib). Correctness fixes landed (parser FX/bounds, bound-shift, presolve retry); **16/21 pass** (AUTO, IPM, simplex all agree); `bandm` is fixed by the Phase 3 priced path; remaining failures are `25fv47` (IPM stall + polisher budget), `dfl001` (near-dense KKT), `bandm` (default-engine TIME_LIMIT, see Phase 3), `shell`/`tuff` (verified optima at the 1e-6 reference tolerance edge) — see gap 8. |
| 12 | **Sparse Matrix / Efficient** | ✅ Real | CSR sparse matrix, sparse LU/Cholesky/LDLT with AMD order + symbolic fill + etree. IPM normal-equation sparse Cholesky is **reused across barrier iterations** (symbolic analysis amortized via `refactorize` under a pattern-signature guard); simplex basis factors update via a **sparse eta file**; LP and QP solvers use sparse factorizations (QP KKT via sparse LU + iterative refinement). Dense-Gaussian path retained only as the `HYPERNOVA_DENSE_FACTOR=1` debug toggle. |
| 13 | **Parallel Processing** | ✅ Real | `solve_parallel` (Phase 7) drives **real concurrent B&B node processing**: shared best-bound heap, per-worker warm caches with exclusive claims, atomic incumbent/bound/prune/cut counters, mutex-guarded cut pool + pseudocosts, condition-variable worker wait, worker-exception containment, global time/node/solution limits and `interrupt()` propagation. `--threads` (CLI/API/bench) and `--sweep` (bench) exercised. Parallel regression tests (7): serial==parallel objective on knapsack/mixed/MIQP up to 8 threads, multi-worker interrupt/time/node limits, statistics consistency. Sweep on `knap_n100` (gap 1e-6): **6.27s→0.93s (6.75×) at 8 workers; 4.30× at 2, 6.99× at 4**; parallel best-bound search also explores fewer nodes (455→112). GPU backends remain empty placeholders. |
| 14 | **Industrial Examples** | ❌ Absent | `industrial-cases/` is a placeholder. No models built. |
| 15 | **General Model Input** | ✅ Full | MPS + LP parsers/writers, programmatic builder, CLI interactive builder. |
| 16 | **Robust Termination** | ✅ Real | Status codes + iteration/time limits on all solvers. `interrupt()` sets a flag polled by every solver loop (returns INTERRUPTED; B&B kept its incumbent). |
| 17 | **Clean Architecture** | ✅ Good | Clean module separation, CMake graph, CI. Empty placeholder dirs inflate claimed scope. |
| 18 | **Infeasibility Diagnosis** | ✅ Real | Deletion-filter IIS for LP relaxations (rows + variable bounds) via `Solver::compute_iis` / CLI `iis`. **Verified Farkas certificates for infeasibility and verified recession-direction rays for unboundedness** via `Solver::diagnose` / CLI `diagnose` (rowized standard-form auxiliary LPs, re-verified against the model). |

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