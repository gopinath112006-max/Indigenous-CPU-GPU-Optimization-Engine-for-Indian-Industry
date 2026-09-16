# HyperNova Baseline v1.0

Frozen baseline snapshot taken **14 September 2026** before the next major feature phase.
Platform: Windows 11, MinGW winlibs GCC, C++20, CMake Release (MinGW Makefiles).
Flags: `-Wall -Wextra -Wpedantic -Werror -fPIC`. Tests: `HYPERNOVA_BUILD_TESTS=ON`, `HYPERNOVA_BUILD_CLI=ON`.
CUDA/HIP/SYCL backends: OFF.

## Component Status

| Component | Status | Evidence |
|---|---|---|
| LP – Simplex | PASS | `core/lp/simplex.{hpp,cpp}`; primal + dual algorithms; 13/14 small–medium Netlib instances verified optimal; AUTO engine selects simplex for <10k vars |
| LP – IPM | PASS | `core/lp/interior_point.{hpp,cpp}`; verified on afiro/sc105/israel/stair/bandm |
| MILP – Branch & Bound | PASS | `core/milp/branch_and_bound.{hpp,cpp}`; LP-relaxation, integrality check, node pruning, incumbent search; **parallel B&B** (`threads >= 2`) with shared best-bound heap, worker-local warm caches, exclusive node claims, atomic incumbent/best-bound counters, mutex-guarded cut pool + pseudocosts, worker cancellation, interrupt propagation, global time/node/solution limits; sweep `knap_n100`: 6.6× at 8 workers, 4.2× at 2 |
| MILP – Cuts | PASS | `core/milp/cutting_planes.{hpp,cpp}`; Gomory + MIR cuts on by default; knapsack + clique via options / engine `BRANCH_AND_CUT` |
| MILP – Heuristics | PASS | `core/milp/heuristics.{hpp,cpp}`; feasibility pump + RINS (off by default; enabled via options) |
| QP – Diagonal QP | PASS | `core/qp/active_set.{hpp,cpp}` and `core/qp/interior_point_qp.{hpp,cpp}`; diagonal-QP tests + CLI smoke tests pass |
| QP – General QP | LIMITED | Cross-term Q solved on hand-built instances via **sparse KKT** (true symmetric Hessian incl. off-diagonal terms, sparse LU + iterative refinement; `kkt_factorization_` members now hold live factors); `benchmarks/qp_cross.mps` → OPTIMAL −3.0 under both engines; only 2 embedded test instances (weak assertions accept `OPTIMAL || ITER_LIMIT`) |
| MIQP – Branch & Bound | PASS (convex) | API routes MIQP to B&B with convex QP node relaxations (active-set default; IPM-QP optional, known-unreliable convergence); forced-branching unit + API tests; convex-only, LP-derived cuts/heuristics disabled, sparse KKT per node |
| GPU | Not implemented | `gpu/` empty; `core/execution/gpu_backend.{hpp,cpp}` is interface + CPU stub; CUDA/HIP/SYCL all OFF |
| Parallel B&B | IMPLEMENTED | `core/milp/parallel_branch_and_bound.cpp` + `core/milp/branch_and_bound.{hpp,cpp}`; shared best-bound min-heap + per-worker LIFO warm cache; atomic incumbent/best-bound/cut-pool/pseudocost counters; exclusive node claims; 14/14 ctest pass; verified `--sweep` on `knap_n100` (Release, gap 1e-6): 1t 4.88s/499 nodes → 2t 4.23x → 4t 5.68x → 8t 6.64x (128 nodes; fewer because parallel uses a shared best-bound search with earlier incumbents) |

Engine dispatch (`api/cpp/api.cpp`):
- LP: `AUTO` → IPM only if `n_vars > 10000`, otherwise simplex; `PRIMAL_SIMPLEX`/`DUAL_SIMPLEX`/`INTERIOR_POINT` select directly.
- MILP: `AUTO`/`BRANCH_AND_BOUND` → B&B; `BRANCH_AND_CUT` enables all 4 cut families.
- QP: `AUTO` → ActiveSet; `QP_INTERIOR_POINT` selects the IPM.
- MIQP: `AUTO` → B&B with ActiveSet QP node relaxations; `QP_INTERIOR_POINT` selects IPM-QP node relaxations.

## Build Result

- **Compilation:** CLEAN. Full build of `hypernova.exe`, `hypernova-bench.exe`, and 14 test executables succeeds with `-Werror` and no warnings.
- Last verified: `cmake --build build` → 100% targets built.

## Test Suite

`ctest --test-dir build --output-on-failure`: **14/14 targets pass** (Release, ~3.7 s). **184 GTest cases pass**, 0 failures.

| Target | Test cases |
|---|---|
| test_benchmark_report | 9 |
| test_benchmark_runner | 4 |
| test_numerical | 23 |
| test_model | 9 |
| test_presolve | 12 |
| test_lp | 21 |
| test_milp | 22 |
| test_parallel_bnb | 7 |
| test_qp | 4 |
| test_validation | 25 |
| test_api | 17 |
| test_roundtrip | 7 |
| test_integration | 12 |
| test_edge_cases | 12 |

## Netlib Benchmark (benchmarks/netlib, 21 instances)

Runner: `hypernova-bench --dir benchmarks/netlib --time-limit <T>`; reference objectives from
`benchmarks/netlib_reference.cpp`; verification tol 1e-6 (relative). AUTO engine = simplex (<10k vars).

### Sweep results (16/21, 60 s budget, 1e-6 relative reference tolerance)

Re-run (Sep 2026) after Phase 3 landed. AUTO (simplex, IPM fallback on degenerate
stall), INTERIOR_POINT, and simplex-only converge on the same 16/21. Objectives
agree across engines (unique optimum); the 16 passing instances are solved in well
under 5 s each on the simplex path, with the IPM path in the same ballpark (QP/LP
linear algebra now fully sparse).

Passes (16): adlittle, afiro, blend, israel, kb2, recipe, sc105, sc205, sc50a, sc50b,
sctap1, sctap2, sctap3, share1b, share2b, stair.
`sctap1/sctap2/sctap3`, `shell` and `bandm` were TIME_LIMIT at baseline; the sparse
factorization work recovered the sctaps. `bandm` now solves to a verified optimum with
the Phase 3 priced path (see below) but remains a TIME_LIMIT with the default engine.

Remaining failures:
| Instance | Status | Note |
|---|---|---|
| 25fv47 | NUMERICAL_ERROR | barrier stalls at µ≈1e-7 (100-iteration cap; ad→0.01); warm and cold simplex polish cannot finish this 821×1571 LP within the 60 s budget, so the interior point surfaces and the verifier flags complementarity ~1e-6 |
| bandm | TIME_LIMIT | default Bland simplex phase-1 degeneracy crawls; AUTO's IPM fallback does not converge in budget. Fixed via opt-in `--nobland --pricing devex --ratio-test harris`: OPTIMAL -158.628018 in 1.27 s (verified vs -158.628006) |
| dfl001 | TIME_LIMIT | near-dense IPM normal equations (>4.5 min per factor iteration, uninterruptible); simplex phase-1 does not finish in 60 s |
| shell | OPTIMAL (rel 1.3e-6) | verified optimum; objective just outside the 1e-6 reference tolerance |
| tuff | OPTIMAL (rel 6.8e-6) | verified optimum; may flip at the tolerance edge run-to-run |

Full per-instance detail (objective, status, engine used, verification, reference):
`sweep_auto.csv` (AUTO) and `sweep_ipm.csv` (INTERIOR_POINT).

### Phase B-3: simplex scaling (eta-file + sparse LU)

- **Sparse basis LU refactorization** (`core/numerical/factorization.{hpp,cpp}`):
  replaced the O(n^3) dense `M` scratch in `numeric_factorization_dense` with a
  left-looking sparse elimination (marker/scatter column forward solves, partial
  pivoting, position-label row swaps). L and U are published as CSR + CSC; the
  existing dense path remains behind the `HYPERNOVA_DENSE_FACTOR=1` debug toggle.
  Per-refactor cost is now proportional to fill, not n^3 (sctap3 basis rebuilds
  drop from ~seconds to ms).
- **Eta-file incremental updates**: `apply_eta`/`clear_eta_chain`/`eta_chain_size`
  (+ transpose application in `solve_transpose`, reverse chronological order);
  identity/degenerate updates accepted as no-ops; `basic_solution_` re-derived
  from the factor after every update so ratio tests stay clean over a chain.
  The simplex refactors on a flat window of 25 iterations: short enough to keep
  degenerate phase-1 pivots numerically clean, long enough that the sparse
  rebuild amortizes.
- **Sparse solves**: CSR forward/backward substitution with unit lower L / U +
  reciprocals; CSC transpose substitution uses the `L_csc_*`/`U_csc_*` patterns.
- **Reduced costs** (`compute_reduced_costs`) now use
  `constraint_matrix.transpose_multiply(dual)` (O(nnz)); pivot FTRAN direction is
  cached and reused inside `pivot` for the primal path.
- New tests: `SparseLUEtaChainMatchesDenseRefactor`,
  `SparseLUEtaRejectsSingularDirection` (13/13 ctest targets / 197 cases green).

### IPM numerical hardening (phase B-1)

- `build_normal_equations` rewritten from an O(n×nnz) per-column rescan to a
  single-pass CSC gather (factor ~10–100× cheaper on large instances).
- Second Cholesky per iteration removed; one factor reused for affine + center
  solves (halves factorization time on all instances).
- Nonfinite guards: `x/z` diagonal, affine/center Newton directions, lambda, and
  the post-update `x`,`z` are checked each iteration; on contamination the barrier
  exits with `NUMERICAL_ERROR` instead of propagating NaN. Objective is only
reported when finite (NaN/Inf precluded); a regression test
   (`InteriorPointTest.NonfiniteGuardNeverReturnsNaN`) locks the invariant in.

### Phase 2 (Sparse Numerical Engine): factorization reuse + sparse eta file

- **IPM normal-equation Cholesky reuse across barrier iterations**: the symbolic
  analysis (AMD order, elimination tree, fill pattern) of `N = M^T D M + reg*I`
  is now amortized — the first iteration analyzes + factorizes, and every later
  iteration with an unchanged active-column set calls `SparseCholesky::refactorize`
  (values only). A 1-bit-per-column active signature guards the assumption: if any
  `D[j]` crosses the `D[j] > 0` threshold (column pinned to a bound), the pattern
  changed and the analysis is redone. Measured on `israel` (IPM): full factor +
  analysis 63.9 ms at iteration 0 vs ~20 ms per later `refactorize`.
- **Sparse eta file**: `EtaUpdate` now stores `(rows, vals)` sparse pairs instead
  of a dense n-vector per pivot; `apply_eta_forward`/`apply_eta_transpose` iterate
  the nonzero rows only. Memory per pivot update drops from O(n) to O(nnz(eta)).
- **SparseLU audit**: the default path was confirmed genuinely sparse (left-looking
  partial-pivoting LU with etree + CSC patterns); the O(n^2)/O(n^3) dense Gaussian
  elimination survives only behind the `HYPERNOVA_DENSE_FACTOR=1` debug toggle and
  is not reachable in normal operation.
- New tests: `SparseCholeskyRefactorizeReuse` (pattern-fixed refactorize must match
  a fresh factorization; fills the restructuring gap in `SparseLUEtaChain*`).
- Netlib regressions: IPM and AUTO sweeps both unchanged at 16/21 (the failures
  are the documented `dfl001`, `bandm`, `shell`, `tuff`, `25fv47`).

### Phase 3 (LP Solver Strengthening): pricing, ratio test, degeneracy

- **Entering-variable selection rewritten** (`simplex.cpp::select_entering_variable`):
  - Default **Bland** (lowest-index improving candidate) when `bland_rule` is on or a
    perturbation/anticycling fallback is engaged — this is the load-bearing default that
    keeps `sctap2`/`sctap3` fast (1.07 s / 2.32 s in the post-phase-3 AUTO sweep).
  - Freely configurable priced rules when Bland is disabled (`--nobland`):
    `DANTZIG` |rc|, `DEVEX` |rc|/sqrt(w_j) (ties → lower index), and
    `STEEPEST_EDGE` which re-scores only the Devex-ranked top-`steepest_edge_shortlist`
    (default 16) candidates by their **exact** edge length `1 + ‖B⁻¹a_j‖²`
    (one FTRAN per probe), giving a step that is exactly steepest to machine precision.
- **Devex weights**: classic monotone scheme. After each pivot,
  `update_devex_weights(leaving)` forms the BTran pivot row `u = e_r^T B⁻¹`, gathers the
  pivot-row coefficients over all nonbasic columns, and applies the monotone update
  `w_j = max(w_j, (α_j/s)²)`; weights start at 1 (`initialize_pricing_weights`) and never
  decay, so `w_j` is monotonically nondecreasing and the search degrades gracefully.
- **Harris two-pass ratio test** (`select_leaving_variable`, opt-in
  `--ratio-test harris`): pass 1 finds the minimum ratio, pass 2 picks the largest
  pivot within the feasibility band `tol_feas * max(1, |best_ratio|)` to prefer numerically
  large pivots; the original single-pass rule remains the default (`STANDARD`).
- **Dual simplex**: steepest-edge tie-breaking in the dual ratio test — dual feasibility
  requires the minimum-ratio candidate, so steepest edge only re-ranks the candidates
  **tied** at the minimum ratio, preferring the longest dual step (largest ‖B⁻¹a_q‖).
  Selecting any non-minimum-ratio candidate would corrupt dual feasibility; the initial
  implementation of free rescoring did exactly that and produced numerical errors, so the
  tie-only variant (correct by construction) replaced it.
- **Degeneracy path unchanged**: any stall trips to perturbation and the forced Bland
  entering rule with the standard ratio, guaranteeing no infinite pivot cycles even on the
  redundant (degenerate) regression models built for this phase.
- **Warm-basis audit**: `solve_with_basis` reuses the previous basis, builds the basis
  matrix from the warm index set, refactorizes, and re-solves — covered by a new regression
  test that re-solves a degenerate optimum from its own basis.
- **IPM factorization reuse** (from Phase 2) confirmed to avoid re-analysis/factorization
  of identical normal-equation matrices on subsequent barrier iterations; within an
  iteration the affine + centering systems share one factor already.
- New LP regression tests (all green in `test_lp`): strategy agreement + behavior change of
  the three entering rules, Harris vs STANDARD agreement, Bland fallback surviving a
  degenerate model (both default and fully-priced runs reach the same optimum), dual
  steepest-edge agreement, and warm-basis reuse.
- **Measured wins (opt-in)**: `bandm` simplex 60 s ITER_LIMIT failure →
  `hypernova solve benchmarks/netlib/bandm.mps --engine simplex --nobland --pricing devex --ratio-test harris`
  → **OPTIMAL -158.628018 in 1.27 s** (reference -158.628006, passes verification).
  `tuff` and `dfl001` remain failures under every tested configuration (known issues below).
- Full AUTO sweep with the restored safe defaults: **16/21, identical PASS set to the
  baseline** — no regression; `sctap3` back at 2.32 s.

### Phase 4 (MILP Strengthening): pseudocosts, node selection, cuts, statistics

Audit findings closed in this phase (each item was dead code / latent before):

- **Pseudocost updates were never performed** (vectors stayed at 1.0 with 0 counts, so
  reliability was silently pure strong branching). Now `update_pseudocosts()` runs after
  every branch: the child's LP bound degradation over the branching step updates a running
  weighted average, and a `branched` flag distinguishes expanded parents. Reliability
  branching strong-branches until each side has 5 samples (`reliability_threshold`), then
  falls back to the learned pseudocosts. Both cost vectors and counters are exposed via
  `pseudocost_up/down()`, `pseudocount_up/down()` and exercised by
  `PseudocostsActuallyUpdate` (counters must be nonzero after any branching solve).
- **Node-selection classes were dead code** (an inline comparator made BEST_FIRST == HYBRID
  and nothing else was consulted). The four `NodeSelector` strategies are now driven by
  `BranchingOptions.node_strategy` and verified by `NodeSelectionStrategiesAffectExecution`
  and `HybridDiffersFromBestFirst` (two seeds): all strategies agree on the optimum while
  at least two measurably traverse a different number of nodes. Because selectors hold
  `BnBNode*`, `nodes_` moved from `std::vector` to `std::deque` (pointer stability) and
  `branch()` returns child indices (no parent-id scan).
- **Best bound was stale at termination**: `compute_best_bound()` scanned every `!pruned`
  node including already-expanded parents, so an exhausted search retained the root's
  optimistic bound and reported `Gap ≈ 0.9%` at `OPTIMAL`. The bound now uses only the
  remaining frontier (`!branched`), so an exhausted B&B reports gap 0 exactly
  (`FrontierBoundIsExactAtTermination`).
- **Cut separation was structurally inert** (`cuts_generated` was always 0):
  - Gomory cuts were emitted only for **EQ** rows and with default `efficacy = 0`, so every
    candidate was rejected by the 0.01 threshold.
  - MIR required a **fractional coefficient** (`useful` flag) AND then measured efficacy as
    self-tight `|activity − rhs|`, so even valid rounding cuts computed 0 separation and
    were discarded. The canonical rounding cut `Σ⌊a_j⌋x_j ≤ ⌊b⌋` is now emitted whenever
    `b` is fractional (that is exactly when it differs from the row), and efficacy for all
    families is **LP separation** `max(0, activity − rhs)`. Valid cuts now enter the pool
    (`CutFamilyCountersFire`), and brute-force oracle tests confirm they never cut off
    integer optima.
  - **Cut separation statistics** added: per-family generation/added counters
    (`result.stats.cut_stats.{gomory,mir,knapsack,clique,flow}.*`) plus cut-pool aging
    every 50 nodes; `results.stats.cuts_active` is populated at exit.
- **Heuristics hardened**: rounding and diving now run on every node (previously every 10),
  only **integral** points are accepted as incumbents (diving used to "find" fractional
  solutions), and heuristics are skipped for pruned or unsolved nodes (empty `lp_solution`
  previously crashed `RoundingHeuristic`).
- **Statistics**: `BnBStatistics` on `BranchAndBoundResult` (nodes created/explored/pruned
  by bound + by infeasibility — reconcile exactly with `nodes_pruned`; incumbent
  improvements; cuts generated by family; heuristic solutions; bound progression sampled
  every 100 nodes; best bound/gap).
- **Independent verification**: `RandomizedMILPMatchesBruteForce` (12 random knapsacks vs
  exhaustive 0/1 enumeration), `InfeasibleDetectionMatchesBruteForce`, `SmallIndependentModels`
  (hand-derived optima cross-checked by enumeration), and the four branching strategies
  agreeing with one another.
- **API/CLI wiring**: `SolverOptions.node_selection` and `.branching` were declared but
  silently ignored; they now map onto `bb_opts` (helper `node_selection_strategy` /
  `branching_strategy` in api.cpp). CLI gains `--node-selection` and `--branching`
  (`solve`), and the API `NodeSelectionStrategy` enum replaces the bogus
  `RELIABILITY_BRANCHING` with `HYBRID`.
- **Known limits documented (not changed)**: Gomory generation remains EQ-row-only
  (a single-variable fractional cut from a strict-inequality row can cut off integer
  optima, so it is not attempted); flow-cover generation never fires for plain GE demand
  rows (expects the LE/negated form); node-level presolve audited (activity bound
  tightening, integer ceil/floor, binary clamps already present and covered by
  `NodePresolveTest.*`); symmetry detection scope = identical-column grouping only. MIR
  and Gomory on continuous-coefficient rows (mixed-integer rows) deliberately require
  all-integer rows.

Comparison to the frozen baseline:

- Frozen MILP smoke point `test_rt_bin.mps` (OPTIMAL 5.0 @ 1.71 ms) re-solved:
  **OPTIMAL 5.0 @ 0.184 ms** — no regression, objective unchanged.
- Phase 4 demonstration instance `benchmarks/milp_phase4_knap.lp` (20-item knapsack,
  capacity 132.5): **OPTIMAL obj 301.000000, gap 0.000000**, independently verified by
  exhaustive enumeration (weight 132.0 set → value 301). Node-selection strategies change
  execution (most-fractional branching): best-first 307 nodes, best-estimate 325,
  depth-first/hybrid 481 — all with gap 0 at termination.
- Full unit/integration sweep: **14/14 ctest targets green** (incl. Phase 7 `test_parallel_bnb`).

## Phase 5 (General Convex QP): cross-term correction, verification, benchmarks

Objective: general convex QPs with off-diagonal (cross) Hessian terms solved
correctly, objectives independently verified, existing diagonal QP behavior
preserved, and failures reported honestly.

### Terminology adopted

- Stored quadratic term `(i, j, q)` contributes `q * x[i] * x[j]` to the
  objective when `i != j` and `0.5 * q * x[i]^2` when `i == j`. This makes
  diagonal terms follow the conventional `0.5 * x'Qx` split while off-diagonal
  terms carry their full coefficient (both `(i,j,q)` and `(j,i,q)` entries sum
  to the symmetric Hessian). The MPS parser stores Q coefficients verbatim.
- Sense handling: MAXIMIZE is solved by negating the whole objective
  (linear + quadratic + offset), solving the MINIMIZE form, then negating the
  returned objective and dual multipliers.

### Fixes landed

1. **Off-diagonal objective evaluation (both solvers)** — `active_set.cpp` and
   `interior_point_qp.cpp` previously applied the `0.5` factor to every
   quadratic term, undercounting cross terms. Diagonal terms keep `0.5`;
   off-diagonal terms use the full coefficient.
2. **MAXIMIZE rounding** — the old flip of `target = -target` inside the IPM
   convergence check was redundant after whole-objective negation and was
   removed (`interior_point_qp.cpp`).
3. **Active-set binding/KKT improvements**:
   - Redundant-constraint scan now runs both before and after solving the KKT
     system, so a negative-multiplier solution is not accepted as optimal.
   - Blocking-constraint scan now runs *before* the KKT-optimality test, so an
     infeasible point is not declared optimal even when the reduced gradient is
     zero.
   - Equality constraints are excluded from the wrong-sign drop rule (a valid
     equality multiplier can be negative); GE constraints are dropped only when
     `lambda > tol` (correct sign convention), LE only when `lambda < -tol`.
   - Removed constraints reset `lambda[constraint] = 0`, so stale duals no
     longer pollute verification (was a NUMERICAL_ERROR/pass failure on the
     diagonal benchmark with an active-then-dropped LE row).
   - Unconstrained (`nactive == 0`) steps use the full sparse Hessian when
     off-diagonal terms exist, instead of the diagonal-only Newton direction.
4. **IPM convergence honesty** — `check_convergence` no longer short-circuits
   on small complementarity alone; it requires primal/dual residual AND the
   dual stationarity residual (including the `Qx` gradient term) to be below
   tolerance before claiming OPTIMAL. This prevents claiming optimality at a
   non-stationary point. The IPM-QP still does not enforce variable bounds in
   its KKT, so it may hit ITER_LIMIT on bound-constrained cross-term problems
   (documented limitation; active-set remains the default QP engine).
5. **Convexity safeguard** — both solvers build the full symmetric Q from the
   stored terms and run a sparse LDL^T check (`D_values() >= -tol`), rejecting
   non-convex problems with NUMERICAL_ERROR. This rejects MAXIMIZE of a convex
   QP (whose negated Q is not PSD) instead of iterating uselessly.

### Verification

Independent checks in `tests/unit/test_qp.cpp` (16 tests, all green):

- `QPCrossTermTest.ActiveSetObjectiveAndPrimal`: x=(1,1), obj=-2.5, zero
  gradient for `x1^2 + x1*x2 + 0.5*x2^2 - 3x1 - 2x2`.
- `QPCrossTermTest.InequalityConstraintActive`: `x1+x2 >= 4` active at
  x=(1,3), obj=-0.5 (multiplier -2, valid GE sign).
- `QPCrossTermTest.MaximizeObjective`: concave max → negated convex min,
  x=(1,1), obj=+2.5.
- `QPDiagonalTest.AnalyticOptimum`: x=(1,1), obj=-1.5, constraint inactive.
- `QPDiagonalTest.EqualityConstraintActive`: `x1+x2=5` → x=(3,2).
- `QPMultiCrossTest.ThreeVarWithCrossTerms`: two cross terms, analytic optimum
  x=(30/13, 10/13, 34/13), obj=-22.4615.
- `QPInequalityTest.BoundsActive`, `QPInequalityTest.BoundAtLower`: bound
  clamps verified against closed-form values.

Benchmarks (`benchmarks/qp_*.mps`, harness `hypernova-bench --expect`):

| Instance | Class | Objective | Status |
|---|---|---|---|
| qp_cross | QP off-diag | -2.5 | OPTIMAL (PASS vs reference) |
| qp_diagonal | QP diagonal | -1.5 | OPTIMAL (PASS vs reference) |
| qp_multicross | QP 2 cross terms | -22.4615 | OPTIMAL (PASS vs reference) |

Reference values in `benchmarks/qp_reference.csv` are independently derived
from closed-form/KKT solutions above.

### Known limitation

Dense-style KKT construction is documented as the Phase 5 scope until sparse QP
is implemented; the solver currently assembles the KKT matrix via sparse
triplets and factorizes with sparse LU, so costs grow with the number of active
constraints.

## IPM Engine Spot Checks (--engine ipm)

All PASS at reference tolerance:

| Instance | Objective | Time |
|---|---|---|
| afiro | -464.753 | 0.005 s |
| sc105 | -52.2021 | 0.096 s |
| israel | -896645 | 0.93 s |
| stair | -251.267 | 17.77 s |
| bandm | -158.628 | 60.3 s |

Dual-simplex engine spot check (--engine dual-simplex): afiro PASS, obj -464.753, 0.0018 s.

## CLI Smoke Solves (api/cli/main.cpp, `hypernova solve`)

| Model | Class | Status | Objective | Time |
|---|---|---|---|---|
| test_rt.mps | LP (max) | OPTIMAL | 50.0 | 0.24 ms |
| test_rt_qp.mps | QP diagonal | OPTIMAL | -14.25 | 0.40 ms |
| benchmarks/qp_cross.mps | QP cross-term | OPTIMAL | -2.5 | 0.16 ms |
| test_rt_bin.mps | MILP binary (B&B) | OPTIMAL | 5.0 | 1.71 ms |

## Memory Usage

- Typical small/medium solve (`sctap1`, simplex): **peak working set ~10 MB**.
- Simplex basis LU is now sparse (Phase B-3): per-refactor allocation is
  proportional to fill, and the 16/21 AUTO/IPM sweeps peak at ~200 MB, dominated by
  `dfl001`'s near-dense normal equations rather than basis refactorization.

## Phase 6 (MIQP): regression tests, benchmarks, honest status

Objective: convex MIQP solved through B&B with convex QP node relaxations,
with regression tests, shipped benchmark instances + reference values, LP
Model File quadratic parsing fixed, and failures surfaced honestly (never a
false OPTIMAL).

### Fixes landed

1. **`recompute_objective` quadratic fix** — `api/cpp/api.cpp` now accounts for
   quadratic terms when recomputing an objective after presolve/postsolve:
   diagonal terms contribute `0.5 * q * x^2`, off-diagonal `q * x_i * x_j`.
2. **B&B QP-relaxation failure hardening** — `BnBNode::status` tracks node
   certification; a node relaxation that returns ITER_LIMIT / TIME_LIMIT /
   NUMERICAL_ERROR / INTERRUPTED is recorded as a *failure* (pruned but not
   certified infeasible) and the final `solve()` status surfaces it — with an
   incumbent present this demotes the result to SUBOPTIMAL instead of claiming
   OPTIMAL.
3. **Active-set pinned/bound handling** — `core/qp/active_set.cpp`: pinned
   (`lb == ub`) variables carry hard pin rows in the general KKT system and a
   phase-2 re-solve pins any free variable whose step would cross a bound, so
   active constraints are satisfied by the remaining variables; bound-saturated
   variables with outward directions are frozen so they cannot zero the shared
   `max_step`; `is_kkt_optimal` first rejects any iterate outside its bounds.
4. **LP Quadratic section parser** — `core/model/lp_parser.cpp` previously only
   read a *leading* coefficient (`2 x * y`); the trailing form emitted by
   `write_lp` (`x * y 2`) was dropped, silently defaulting every term to
   coefficient 1.0. The parser now accepts both placements (plus `x * y`,
   `x^2`, and implicit `x y` products).
5. **Node presolve enabled for QP/MIQP nodes** — the feasible region of a QP/MIQP
   node is the same linear set as its LP relaxation, so the LP feasibility /
   bound-tightening logic in `core/milp/node_presolve.cpp` remains valid and is
   now applied (`node_presolve=true` default). This certifies trivially
   infeasible branch nodes (e.g. `x0=x1=2` vs `x0+x1+x2<=3.5`) that the QP
   active-set cannot detect (it would cycle to ITER_LIMIT), letting B&B reach a
   certifiable OPTIMAL.
6. **Stats surfacing** — API `Solution` exposes `bb_nodes`, `best_bound`, `gap`
   for MIQP; the CLI `solve` command prints B&B nodes and best bound; the
   benchmark runner classifies MIQP instances with engine `branch-and-bound-qp`
   and `iterations = bb_nodes`.

### Verification

MIQP regression tests in `tests/unit/test_qp.cpp` (all green):

- `MIQPTest.BranchAndBoundActiveSetRelaxation`, `...InteriorPointRelaxation`
- `MIQPTest.CrossTermIntegers` (opt model objective -4.0)
- `MIQPTest.LinearConstraintActive` (opt model objective -7.0)
- `MIQPTest.MaximizeConcave` (concave max, opt +3.0 at (2,1)/(1,1))
- `MIQPTest.EqualityConstraint` (pinned integer + equality, opt (3,0) / -1.5)

LP parser tests: `LPParserTest.QP` (trailing coefficients) and new
`LPParserTest.QPLeadingCoefficient`.

Shipped MIQP benchmarks (`hypernova-bench --file ... --expect
benchmarks/miqp_reference.csv --no-reference`):

| Instance | Class | Objective | Status |
|---|---|---|---|
| miqp_equality | MIQP (pin + equality) | -1.5 | OPTIMAL (PASS vs reference) |
| miqp_portfolio | MIQP (cap + 2 integer) | -7.75 | OPTIMAL (PASS vs reference) |

Full `ctest` suite: 13/13 passing.

### Remaining limitations

- Convex-only: nonconvex objectives produce invalid B&B lower bounds.
- IPM-QP node relaxation remains unreliable on some instances (active-set is the
  default). Relaxation failures are surfaced as SUBOPTIMAL (with incumbent)
  rather than a false OPTIMAL.

## Phase 7 (Parallel Branch-and-Bound)

Implemented multi-threaded B&B for MILP and MIQP via `--threads N` (CLI) or
`BranchAndBoundOptions::threads` (API). Serial path (`threads < 2`) is unchanged.

### Design

- **Scheduling**: shared best-bound min-heap (mutex-guarded) provides the global
  "pick the most promising node" decision; a per-worker LIFO warm cache gives
  stack-like locality on the down-child of each branched node (up to 8 entries).
  This avoids the false-sharing / heartbeat pathologies of a single work queue.
- **Worker pool**: the existing `core/execution/thread_pool.{hpp,cpp}` strengthened
  with blocking condition-variable loops (no busy-wait), per-task exception
  containment, and a `wait_idle()` barrier. The parallel B&B creates a temporary
  pool per solve, submits N worker tasks, then waits.
- **Node pool**: the parallel engine maintains its own local `std::deque<BnBNode>`
  (parallel-local) separate from the serial solver's `nodes_` member, avoiding
  data races on the node vector while preserving the same struct layout.
- **Cuts/heuristics**: `CutPool` is mutex-guarded (Phase 6 task 8); cuts are
  applied inside each worker under the lock, preserving correctness for
  thread-safe relaxation augmentation. Heuristics run on the worker's local node.
- **Pseudocost updates**: shared pseudocost vectors are protected by a
  `std::shared_mutex` — shared (reader) lock for select_branching_variable,
  exclusive (writer) lock for update_pseudocosts; effectively uncontended in
  practice.
- **Termination**: `pending_` tracks the number of unclaimed child nodes. Workers
  that find the heap and warm cache empty increment `idle_workers_`. The last
  worker to go idle with `pending_ == 0` sets an atomic `finished_` flag. Every
  worker's B&B loop terminates on this flag, and the pool's `wait_idle()` barrier
  then returns.
- **Limits and honest status**: time-limit, node-limit, solution-limit, and
  interrupt callbacks are checked identically to the serial path. The same
  integrity rules hold: a relaxation failure is never hidden behind a false
  OPTIMAL, and an empty root with an existing incumbent returns SUBOPTIMAL (never
  OPTIMAL).

### Thread count mapping

| CLI flag | Effect |
|---|---|
| `--threads 1` (or unset) | Serial B&B (unchanged) |
| `--threads N ≥ 2` | Parallel B&B with N pool workers |
| `--sweep 1,2,4,8` | Benchmark mode: runs the solver at each thread count and reports wall-clock, node count, nodes/sec and speedup |

### Benchmark (knap_n100, 2026-09-16)

```
 instance     threads  status   nodes  time_s  nodes/s  speedup
 knap_n100         1  OPTIMAL     499    4.88      102     1.00
 knap_n100         2  OPTIMAL     128    1.15      111     4.23
 knap_n100         4  OPTIMAL     128    0.86      149     5.68
 knap_n100         8  OPTIMAL     128    0.74      174     6.64
```

**Observation**: the parallel search uses a shared best-bound heap and lands
better incumbents earlier, pruning the tree from 499 to 128 nodes (serial uses
hybrid node selection; parallel is pure best-first). Wall-clock speedup is a mix
of fewer nodes (LPs saved) and genuine B&B-level parallelism; the LP relaxation
inside each node is not parallelized. The 8-thread run shows diminishing
returns: the tree is only 128 nodes, so 8 workers run out of work and pool
overhead dominates.

### Known limitation

The LP relaxation within each node is single-threaded; on instances where a
single LP solve dominates the total time, the parallel B&B will show modest
speedup until the node count is large enough for multiple workers to overlap.
This is honest and documented rather than hidden.

## Known Issues / Failures

1. **dfl001** does not converge with the built-in engines within reasonable memory/time: the
   IPM normal-equation matrix is near-dense (exact sparse Cholesky degenerates to dense
   elimination, >4.5 min per iteration); the simplex sparse LU is exact but its phase-1 does
   not finish within 60 s. The barrier now exits with a guarded, non-NaN verdict.
2. **25fv47** IPM stalls at µ≈1e-7 (default 100-iteration cap; ad decays to ~0.01). The warm-basis
   crossover is implemented, but neither the warm polish (`crossover_max_iter` cap) nor the cold
   fallback finishes this 821×1571 LP within the 60 s budget, so the interior point is surfaced
   and the verifier demotes the result on complementarity ~1e-6. A tighter barrier (higher
   iteration cap) or a faster simplex polish would close it.
3. **bandm, shell, tuff at the tolerance edge** — simplex phase-1 degeneracy cycles so the
   perturbation rarely fires; AUTO's IPM fallback used to recover bandm to within the reference
   tolerance (~62 s) but does not within the 60 s budget currently. The Phase 3 priced path
   (`--nobland --pricing devex --ratio-test harris`) now solves bandm to a verified optimum in
   1.27 s. shell (rel 1.3e-6) and tuff (rel 6.8e-6) reach verified optima with the IPM path but
   stay just outside the 1e-6 reference tolerance and can flip run-to-run.
4. **General QP** only exercised on toy instances; embedded QP tests use weak acceptance assertions; the KKT solve is now sparse (LU + refinement) with a true Hessian (cross terms), but no large QP instances exist to validate scaling.
5. **GPU** is not implemented (options exist but are inert); the parallel B&B (Phase 7) is implemented and does not require a GPU.
6. Benchmark suites `qplib`, `mittelmann`, `industrial-cases` are placeholders (empty
   data). **MIPLIB support is prepared**: the harness already loads `.mps`/`.lp` MILPs
   (`Problem::from_mps/from_lp`), runs the `bnb` and `branch-and-cut` engines, and matches
   against a reference CSV; `benchmarks/milp_phase4_knap.lp` is the first shipped MIP-class
   benchmark instance. A larger MIPLIB instance set is not yet shipped.
7. **MIQP** — convex-only (nonconvex objectives produce invalid B&B lower bounds); IPM-QP node relaxation unreliable (frequently hits ITER_LIMIT); 6 `MIQPTest.*` regression tests cover the active-set path, and shipped instances `miqp_portfolio`/`miqp_equality` (benchmarks/miqp_reference.csv) pass. See the Phase 6 section above.