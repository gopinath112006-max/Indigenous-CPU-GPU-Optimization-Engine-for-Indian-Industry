# HyperNova — Indigenous GPU-Accelerated Optimization Solver
## Complete Product & Technical Design Specification
**SIH Problem Statement ID: 26119 | Organization: MRPL | Category: Software | Theme: Smart Automation**

**Document type:** Production solution blueprint (architecture + engine spec + full web console UI/content)
**Status:** Master reference document — supersedes README-level notes

---

## Table of Contents

1. Product Vision & Positioning
2. Guiding Principles & Non-Negotiables
3. System Architecture (Layered View)
4. Solver Engine — Detailed Module Specification
5. Numerical Infrastructure
6. Parallelization & GPU Acceleration
7. File Formats & I/O
8. APIs (C++ Core API, CLI, REST/Cloud API)
9. Data Model / Database Schema
10. Web Console — Complete Page-by-Page UI & Content
11. Design System (tokens, components, states)
12. Security, Licensing & Compliance
13. Benchmarking & Validation Plan
14. Non-Functional Requirements
15. Repository Structure
16. Delivery Roadmap
17. Team Roles
18. Risk Register
19. Glossary

---

## 1. Product Vision & Positioning

**Name:** HyperNova
**Tagline:** *India's own optimization engine — from first principles, built for scale.*
**One-line pitch:** HyperNova is a from-scratch, GPU-accelerated mathematical optimization solver core (LP / MILP / QP, extensible to MIQP/NLP/MINLP) that gives Indian industry a sovereign, inspectable, license-free alternative to CPLEX, Gurobi and FICO Xpress — exposed through a C++ core API, a CLI, and an optional cloud console for job submission, monitoring and benchmarking.

**Who uses it:**
- Refinery/process engineers (MRPL and similar PSUs) running crude blending, scheduling and production planning models.
- Power system operators doing unit commitment / economic dispatch.
- Logistics & supply-chain planners solving large MILP network problems.
- Research/academic users benchmarking against MIPLIB/Netlib/QPLIB.
- Platform/DevOps teams embedding HyperNova into existing planning software via the C++ API or REST API.

**What it is NOT:**
- Not a modeling language (no AMPL/GAMS-style DSL in v1). Users author models via MPS/LP files or the C++ API.
- Not a general ML platform.
- Not a fork or wrapper of CBC/HiGHS/SCIP/GLPK — everything below the API boundary is original.

**Competitive frame (reference points used for parity, not code):** IBM ILOG CPLEX, Gurobi Optimizer, FICO Xpress, and open-source COIN-OR HiGHS/CBC/SCIP/GLPK. HyperNova's console UX draws conceptually on Gurobi Cloud / CPLEX Studio-style consoles (job submission, log streaming, solution inspection) but is an original implementation, original visual design, and original copy — a "spiritual" reference only, not a clone of any proprietary UI or code.

---

## 2. Guiding Principles & Non-Negotiables

1. **From scratch, mathematically grounded.** No dependency on CBC/HiGHS/SCIP/GLPK/Clp internals. Every algorithm implemented from its mathematical formulation, with derivations documented in `docs/theory/`.
2. **Numerical robustness over raw speed** in early phases — correctness and reliable convergence on ill-conditioned/degenerate problems is the primary success metric, performance parity is secondary but tracked continuously.
3. **Layered, replaceable architecture.** Model → Presolve → Solver → Execution (CPU/GPU) → Validation, with strict interfaces so any layer can be swapped (e.g., a future interior-point QP layer) without touching others.
4. **Determinism by default, parallel by opt-in.** Single-threaded runs must be bit-reproducible; multi-threaded/GPU runs must be within documented tolerance of the deterministic result.
5. **Extensibility.** MILP/LP/QP is v1 scope; class hierarchy and problem representation designed so MIQP, NLP, MINLP are additive, not rewrites.
6. **Transparent engineering.** Every solve returns a machine-readable solve report (iterations, gap, timing breakdown, presolve stats) — no "black box" behavior.

---

## 3. System Architecture (Layered View)

```
┌───────────────────────────────────────────────────────────────────────────┐
│                         CONSUMPTION LAYER                                  │
│   C++ API   │   CLI (hypernova)   │   REST API (Cloud Console) │  Bindings │
│                                                          (Py/planned)      │
├───────────────────────────────────────────────────────────────────────────┤
│                         MODEL LAYER                                        │
│  Problem representation: Variables, Constraints, Objective, Bounds,        │
│  Sparse constraint matrix (CSR/CSC), Problem metadata, MPS/LP parsers      │
├───────────────────────────────────────────────────────────────────────────┤
│                         PRESOLVE LAYER                                     │
│  Bound tightening, redundant row/col removal, singleton elimination,       │
│  coefficient strengthening, probing, dual reductions, scaling              │
│  (geometric + Curtis-Reid), symmetry detection (basic)                     │
├───────────────────────────────────────────────────────────────────────────┤
│                         SOLVER CORE                                        │
│  ┌───────────────┐ ┌────────────────────┐ ┌────────────────────────────┐  │
│  │  LP Engine     │ │  MILP Engine       │ │  QP Engine                 │  │
│  │  - Revised     │ │  - Branch & Bound  │ │  - Active-Set (small/med)  │  │
│  │    Simplex     │ │  - Branch & Cut    │ │  - Interior Point (convex, │  │
│  │  - Interior    │ │  - Cutting planes  │ │    large sparse QP)        │  │
│  │    Point (PDIP)│ │  - Node selection  │ │                             │  │
│  │                │ │  - Heuristics      │ │                             │  │
│  └───────────────┘ └────────────────────┘ └────────────────────────────┘  │
├───────────────────────────────────────────────────────────────────────────┤
│                         NUMERICAL INFRASTRUCTURE                           │
│  Sparse LA (LU, Cholesky, LDLᵀ, Bartels-Golub updates), scaling,           │
│  tolerance manager, residual/condition-number monitoring                   │
├───────────────────────────────────────────────────────────────────────────┤
│                         EXECUTION LAYER                                    │
│  Thread pool (work-stealing), parallel B&B node dispatch,                  │
│  GPU backend abstraction (CUDA / HIP / SYCL), CPU↔GPU cost model            │
├───────────────────────────────────────────────────────────────────────────┤
│                         VALIDATION & TELEMETRY LAYER                       │
│  Solution verifier (primal/dual feasibility, complementarity),             │
│  solve-report generator, benchmark harness (MIPLIB/Netlib/QPLIB runners)   │
└───────────────────────────────────────────────────────────────────────────┘
```

### 3.1 Layer contracts

| Layer | Input | Output | Must guarantee |
|---|---|---|---|
| Model | MPS/LP file or API calls | Canonical `Problem` object (sparse CSR/CSC, bounds, senses) | Lossless round-trip to/from file formats |
| Presolve | `Problem` | Reduced `Problem` + `PresolveMap` (for postsolve) | Exact postsolve recovery of original-space solution |
| Solver Core | Reduced `Problem` | `Solution` (primal, dual, basis/reduced costs, status) | Status ∈ {OPTIMAL, INFEASIBLE, UNBOUNDED, SUBOPTIMAL, TIME_LIMIT, ITER_LIMIT, NUMERICAL_ERROR} always reported honestly |
| Execution | Solver work units (pivots, node evaluations, kernels) | Scheduled/parallel execution results | No silent divergence between CPU-only and GPU-assisted results beyond declared tolerance |
| Validation | `Solution` + original `Problem` | `SolveReport` (verified) | Independent re-check of feasibility before returning OPTIMAL |

---

## 4. Solver Engine — Detailed Module Specification

### 4.1 LP Engine

**4.1.1 Revised Simplex Method**
- Representation: basis inverse maintained implicitly via **LU factorization with Forrest–Tomlin (or Bartels–Golub) updates**, not explicit inverse.
- Pricing: **Dantzig rule** (baseline) → **Devex pricing** (default) → **steepest-edge pricing** (optional, for degenerate/large problems).
- Anti-cycling: **Bland's rule** fallback after a configurable stall-iteration threshold; **perturbation-based anti-degeneracy** as an alternative strategy.
- Ratio test: **Harris two-pass ratio test** with tolerance expansion to combat degeneracy, falling back to a strict ratio test.
- Warm start: basis reuse across re-solves (critical for B&B).
- Both **Primal Simplex** and **Dual Simplex** implemented; Dual Simplex is the default re-optimization method after adding cuts/branching bounds.

**4.1.2 Interior-Point Method (Predictor–Corrector)**
- **Primal–Dual Interior Point (Mehrotra Predictor-Corrector)** for large sparse LPs.
- Normal-equations or augmented-system formulation solved via sparse **Cholesky (normal eq.)** or **LDLᵀ (augmented system, handles free variables/QP)**.
- Regularization (static + dynamic) for ill-conditioned KKT systems.
- Crossover routine to recover a vertex/basic solution from the interior-point solution (needed to seed MILP branching with a basis).

**4.1.3 Engine selection logic**
- Heuristic + user override: dense/small/degenerate → Simplex; very large sparse, well-structured → Interior Point; automatic switch documented in `docs/theory/engine-selection.md`.

### 4.2 MILP Engine

- **Branch-and-Bound (B&B)** core with pluggable strategies.
- **Branch-and-Cut**: B&B integrated with cut generation at nodes.
- **Cutting planes:** Gomory mixed-integer cuts, Mixed-Integer Rounding (MIR) cuts, Knapsack cover cuts, Clique cuts, Flow-cover cuts (phased rollout — Gomory + MIR in Phase 1, others in Phase 2).
- **Presolve inside B&B:** lightweight re-presolve at each node (bound propagation) vs. full presolve at root only (configurable).
- **Branching variable selection:** Most-fractional (baseline) → **Pseudocost branching** → **Strong branching** (default for small/medium node counts) → **Reliability branching** (hybrid, default for large problems).
- **Node selection:** Best-first (best bound), Depth-first (memory-friendly), Best-estimate (pseudocost-based), hybrid diving.
- **Primal heuristics:** Rounding, Diving (fractional/coefficient diving), Feasibility Pump, Relaxation-Induced Neighborhood Search (RINS), Local branching (Phase 2).
- **Cut management:** cut pool, age-based cut removal, per-node cut limits to control LP relaxation blow-up.
- **Parallel B&B:** work-stealing node queue across threads; GPU-assisted LP re-optimization for node relaxations where beneficial (see §6).

### 4.3 QP Engine (convex QP, initial scope)

- **Active-Set method** for small/medium dense or sparse QPs — efficient warm-starting, good for sequences of related QPs (e.g., re-optimization).
- **Interior-Point method** for large sparse convex QPs — extends the LP IPM's KKT system to include the quadratic term (Hessian) in the augmented system, solved via sparse LDLᵀ.
- Convexity check (PSD verification of Q) with clear error reporting if non-convex (non-convex QP explicitly out of scope for v1; flagged as MIQP/NLP roadmap item).

### 4.4 Extensibility hooks (documented, not built in v1)

- `IObjectiveTerm` interface allows quadratic and (future) nonlinear terms to be added to the same `Problem` object without changing the LP/MILP core.
- Branch-and-bound node interface (`INodeRelaxationSolver`) is engine-agnostic so a future NLP relaxation solver can plug into the same B&B shell for MINLP.

---

## 5. Numerical Infrastructure

- **Sparse storage:** CSR (row-major, used for constraint access) and CSC (column-major, used for factorization) with fast conversion; sparse triplet format for I/O.
- **Factorizations:** sparse LU (Simplex basis), sparse Cholesky (IPM normal equations, SPD case), sparse LDLᵀ (IPM augmented system, indefinite/QP case) — implemented with **AMD (Approximate Minimum Degree)** and **symbolic/numeric factorization phases** for fill-in reduction.
- **Scaling:** geometric mean scaling (default) and Curtis–Reid scaling (optional), applied before factorization and undone on solution recovery.
- **Tolerance manager:** single centralized module (`ToleranceConfig`) governing feasibility tolerance, optimality tolerance, pivot tolerance, integrality tolerance, IPM convergence tolerance — all user-configurable, all with safe industrial-grade defaults.
- **Residual/condition monitoring:** every factorization reports estimated condition number; iterative refinement triggered automatically when residuals exceed threshold; numerical-error status surfaced rather than silently returning a wrong answer.

---

## 6. Parallelization & GPU Acceleration

### 6.1 CPU parallelization
- Work-stealing thread pool (custom, no external dependency) for:
  - Parallel B&B node processing.
  - Parallel presolve passes (row/column reductions partitioned).
  - Parallel sparse matrix-vector products.

### 6.2 GPU acceleration (CUDA primary, HIP/SYCL as portable backends)
- **Backend abstraction layer** (`IComputeBackend`) so CPU and GPU implementations of the same kernel are interchangeable and testable head-to-head.
- GPU-accelerated kernels (Phase 2+, after CPU correctness is proven):
  - Sparse matrix-vector / matrix-matrix products (SpMV/SpMM) for IPM iterations.
  - Parallel LP relaxation solves for independent B&B nodes (batched simplex/IPM on GPU).
  - Parallel cut-violation scanning across large cut pools.
- **CPU/GPU decision logic:** cost model based on problem size/density/node-batch size decides at runtime whether to dispatch to GPU (avoids GPU overhead dominating on small problems); decision is logged in the solve report and can be forced via CLI flag.

---

## 7. File Formats & I/O

- **MPS format:** full free-format and fixed-format MPS reader/writer (BOUNDS, RANGES, RHS, SOS1/SOS2 sections supported).
- **LP format:** human-readable LP file reader/writer (CPLEX-LP-style grammar — reimplemented from the published grammar specification, not copied from any solver's source).
- **Solution file formats:** HyperNova `.sol` (JSON-based, includes primal/dual values, reduced costs, basis status, solve metadata) and a plain CPLEX-compatible `.sol`-style text export for interoperability.
- **Solve report:** JSON schema (`solve_report.schema.json`) covering status, objective, gap, iteration counts (simplex iterations / IPM iterations / B&B nodes), timing breakdown per layer, presolve statistics, numerical diagnostics.

---

## 8. APIs

### 8.1 C++ Core API (primary interface)

```cpp
namespace hypernova {

class Problem {
public:
    VariableHandle addVariable(double lb, double ub, VarType type, const std::string& name = "");
    ConstraintHandle addConstraint(const LinearExpr& expr, Sense sense, double rhs, const std::string& name = "");
    void setObjective(const LinearExpr& expr, ObjectiveSense sense);
    void setQuadraticObjective(const QuadExpr& expr); // QP
    static Problem fromMPS(const std::string& path);
    static Problem fromLP(const std::string& path);
    void writeMPS(const std::string& path) const;
};

class SolverOptions {
public:
    double timeLimitSeconds = 3600;
    double mipGapTolerance = 1e-4;
    int threadCount = 0;        // 0 = auto-detect
    bool useGPU = true;
    NodeSelectionStrategy nodeSelection = NodeSelectionStrategy::ReliabilityBranching;
    // ... full tolerance/strategy fields documented in api-reference.md
};

class Solver {
public:
    explicit Solver(const SolverOptions& options = {});
    Solution solve(const Problem& problem);
    Solution warmSolve(const Problem& problem, const Basis& startBasis);
};

} // namespace hypernova
```

### 8.2 CLI (`hypernova`)

```
hypernova solve model.mps --time-limit 3600 --gap 1e-4 --threads 16 --gpu auto --out result.sol
hypernova solve model.lp  --engine interior-point --report report.json
hypernova benchmark --suite miplib2017 --solver hypernova --compare highs
hypernova convert model.lp --to mps --out model.mps
hypernova inspect model.mps --presolve-stats
```

### 8.3 REST / Cloud API (backs the web console, §10)

| Method | Endpoint | Purpose |
|---|---|---|
| POST | `/v1/models` | Upload MPS/LP file, returns `modelId` |
| POST | `/v1/solves` | Submit a solve job (`modelId` + `SolverOptions`) → `jobId` |
| GET | `/v1/solves/{jobId}` | Poll status / partial progress |
| GET | `/v1/solves/{jobId}/stream` | Server-sent events: live log, incumbent updates, gap over time |
| GET | `/v1/solves/{jobId}/solution` | Download solution + solve report |
| DELETE | `/v1/solves/{jobId}` | Cancel a running job |
| POST | `/v1/benchmarks/{jobId}/compare` | Run comparison against a reference solver result set |
| GET | `/v1/apikeys` / `POST /v1/apikeys` | Manage API keys |
| GET | `/v1/usage` | Compute-hour / job usage for the org |

Auth: Bearer API key (server-to-server) or session cookie (console). Rate limiting per org tier (see §10.12 Billing page).

---

## 9. Data Model / Database Schema (Cloud Console backend)

```
organizations(id, name, tier, created_at)
users(id, org_id, name, email, role, created_at)
api_keys(id, org_id, key_hash, label, created_at, last_used_at, revoked_at)
models(id, org_id, uploaded_by, filename, format[mps|lp], num_vars, num_constraints,
       problem_class[LP|MILP|QP], storage_uri, created_at)
solve_jobs(id, org_id, model_id, submitted_by, status[queued|running|optimal|infeasible|
           unbounded|time_limit|error|cancelled], engine_used[simplex|ipm|bnb|qp-activeset|
           qp-ipm], gpu_used[bool], options_json, objective_value, best_bound, gap,
           started_at, finished_at, wall_time_ms)
solve_logs(id, solve_job_id, ts, level, message)         -- streamed console log
solve_metrics(id, solve_job_id, ts, incumbent, best_bound, gap, node_count, iteration_count)
benchmark_runs(id, org_id, suite[miplib2017|netlib|qplib|mittelmann|custom], solver,
               instance_name, status, objective_value, wall_time_ms, gap, run_at)
notifications(id, user_id, type, payload_json, read_at, created_at)
usage_records(id, org_id, period_start, period_end, compute_seconds, gpu_seconds, job_count)
```

---

## 10. Web Console — Complete Page-by-Page UI & Content

> Scope note: the problem statement explicitly says a polished GUI is *not required* for the SIH deliverable — the core engine + API/CLI is the graded artifact. This console is the **optional value-add product layer** (a monitoring/benchmarking/job-submission front end over the REST API in §8.3), specified here in full because the brief asks for a complete, production-style solution. It is an original design, inspired only in *category* (job console for a solver) by how cloud optimization consoles work in general — not a copy of any specific vendor's screens, wording, or assets.

**Global navigation (present on every authenticated page), left sidebar:**
`Dashboard · Models · Solves · Benchmarks · API & Docs · Team · Usage & Billing · Settings`
Top bar: org switcher, environment badge (`Production` / `Sandbox`), search (`⌘K`), notifications bell, avatar menu (`Profile`, `API Keys`, `Sign out`).

### 10.1 Marketing / Landing Page (public, unauthenticated)

- **Hero section**
  - Headline: "A sovereign optimization engine, built from first principles."
  - Subhead: "HyperNova solves LP, MILP and QP problems at industrial scale — GPU-accelerated, fully inspectable, and free from foreign licensing constraints."
  - Primary CTA button: `Get API Access` → Sign-up page
  - Secondary CTA: `View Benchmarks` → Public benchmark page (§10.13)
  - Trust strip: "Built for India's refining, power and logistics sectors" with logos placeholder row (MRPL, generic PSU icons)
- **"Why HyperNova" — 3-column feature block**
  1. **Sovereign & Transparent** — "Every algorithm is documented from its mathematical foundation. No black-box binaries."
  2. **GPU-Accelerated Core** — "CUDA/HIP/SYCL backends accelerate interior-point and branch-and-bound at scale."
  3. **Industrial-Grade Robustness** — "Engineered for degenerate, ill-conditioned, large-sparse problems — not just textbook instances."
- **Problem classes strip:** four cards — `Linear Programming`, `Mixed-Integer Programming`, `Quadratic Programming`, `Coming soon: MIQP / NLP / MINLP`
- **Benchmarks teaser section:** small embedded chart "HyperNova vs. open-source baselines on MIPLIB 2017 (subset)" with `See full benchmark report →`
- **Use-case band:** Refinery Scheduling · Crude Blending · Production Planning · Power Dispatch · Logistics & Supply Chain — each a short 1-line card linking to a case-study page.
- **Footer:** Docs, API Reference, GitHub (architecture docs only, per licensing), Status page, Contact, "Made in India" badge.

### 10.2 Sign Up / Sign In

- **Sign Up fields:** Full name, Work email, Organization name, Password, Country (dropdown, default `India`), Sector (dropdown — see below), checkbox "I agree to the Terms of Service and Acceptable Use Policy."
  - **Sector dropdown options:** `Oil & Gas / Refining`, `Power & Energy`, `Manufacturing`, `Logistics & Supply Chain`, `Academic / Research`, `Government / PSU`, `Other`
- **Sign In fields:** Email, Password, `Forgot password?` link, `Continue with SSO` (enterprise tier only).
- Post-signup: email verification screen → onboarding wizard (§10.3).

### 10.3 Onboarding Wizard (first login only, 3 steps)

- **Step 1 — "What will you solve first?"** single-select cards: `Linear Programming (LP)`, `Mixed-Integer Programming (MILP)`, `Quadratic Programming (QP)`, `Not sure yet`
- **Step 2 — "How will you connect?"** single-select: `Upload a model file (MPS/LP)`, `Use the C++ API`, `Use the CLI`, `Use the REST API`
- **Step 3 — "Create your first API key"** — key label input (default `default-key`), scope dropdown (`Full access`, `Read-only`), `Generate Key` button, one-time key display with copy button and warning "This key will not be shown again."
- Finish → redirects to Dashboard.

### 10.4 Dashboard (home after login)

- **Header:** "Welcome back, {first_name}" + environment badge.
- **KPI row (4 stat cards):**
  1. Active Solves (live count, auto-refresh)
  2. Solves This Month
  3. Average Solve Time (rolling 30-day)
  4. Compute Hours Used vs. Plan Quota (progress bar)
- **"Recent Solves" table** (last 10): columns `Model`, `Status` (colored pill: Optimal=green, Infeasible=amber, Error=red, Running=blue pulsing), `Objective Value`, `Gap`, `Duration`, `Started`, row-click → Solve Detail page (§10.8).
- **"Recent Models" list** (last 5 uploaded): filename, problem class badge (`LP`/`MILP`/`QP`), var/constraint counts, `Solve` quick-action button.
- **Quick actions panel:** `Upload New Model`, `Submit a Solve`, `Run a Benchmark`, `View API Docs`.
- **System status widget:** small banner if solver cluster has degraded GPU capacity, e.g., "GPU queue currently at high load — jobs may fall back to CPU."

### 10.5 Models — List Page

- Table columns: `Name`, `Format` (MPS/LP), `Class` (LP/MILP/QP badge), `Variables`, `Constraints`, `Uploaded By`, `Uploaded`, actions (`Solve`, `Download`, `Delete`).
- Filter bar (dropdowns): `Class: All / LP / MILP / QP`, `Uploaded by: All / Me / Teammate`, `Date range`.
- Sort options dropdown: `Newest first`, `Oldest first`, `Largest (variables)`, `Smallest (variables)`.
- Primary button top-right: `+ Upload Model`.
- Empty state: illustration + "No models yet. Upload an MPS or LP file to get started." + `Upload Model` button.

### 10.6 Model Upload Modal / Page

- Drag-and-drop zone: "Drop your .mps or .lp file here, or browse."
- Post-upload auto-detected metadata panel (read-only, computed by parser): `Format detected`, `Problem class` (auto-classified LP/MILP/QP based on integer variables / quadratic terms present), `Variables`, `Constraints`, `Non-zeros`, `Bounds summary`.
- Optional fields: Model name (editable, defaults to filename), Description (textarea), Tags (multi-select free text).
- Validation panel: shows parser warnings (e.g., "3 duplicate row names auto-renamed", "RANGES section present and applied") before allowing save.
- Buttons: `Cancel`, `Save Model`, `Save & Solve Now`.

### 10.7 New Solve — Submission Page (wizard, 3 tabs)

**Tab 1 — Model & Engine**
- Model selector (dropdown/search, defaults to model just uploaded if navigated from there).
- Engine dropdown: `Auto-select (recommended)`, `Simplex (Primal)`, `Simplex (Dual)`, `Interior Point`, `Branch-and-Bound`, `Branch-and-Cut`, `QP Active-Set`, `QP Interior-Point` — options auto-filtered by detected problem class.
- Compute target dropdown: `CPU only`, `CPU + GPU (auto)`, `CPU + GPU (force)`.

**Tab 2 — Parameters**
- Time limit (seconds) — numeric input, default `3600`.
- MIP gap tolerance — numeric input, default `1e-4` (shown only for MILP).
- Thread count — numeric input with `Auto` toggle.
- Node selection strategy dropdown (MILP only): `Best-first`, `Depth-first`, `Best-estimate`, `Reliability branching (default)`.
- Branching rule dropdown (MILP only): `Most fractional`, `Pseudocost`, `Strong branching`, `Reliability (default)`.
- Presolve level dropdown: `Off`, `Conservative`, `Aggressive (default)`.
- Scaling method dropdown: `None`, `Geometric (default)`, `Curtis–Reid`.
- Advanced tolerances (collapsible section): Feasibility tolerance, Optimality tolerance, Pivot tolerance, Integrality tolerance — each numeric with sensible pre-filled defaults and an ⓘ tooltip explaining the parameter.

**Tab 3 — Review & Submit**
- Read-only summary of selected model + all chosen options.
- `Save as Preset` checkbox + preset name field (presets reusable in future submissions, listed in a `Preset: [dropdown]` field on Tab 1 for returning users).
- Primary button: `Submit Solve`. On click → redirect to Solve Detail page in `Queued` state.

### 10.8 Solve Detail / Live Monitor Page

- **Header:** model name, status pill (`Queued`, `Presolving`, `Running`, `Optimal`, `Infeasible`, `Unbounded`, `Time Limit Reached`, `Error`, `Cancelled`), elapsed time (live), `Cancel Solve` button (while running).
- **Live convergence chart:** dual-line chart — Incumbent Objective vs. Best Bound over time (updates via SSE), with shaded MIP gap band. For LP/QP: single convergence line (objective vs. iteration) plus residual sub-chart.
- **Live stats strip:** `Iterations` (simplex/IPM), `Nodes Explored` (MILP), `Nodes Remaining`, `Current Gap %`, `Cuts Added`, `Active Threads`, `GPU Utilization %` (if GPU engaged).
- **Streaming log panel** (monospace, auto-scroll, pausable): raw solver log lines, filterable by dropdown `All`, `Info`, `Warnings`, `Numerical diagnostics`.
- **Presolve Summary card** (populates once presolve completes): rows removed, columns removed, coefficients tightened, resulting reduced problem size, time taken.
- On completion → **Results panel** appears below:
  - Objective value, solve status, total wall time, gap achieved.
  - `Download Solution (.sol)`, `Download Solve Report (.json)`, `Download Log (.txt)` buttons.
  - Solution variable table (paginated, searchable): `Variable`, `Value`, `Reduced Cost` (LP) / `—` (MILP integer vars), `Bound Status`.
  - Constraint table: `Constraint`, `Activity`, `RHS`, `Slack`, `Dual Value` (LP/QP only).
  - `Re-run with different parameters` button → pre-fills Tab 2 of §10.7.

### 10.9 Solves — History / List Page

- Table: `Job ID`, `Model`, `Status`, `Objective`, `Gap`, `Engine`, `GPU`, `Duration`, `Submitted By`, `Started`.
- Filter dropdowns: `Status: All / Optimal / Infeasible / Unbounded / Time Limit / Error / Cancelled`, `Engine: All / Simplex / Interior Point / Branch-and-Cut / QP`, `Compute: All / CPU / GPU`.
- Bulk actions: `Export selected as CSV`, `Delete selected`.

### 10.10 Benchmarks — Suite Runner Page

- **"Run a benchmark suite" panel:**
  - Suite dropdown: `MIPLIB 2017`, `Netlib LP`, `Mittelmann LP/MILP set`, `QPLIB`, `Custom (from my models)`.
  - Compare-against dropdown (multi-select): `HiGHS`, `SCIP`, `GLPK`, `CBC`, `None (HyperNova only)` — labelled "Reference results are pre-computed and version-pinned for fair comparison; see methodology."
  - Instance subset dropdown: `Full suite`, `Easy subset`, `Medium subset`, `Hard/degenerate subset`, `Custom selection`.
  - Time limit per instance, numeric input, default `1800` seconds.
  - `Run Benchmark` primary button.
- **Results table** (per instance): `Instance`, `HyperNova Status`, `HyperNova Time`, `HyperNova Gap`, then one column pair per comparison solver (`Status`, `Time`), plus a computed `Speedup / Slowdown` column (color-coded).
- **Summary scorecard at top:** solved-to-optimality count, average time ratio vs. each comparison solver, shifted geometric mean (industry-standard metric for solver benchmarking), win/loss/tie count.
- **Export:** `Download Results CSV`, `Download Performance Profile (PDF)` — generates a performance-profile plot (standard solver-benchmarking visualization).

### 10.11 Public Benchmark Report Page (unauthenticated, marketing-linked)

- Static/refreshed-periodically summary: "HyperNova v{X} vs. Open-Source Baselines" with the same performance-profile chart, methodology disclosure box ("Hardware: …, Time limit: …, Solver versions: …, Date: …"), and a `Reproduce this benchmark →` CTA leading to sign-up.

### 10.12 API & Docs Page

- Left sub-nav: `Getting Started`, `Authentication`, `REST API Reference`, `C++ API Reference`, `CLI Reference`, `File Formats (MPS/LP)`, `SDKs (Python — coming soon)`, `Changelog`.
- **"Getting Started" content block (sample copy):**
  > "1. Create an API key from Settings → API Keys. 2. Upload a model via `POST /v1/models` or the console. 3. Submit a solve via `POST /v1/solves`. 4. Poll or stream status, then download your solution."
- Interactive API explorer (`Try it` panel) with endpoint dropdown selector, auto-filled auth header from the logged-in user's key, request/response JSON preview.
- Code snippet tabs: `cURL`, `C++`, `CLI`.

### 10.13 Team Page

- Members table: `Name`, `Email`, `Role` (dropdown per row: `Owner`, `Admin`, `Member`, `Viewer`), `Last Active`, remove action.
- `Invite Member` button → modal: email input, role dropdown (`Admin`, `Member`, `Viewer`), `Send Invite`.
- Pending invitations list with `Resend` / `Revoke` actions.

### 10.14 Usage & Billing Page

- Current plan card: plan name (dropdown to change plan: `Free / Sandbox`, `Team`, `Enterprise`), compute-hour quota, GPU-hour quota, seats included.
- Usage chart: compute seconds per day, current billing period, stacked by CPU/GPU.
- Invoices table: `Period`, `Amount`, `Status` (`Paid`/`Due`), `Download PDF`.
- `Upgrade Plan` / `Contact Sales` buttons (Enterprise tier).

### 10.15 Settings Page (tabs)

- **Profile:** name, email, password change, timezone dropdown.
- **API Keys:** list with label, created date, last used, scope badge, `Revoke` action; `+ Create New Key` button opens scope dropdown (`Full access`, `Read-only`, `Solve-submission only`).
- **Notifications:** toggle switches — "Email me when a solve completes", "Email me when a solve fails", "Weekly usage digest".
- **Default Solve Parameters:** the same fields as §10.7 Tab 2, saved as the org-wide default preset.
- **Danger Zone:** `Delete Organization` (Owner-only, requires typed confirmation).

### 10.16 Notifications Panel (slide-over, from bell icon)

- List of recent events: "Solve `refinery_blend_q3.mps` completed — Optimal (objective: 4,281,902.55)", "Benchmark run `MIPLIB 2017 subset` finished — 87/100 solved to optimality", each with relative timestamp and `Mark as read`.
- `Mark all as read` link, `Notification settings →` link to §10.15.

### 10.17 Error / Empty / Edge States (applies across pages)

- **No GPU available banner** (Solve Detail, when `GPU: force` selected but none available): "No GPU capacity currently available — falling back to CPU. [Learn more]"
- **Infeasible result state:** dedicated panel replacing the solution table: "This problem is infeasible." + `Run Infeasibility Analysis` button (identifies a minimal conflicting constraint subset — IIS-style report) with results listed as a constraint-name list.
- **Unbounded result state:** panel: "This problem is unbounded in the objective direction." + link to docs section on common causes (missing bounds, sign errors).
- **404 / permission-denied states:** standard illustration + "You don't have access to this resource, or it doesn't exist." + `Back to Dashboard` button.

---

## 11. Design System (tokens & components)

- **Type scale:** Display 32/40, H1 28/36, H2 22/28, H3 18/24, Body 15/22, Caption 13/18 — single sans-serif family (system UI stack) for engineering-tool legibility.
- **Color roles:** `--surface`, `--surface-raised`, `--border`, `--text-primary`, `--text-secondary`, `--accent` (brand indigo/violet, evoking "nova"), `--success` (optimal/green), `--warning` (amber — infeasible/time-limit), `--danger` (red — error), `--info` (blue — running/queued).
- **Status pill component:** consistent across Dashboard, Solve List, Solve Detail — fixed color mapping per status enum (§9 `solve_jobs.status`).
- **Data density:** console defaults to compact table density (engineering-tool convention); a density toggle (`Comfortable` / `Compact`) lives in user Settings → Profile.
- **Charts:** convergence charts and performance profiles use a consistent axis-label and legend style; log-scale toggle available on time-based charts (common need when comparing solvers across orders-of-magnitude runtimes).

---

## 12. Security, Licensing & Compliance

- Model files and solutions encrypted at rest; org-level data isolation enforced at the query layer (every table keyed by `org_id`).
- API keys stored as salted hashes only; shown once at creation.
- Role-based access control per §10.13 (`Owner/Admin/Member/Viewer`) enforced on every REST endpoint.
- Audit log (append-only) for: model deletion, API key creation/revocation, plan changes, role changes.
- Solver core itself ships under a permissive/sovereign license consistent with the "Indigenous" mandate of the problem statement — no GPL/AGPL dependency that would impose copyleft on downstream industrial users (relevant given zero reliance on CBC/HiGHS/SCIP, all of which carry EPL/MIT variants that would otherwise be moot since nothing is reused).

---

## 13. Benchmarking & Validation Plan

1. **Correctness first:** every solved instance independently re-verified by the Validation layer (§3.1) before being marked `OPTIMAL` — primal feasibility, dual feasibility, complementary slackness (LP/QP) or integrality + feasibility (MILP) all re-checked outside the solving code path.
2. **Netlib LP set:** full pass required for release sign-off (correct optimal objective within `1e-6` relative tolerance on all solvable instances; correct INFEASIBLE/UNBOUNDED classification on classic edge-case instances like `Adlittle`, `Kb2`, degenerate sets).
3. **MIPLIB 2017:** tiered targets — Phase 1 milestone: solve the "easy" tier within time limit; Phase 2: "hard" tier parity (gap %) tracked against HiGHS as the open-source reference point (comparison only, never as a code dependency).
4. **QPLIB:** convex QP subset for QP engine validation.
5. **Mittelmann benchmark set:** used for continuous performance regression tracking (CI job runs a fixed subset nightly, results plotted over time to catch performance regressions).
6. **Industrial case studies:** representative refinery scheduling / crude blending / production planning models (constructed from open literature, since MRPL's real data is confidential) used as the domain-relevance proof point required by the problem statement's "Expected Solution."
7. **Stress/robustness suite:** hand-built ill-conditioned, highly degenerate, and near-infeasible instances specifically to validate the numerical-robustness claim in the problem statement (not just "does it solve," but "does it fail honestly and gracefully when it can't").

---

## 14. Non-Functional Requirements

| Category | Requirement |
|---|---|
| Scale | Support problems with 10⁶+ variables/constraints on IPM path; 10⁵+ variables on B&B path within practical time |
| Reliability | No silent wrong-answers — all uncertain results reported as `SUBOPTIMAL`/`NUMERICAL_ERROR`, never mislabeled `OPTIMAL` |
| Reproducibility | Single-threaded runs bit-reproducible across identical hardware/OS/compiler |
| Portability | Core engine builds on Linux (primary target for industrial/HPC deployment) and Windows (secondary) |
| Console availability | 99.5% target uptime for the cloud console (console is a value-add layer, not the graded core deliverable) |
| Observability | Every solve emits a structured JSON solve report; console surfaces it without needing log-scraping |

---

## 15. Repository Structure

```
hypernova/
├── core/
│   ├── model/            # Problem representation, MPS/LP parsers
│   ├── presolve/
│   ├── numerical/        # sparse LA, factorizations, scaling, tolerances
│   ├── lp/                # simplex, interior point
│   ├── milp/               # branch-and-bound/cut, cuts, heuristics
│   ├── qp/                # active-set, interior point
│   ├── execution/         # thread pool, GPU backend abstraction
│   └── validation/        # solution verifier, solve report
├── gpu/
│   ├── cuda/
│   ├── hip/
│   └── sycl/
├── api/
│   ├── cpp/               # public C++ headers
│   └── cli/                # hypernova CLI
├── console/                # web console (frontend + REST backend, §8.3/§10)
├── benchmarks/
│   ├── miplib/ netlib/ qplib/ mittelmann/ industrial-cases/
├── tests/
│   ├── unit/ integration/ regression/
├── docs/
│   ├── theory/            # algorithm derivations
│   ├── architecture.md
│   └── api-reference.md
└── DEV_TASKS.md
```

---

## 16. Delivery Roadmap

**Phase 0 — Foundations (Weeks 1–3):** Problem/model layer, MPS/LP parsers, sparse LA primitives, tolerance manager, project scaffolding, CI.

**Phase 1 — LP core (Weeks 3–7):** Revised Simplex (primal + dual), basic presolve, Netlib LP validation pass, CLI `solve` command, solution verifier.

**Phase 2 — MILP core (Weeks 6–11):** Branch-and-bound shell, most-fractional + pseudocost branching, best-first node selection, Gomory/MIR cuts, rounding + diving heuristics, MIPLIB easy-tier validation.

**Phase 3 — IPM + QP (Weeks 9–13):** Interior-point LP engine, crossover, QP active-set + IPM extension, QPLIB validation.

**Phase 4 — Parallel & GPU (Weeks 12–16):** CPU thread pool + parallel B&B, GPU backend abstraction, CUDA SpMV/IPM kernels, CPU/GPU cost-model dispatch.

**Phase 5 — Hardening & Console (Weeks 15–20):** Reliability branching, cut pool management, RINS/feasibility pump, full Mittelmann benchmark run, industrial case studies, cloud console (§10) build-out, final packaging.

**Phase 6 — Validation & Demo Prep (Weeks 19–22):** Full benchmark comparison report, stress/robustness suite, documentation pass, SIH demo script and pitch deck.

---

## 17. Team Roles

- **Solver/Numerical Lead** — simplex, IPM, sparse LA, factorizations.
- **MILP/Combinatorics Lead** — B&B, cutting planes, heuristics, branching/node strategies.
- **Systems/GPU Engineer** — thread pool, CUDA/HIP kernels, backend abstraction, performance profiling.
- **Platform Engineer** — REST API, database, cloud console backend, auth/RBAC.
- **Frontend Engineer** — console UI (§10), design system (§11), live-monitor SSE integration.
- **QA/Validation Engineer** — benchmark harness, MIPLIB/Netlib/QPLIB runners, regression CI, solution verifier.
- **Domain/Docs Lead** — industrial case studies (refinery/blending/dispatch), theory documentation, SIH submission materials.

---

## 18. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| From-scratch numerical code underperforms mature solvers on hard MILPs | Fails benchmark parity requirement | Aggressive Phase 2/5 focus on branching/cut quality; honest reporting of gaps rather than overclaiming |
| GPU acceleration yields little benefit on typical sparse industrial LPs | Wasted engineering effort | Cost-model gating (§6.2) ensures GPU is only used where it measurably helps; CPU path remains fully competitive standalone |
| Ill-conditioned industrial matrices cause numerical failure | Core robustness claim undermined | Centralized tolerance manager + iterative refinement + honest `NUMERICAL_ERROR` status instead of silent wrong answers |
| Scope creep into full modeling-language territory | Delays core solver delivery | MPS/LP + C++ API is the hard boundary for v1; modeling DSL explicitly out of scope |
| Console becomes distraction from core engine (graded deliverable) | Jury perceives weak core | Console explicitly framed (§10 intro) as optional value-add; roadmap sequences it after core validation (Phase 5) |

---

## 19. Glossary

- **B&B / B&C:** Branch-and-Bound / Branch-and-Cut.
- **IPM:** Interior-Point Method.
- **MIR:** Mixed-Integer Rounding (cut family).
- **RINS:** Relaxation-Induced Neighborhood Search (MILP primal heuristic).
- **IIS:** Irreducible Infeasible Subsystem (infeasibility diagnosis).
- **KKT system:** Karush–Kuhn–Tucker conditions/linear system solved inside IPM.
- **Crossover:** recovering a basic (vertex) solution from an interior-point solution.
- **Presolve/Postsolve:** reducing the problem before solving, and mapping the reduced solution back to the original problem space.

---

*This document is the complete solution design for HyperNova against SIH Problem Statement 26119. It is an original architecture and an original UI specification; where existing commercial/open-source solvers are referenced, it is strictly for competitive framing and benchmark comparison, never as a source of copied code, text, or visual design.*
