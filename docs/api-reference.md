# HyperNova C++ API Reference

Version 0.1.0. The canonical public entry point is the single header
`<hypernova/api.hpp>`, which re-exports the model, LP and validation layers used
below. All symbols live in the `hypernova` namespace.

This document describes the classes named in the Phase 12 product-interface
scope: `Problem`, `ProblemBuilder`, `Solver`, `Solution`, `SolveReport`, `IIS`,
`Certificate`, and `Diagnostics`.

---

## 1. Problem classes and model representation

### `enum class EngineType`
`AUTO` (default), `PRIMAL_SIMPLEX`, `DUAL_SIMPLEX`, `INTERIOR_POINT`,
`BRANCH_AND_BOUND`, `BRANCH_AND_CUT`, `QP_ACTIVE_SET`, `QP_INTERIOR_POINT`.

- LP: `AUTO` selects interior point for >10 000 variables, otherwise simplex.
- MILP: `AUTO` / `BRANCH_AND_BOUND` → branch-and-bound; `BRANCH_AND_CUT`
  additionally enables Gomory, MIR, knapsack and clique cuts.
- QP: `AUTO` → active-set; `QP_INTERIOR_POINT` selects the interior-point engine.
- MIQP: routed to B&B with convex QP node relaxations (`QP_INTERIOR_POINT`
  selects IPM-QP node relaxations).

### `enum class NodeSelectionStrategy`
`BEST_FIRST`, `DEPTH_FIRST`, `BEST_ESTIMATE`, `HYBRID` (default).

### `enum class BranchingStrategy`
`MOST_FRACTIONAL`, `PSEUDOCOST`, `STRONG_BRANCHING`, `RELIABILITY` (default).

### `enum class PresolveLevel`, `enum class ScalingMethod`
Presolve: `OFF`, `CONSERVATIVE`, `AGGRESSIVE` (default). Scaling: `NONE`,
`GEOMETRIC` (default), `CURTIS_REID`.

### `enum class ComputeTarget`
`CPU_ONLY`, `CPU_GPU_AUTO` (default), `CPU_GPU_FORCE`.

### `enum class model::VarType`
`CONTINUOUS`, `INTEGER`, `BINARY` (plus `SEMI_CONTINUOUS`, `SEMI_INTEGER`);
`model::ConstraintSense`: `LE`, `GE`, `EQ`; `model::ObjectiveSense`:
`MINIMIZE`, `MAXIMIZE`.

### `struct model::Problem`
The canonical in-memory problem:

```cpp
struct Problem {
    std::string name;
    ObjectiveSense obj_sense = ObjectiveSense::MINIMIZE;
    double obj_offset = 0.0;
    std::vector<Variable> variables;        // name, type, lb, ub, obj coeff
    std::vector<Constraint> constraints;    // name, sense, rhs
    std::vector<SOS> sos_constraints;       // SOS1/SOS2 (parsed; not yet enforced)
    SparseMatrix constraint_matrix;         // CSR
    std::vector<QuadTerm> quadratic_terms;  // (row, col, coeff)

    std::size_t num_continuous_vars() const;
    std::size_t num_integer_vars() const;   // INTEGER + BINARY
    std::size_t num_binary_vars() const;
    bool is_lp() const;       // no integer vars, no quadratic terms
    bool is_milp() const;     // integer vars, no quadratic terms
    bool is_qp() const;       // quadratic terms, no integer vars
    bool is_miqp() const;     // integer vars and quadratic terms
    std::vector<double> get_lb() const;  get_ub();  get_obj();  get_rhs();  get_senses();
};
```

Quadratic-term convention: stored coefficient `q` for term `(i, j)` contributes
`q * x_i * x_j` when `i != j` and `0.5 * q * x_i^2` when `i == j`. The MPS
`QSECTION`/`QUADOBJ` and LP `Quadratic` sections are both read.

### `class model::ProblemBuilder`
Programmatic model construction:

```cpp
std::size_t add_variable(double lb, double ub, VarType type, const std::string& name = "");
std::size_t add_constraint(const std::vector<std::pair<std::size_t, double>>& coeffs,
                           ConstraintSense sense, double rhs, const std::string& name = "");
void set_objective(const std::vector<std::pair<std::size_t, double>>& coeffs,
                   ObjectiveSense sense = MINIMIZE);
void add_quadratic_term(std::size_t row, std::size_t col, double coeff);
void add_sos1(const std::vector<std::size_t>& vars, const std::vector<double>& weights,
              const std::string& name = "");
void add_sos2(...);                 // same signature
Problem build();                    // finalizes CSR + sorts/deduplicates indices
```

### `class Problem` (the public `hypernova::Problem`)
`Problem : public model::Problem` adds file I/O and convenience builders:

```cpp
static Problem from_mps(const std::string& path);   // free + fixed format
static Problem from_lp(const std::string& path);    // CPLEX-style LP grammar
void write_mps(const std::string& path) const;
void write_lp(const std::string& path) const;
std::size_t add_variable(...);                      // forwards to ProblemBuilder
std::size_t add_constraint(...);
void set_objective(...);
void add_quadratic_term(std::size_t row, std::size_t col, double coeff);
```

To build from scratch, prefer `model::ProblemBuilder` and convert via
`Problem{std::move(builder.build())}` (Problem inherits the move constructor).

---

## 2. `struct SolverOptions`

```cpp
struct SolverOptions {
    double time_limit_seconds = 3600.0;
    double mip_gap_tolerance = 1e-4;
    int thread_count = 0;                 // 0 = auto; >= 2 enables parallel B&B
    EngineType engine = EngineType::AUTO;
    NodeSelectionStrategy node_selection = HYBRID;
    BranchingStrategy branching = RELIABILITY;
    PresolveLevel presolve = AGGRESSIVE;
    ScalingMethod scaling = GEOMETRIC;
    ComputeTarget compute_target = CPU_GPU_AUTO;
    bool use_gpu = true;                  // currently inert; see note below
    bool heuristic_feasibility_pump = false;
    bool heuristic_rins = false;
    int rins_frequency = 10;
    numerical::ToleranceConfig tolerances;   // industrial defaults
    lp::PricingStrategy simplex_pricing = DEVEX;   // DANTZIG | DEVEX | STEEPEST_EDGE
    lp::RatioTest simplex_ratio_test = STANDARD;   // STANDARD | HARRIS_TWO_PASS
    bool simplex_bland_rule = true;        // Bland anti-cycling entering rule
    std::size_t simplex_steepest_edge_shortlist = 16;
};
```

> **GPU note (v0.1.0):** `use_gpu` / `compute_target` are configuration-only.
> The CUDA/HIP/SYCL backends are not yet implemented; the solver always uses the
> CPU backend. See `capabilities` in the CLI for the live backend list.

---

## 3. `class Solver`

```cpp
class Solver {
    explicit Solver(const SolverOptions& options = SolverOptions());
    Solution solve(const model::Problem& problem);
    Solution warm_solve(const model::Problem& problem, const Basis& start_basis);

    validation::IISResult           compute_iis(const model::Problem& problem);
    validation::CertificateResult   diagnose(const model::Problem& problem);
    validation::NumericalDiagnostics diagnose_numerical(const model::Problem& problem);

    void set_options(const SolverOptions& options);
    const SolverOptions& options() const;

    void interrupt();          // cooperative, polled by every solver loop
    bool interrupted() const;
};
```

`solve` runs the full pipeline: scaling → presolve → engine dispatch (by problem
class + engine option) → postsolve → independent solution verification. A status
of `OPTIMAL` is only returned after the validation layer re-checks primal/dual
feasibility and complementarity; failures surface as `SUBOPTIMAL`,
`NUMERICAL_ERROR`, `ITER_LIMIT`, `TIME_LIMIT` or `INTERRUPTED` — never as a
false `OPTIMAL`. On `INFEASIBLE`/`UNBOUNDED`, the API re-solves without presolve
to confirm the verdict.

### `struct Solution`
```cpp
struct Solution {
    model::ProblemStatus status;     // see enum below
    double objective_value;
    double best_bound;
    double gap;                      // (best_bound - objective) normalized
    std::vector<double> primal;      // one entry per variable
    std::vector<double> dual;        // one entry per constraint (LP/QP)
    std::vector<double> reduced_costs;
    std::vector<int> basis_status;   // basic/nonbasic markers
    std::size_t simplex_iterations;
    std::size_t ipm_iterations;
    std::size_t bb_nodes;
    double solve_time_ms;
    bool is_optimal() const;
    bool is_feasible() const;        // OPTIMAL or SUBOPTIMAL
};
```

### `enum class model::ProblemStatus`
`UNKNOWN`, `OPTIMAL`, `INFEASIBLE`, `UNBOUNDED`, `SUBOPTIMAL`, `TIME_LIMIT`,
`ITER_LIMIT`, `NUMERICAL_ERROR`, `INTERRUPTED`.

---

## 4. `struct validation::SolveReport`

Machine-readable record of a solve (JSON via `to_json()` / `from_json()`):

```cpp
struct SolveReport {
    std::string problem_name;
    std::string solver_version = "0.1.0";
    std::chrono::system_clock::time_point timestamp;
    model::ProblemStatus status;
    double objective_value, best_bound, gap;
    std::size_t num_variables, num_constraints, num_integer_vars, num_nonzeros;
    std::size_t presolve_passes, presolve_removed_rows, presolve_removed_cols;
    double presolve_time_ms;
    std::size_t simplex_iterations, ipm_iterations, bb_nodes, cuts_added;
    double solve_time_ms, total_time_ms;
    double primal_infeasibility, dual_infeasibility, complementarity,
           integrality_violation;
    std::string engine_used;
    bool gpu_used;
    int threads_used;

    nlohmann::json to_json() const;
    static SolveReport from_json(const nlohmann::json& j);
    std::string to_string() const;
};
```

The CLI `solve --report <file>` produces an equivalent JSON report
(`docs`-side: `status`, counts, iterations, timing, wall time, verification
residuals). The `Solver` itself does not currently emit a `SolveReport` object
from `solve()`; construct one from the `Solution` if you need the full struct.

---

## 5. IIS (irreducible infeasible subsystem)

```cpp
struct validation::IISBound {
    std::size_t variable;
    bool upper;              // true = upper bound violated, false = lower
};

struct validation::IISResult {
    bool found = false;
    std::vector<std::size_t> constraint_indices;
    std::vector<IISBound> bounds;
    std::size_t lp_solves = 0;
    double time_ms = 0.0;
    std::string message;
    std::string to_string(const model::Problem& p) const;  // names rows/bounds
};
```

Obtained via `Solver::compute_iis(problem)`. Uses a deletion filter
(Chinneck) over the rows and finite variable bounds of the LP relaxation;
`lp_solves` reports how many feasibility LPs were needed. Returns
`found == false` when the model is feasible.

---

## 6. Certificates

```cpp
struct validation::FarkasCertificate {
    bool valid = false;
    std::string message;
    std::vector<double> multipliers;   // one per constraint, sign-consistent
    double farkas_value = 0.0;         // negative when the model is infeasible
    double violation = 0.0;
};

struct validation::UnboundedRay {
    bool valid = false;
    std::string message;
    std::vector<double> direction;     // one entry per variable
    double objective_direction = 0.0;  // strictly improving
    double violation = 0.0;
};

struct validation::CertificateResult {
    model::ProblemStatus status;
    FarkasCertificate farkas;
    UnboundedRay ray;
    std::string message;
};
```

Obtained via `Solver::diagnose(problem)`. Both certificates are **re-verified
against the model** by the validation layer and reported only when valid — never
claimed on hand-waving. For a feasible model, `status` is the plain solve
verdict without a certificate.

---

## 7. Diagnostics

```cpp
struct validation::NumericalDiagnostics {
    model::ProblemStatus status;
    double objective_value;
    std::size_t num_variables, num_constraints, num_nonzeros;
    double max_coefficient, min_nonzero_coefficient, dynamic_range;
    double condition_estimate;     // 1-norm cond(A*A^T), Hager; 0 if singular
    double primal_infeasibility, dual_infeasibility, complementarity,
           integrality_violation;
    std::string quality;           // "good" | "warn" | "poor"
    std::vector<std::string> warnings;
    bool matrix_rank_deficient = false;
    std::string to_string() const;
    std::string to_json() const;
};
```

Obtained via `Solver::diagnose_numerical(problem)`, which solves a probe LP and
assembles the report. The CLI `quality` command prints it (use `--json` for the
JSON form).

---

## 8. Example

Compile against HyperNova (`find_package(HyperNova)` or add the `hypernova_api`
target): 

```cpp
#include <hypernova/api.hpp>
#include <iostream>

using namespace hypernova;

int main() {
    model::ProblemBuilder b("my_model");
    auto x = b.add_variable(0.0, 10.0, model::VarType::INTEGER, "x");
    auto y = b.add_variable(0.0, 5.0,  model::VarType::CONTINUOUS, "y");
    b.add_constraint({{x, 1.0}, {y, 2.0}}, model::ConstraintSense::LE, 12.0, "cap");
    b.set_objective({{x, 3.0}, {y, 2.0}}, model::ObjectiveSense::MAXIMIZE);
    Problem problem{std::move(b.build())};

    SolverOptions opts;
    opts.engine = EngineType::AUTO;
    opts.thread_count = 0;
    Solver solver(opts);

    Solution sol = solver.solve(problem);
    std::cout << (sol.is_optimal() ? "OPTIMAL" : "not optimal")
              << " objective=" << sol.objective_value << "\n";
    std::cout << "x=" << sol.primal[x] << " y=" << sol.primal[y] << "\n";
    return sol.is_optimal() ? 0 : 1;
}
```

---

## 9. CLI equivalence

| API | CLI |
|---|---|
| `Problem::from_mps` / `from_lp` | `hypernova solve model.mps` |
| `SolverOptions.*` | `--engine --time-limit --gap --threads --pricing --ratio-test --nobland --node-selection --branching --gpu` |
| `Solution` | printed by `solve`; `--out` writes the JSON solution file; `--report` the JSON report |
| `compute_iis` | `hypernova iis model.mps` |
| `diagnose` | `hypernova diagnose model.mps` |
| `diagnose_numerical` | `hypernova quality model.mps [--json]` |
| backend list | `hypernova capabilities` |