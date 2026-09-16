# HyperNova Roadmap

## A. Core model support

**Current level**
You support LP, MILP, and QP at the basic model level.

**Add next**
- Continuous variables.
- Integer variables.
- Binary variables.
- Semi-continuous variables.
- Semi-integer variables.
- Multiple objective functions.
- Objective priorities.
- Objective weights.
- Multi-scenario models.
- Model cloning.
- Model modification without rebuilding everything.
- Incremental constraint addition.
- Incremental variable addition.
- Model serialization and checkpointing.

**Advanced modeling constraints**
Add support for:
- SOS1 constraints.
- SOS2 constraints.
- Indicator constraints.
- Logical constraints.
- max constraints.
- min constraints.
- Absolute-value constraints.
- Piecewise-linear constraints.
- Quadratic constraints.
- Second-order cone constraints.
- Rotated second-order cone constraints.
- General nonlinear constraints, later.

CPLEX supports QP, MIQP, QCP, MIQCP, second-order cone constraints, rotated second-order cone constraints, and general convex quadratic constraints.

## B. LP solver improvements

**Current**
- Revised simplex.
- Mehrotra interior-point method.

**Add immediately**
- Dual simplex.
- Primal simplex improvements.
- Devex pricing.
- Steepest-edge pricing.
- Anti-cycling rules.
- Phase-I and Phase-II improvements.
- Crash basis construction.
- Sparse basis updates.
- Periodic basis refactorization.
- Advanced pivot selection.

**Interior-point improvements**
Add:
- Sparse Cholesky.
- Sparse LDLᵀ.
- Regularization.
- Predictor-corrector improvements.
- Gondzio corrections.
- Iterative refinement.
- Inexact Newton solves.
- Multiple correction steps.
- Primal/dual scaling.
- Better infeasibility detection.
- Barrier warm start.
- Barrier crossover.

**Why this matters**
Your current LU implementation allows the solver to work, but commercial-grade barrier solving generally needs highly optimized sparse symmetric factorization and numerical stabilization.

Your most important numerical tasks are:
- Sparse Cholesky, sparse LDLᵀ, dual simplex, iterative refinement, and crossover.

## C. MILP branch-and-cut improvements

**Current**
- Branch-and-bound.
- Basic branch selection.
- Gomory/MIR cuts.
- RINS and heuristics.
- Node selection.

**Add stronger cut families**
Implement:
- Gomory fractional cuts.
- Gomory mixed-integer cuts.
- MIR cuts.
- Cover cuts.
- Flow-cover cuts.
- Clique cuts.
- Implied-bound cuts.
- GUB-cover cuts.
- Zero-half cuts.
- Lift-and-project cuts.
- Conflict cuts.
- Mixing cuts.
- Network cuts.
- Modulo cuts.
- User cuts.
- Lazy constraints.

**Add cut management**
Your cut manager should track:
- Cut violation.
- Cut efficacy.
- Cut age.
- Cut activity.
- Numerical quality.
- Root-node usefulness.
- Bound improvement.
- Duplicate cuts.
- Cut density.
- Memory usage.

Implement:
- Global cut pool.
- Local cut pool.
- Cut ageing.
- Cut deletion.
- Cut separation frequency.
- Cut ranking.
- Parallel cut generation.
- Cut violation filtering.

Xpress documentation specifically describes presolve and control over presolve operations, while its modern releases also include parallel heuristics and CUDA device selection.

## D. Branching and node selection

**Current**
- Most-fractional branching.
- Pseudocost branching.
- Basic node selection.

**Add**
- Strong branching.
- Reliability branching.
- Hybrid pseudocost/strong branching.
- Inference branching.
- Conflict-driven branching.
- Branching priorities.
- Pseudocost initialization.
- Pseudocost reliability thresholds.
- Best-bound search.
- Depth-first search.
- Best-estimate search.
- Hybrid node selection.
- Restart strategies.
- Tree recycling.
- Local branching.
- Symmetry-aware branching.

**Most important addition**
Implement reliability branching first.
It gives you much of the benefit of strong branching without applying expensive look-ahead to every variable.

## E. MILP heuristics

**Current**
- RINS.
- Basic heuristics.

**Add**
- Feasibility pump.
- Rounding heuristic.
- Diving heuristic.
- Fractional diving.
- Objective diving.
- Coefficient diving.
- Local branching.
- Large-neighborhood search.
- Relaxation-induced neighborhood search improvements.
- Mutation and repair.
- Solution polishing.
- Sub-MIP heuristic.
- Improvement heuristic.
- Root-level parallel heuristics.
- Node-level parallel heuristics.

CPLEX includes solution polishing and local branching options for improving incumbent integer solutions.

## F. Presolve and propagation

**Current**
- Bound tightening.
- Row/column removal.
- Singleton elimination.

**Add**
- Probing.
- Variable fixing.
- Dominated-column detection.
- Dominated-row detection.
- Parallel-row detection.
- Aggregation.
- Substitution.
- Coefficient strengthening.
- Implied-bound propagation.
- Dual reductions.
- Primal reductions.
- Constraint propagation.
- Conflict propagation.
- Network detection.
- Clique detection.
- SOS detection.
- Symmetry detection.
- Symmetry-breaking constraints.
- Multiple presolve passes.
- Node-level presolve.
- Root-level presolve.
- Presolve undo/reconstruction mapping.

Presolve is not only a one-time preprocessing step. Mature solvers repeatedly apply reductions after new bounds and cuts are generated. Xpress describes presolve as a process that removes redundant rows and columns and can be controlled through multiple presolve operations.

## G. GPU acceleration

**Current**
- IComputeBackend interface.
- CPU fallback.
- CUDA/HIP/SYCL directories are empty.

**Add GPU kernels**
*Basic kernels*
- CSR sparse matrix-vector multiplication.
- Transposed CSR multiplication.
- Vector addition.
- Vector scaling.
- Dot product.
- Parallel reduction.
- Norm calculation.
- Residual calculation.
- Bound checking.
- Feasibility checking.
- Cut violation checking.

*GPU memory system*
- Device memory pool.
- Pinned host memory.
- Unified-memory option.
- Asynchronous memory transfers.
- CUDA streams.
- Event synchronization.
- Multi-GPU device selection.
- GPU memory monitoring.

*GPU algorithms*
- GPU PDHG/PDLP.
- GPU first-order LP.
- GPU barrier linear algebra.
- GPU root LP relaxation.
- GPU batch node relaxation.
- GPU cut separation.
- GPU scenario solving.
- GPU heuristic evaluation.

Current GPU optimization approaches commonly focus on sparse operations and first-order LP methods such as PDLP/PDHG.

**Recommended division**
```
CPU:
- Branching decisions.
- Tree management.
- Node queue.
- Complex heuristics.
- Model transformations.
- Global incumbent management.

GPU:
- Sparse matrix operations.
- Root LP calculations.
- Large-scale first-order LP.
- Residual calculation.
- Cut violation checking.
- Batch processing.
- Independent scenario solving.
```

Do not initially try to run the entire branch-and-bound tree on the GPU. The tree is irregular and dynamic.

## H. Parallel and distributed solving

**Add three separate modes**

1. **Parallel single-model MILP**
One problem is divided dynamically into branch-and-bound nodes:
```
One MILP
   ↓
Dynamic branch-and-bound tree
   ↓
CPU/GPU workers process different nodes
```

2. **Concurrent optimization**
The entire model is solved using different strategies:
```
Worker 1 → Dual simplex
Worker 2 → Barrier
Worker 3 → PDHG
Worker 4 → Aggressive cuts
```
Gurobi supports concurrent LP and MIP, where multiple independent solves run in parallel.

3. **Batch/scenario solving**
Many independent MRPL scenarios are solved simultaneously:
```
Scenario 1
Scenario 2
Scenario 3
...
Scenario 30
```

**Add execution controls**
- Dynamic task scheduling.
- Work stealing.
- Shared incumbent.
- Global bound synchronization.
- Thread-safe cut pool.
- Thread-safe node pool.
- Deterministic mode.
- Opportunistic mode.
- Parallel logging.
- Cancellation.
- Timeout control.
- Checkpointing.
- Failure recovery.
- Multi-GPU scheduling.
- Distributed node processing.

CPLEX supports deterministic and opportunistic parallel modes; deterministic mode aims for repeatable execution, while opportunistic mode prioritizes speed with less synchronization.

## I. Numerical reliability

This is one of the most important areas for becoming a serious solver.

Add:
- Numerical condition estimation.
- Matrix condition warnings.
- Kappa-style diagnostics.
- Primal residual.
- Dual residual.
- Complementarity residual.
- Integrality residual.
- Scaling diagnostics.
- Singular-basis detection.
- Ill-conditioning detection.
- Iterative refinement.
- Feasibility restoration.
- Numerical trouble recovery.
- Automatic algorithm switching.
- Reliable status classification.

Useful output:
```
Numerical status:
- Matrix condition: warning
- Primal residual: 2.1e-8
- Dual residual: 4.5e-9
- Integrality violation: 0
- Solution reliability: high
```

CPLEX provides numerical stability assessment for MIP models through MIP Kappa-style diagnostics.

## J. Validation and certificates

**Current**
- Feasibility checks.
- Complementarity checks.
- JSON reports.

**Add**
- IIS computation.
- Infeasibility certificate.
- Farkas certificate.
- Unbounded ray.
- Dual certificate.
- Feasibility relaxation.
- Constraint violation explanation.
- Variable-bound conflict explanation.
- Optimality certificate.
- Independent verifier executable.
- Cross-check using a second algorithm.
- Exact/rational verification for small models.
- Reproducible solution hash.

Your solver should not only say:

```
Model is infeasible
```

It should say:

```
The following constraints form an irreducible infeasible subsystem:
- Production minimum
- Tank capacity
- Feed availability
```

This is an excellent area for HyperNova innovation.

## K. User API and ecosystem

**Current**
- C++ API.
- CLI.

**Add**
- Python bindings.
- C API.
- Java API.
- .NET API.
- MATLAB API.
- Rust API, later.
- NumPy integration.
- SciPy sparse integration.
- Pyomo interface.
- JuMP interface.
- AMPL interface.
- GAMS interface.
- REST API.
- gRPC API.

**Python API features**
Add interfaces similar to:
```python
model = hypernova.Model()
x = model.add_var(name="x", lb=0, ub=100)
model.add_constraint(x <= 50)
model.set_objective(x, maximize=True)
model.optimize()
```

**Advanced API features**
- Callbacks.
- Lazy constraints.
- User cuts.
- MIP starts.
- Warm starts.
- Basis starts.
- Parameter management.
- Solution pool.
- Multiple objectives.
- Tuning.
- Progress monitoring.
- Interrupt handling.
- Checkpoint/resume.

## L. Performance and benchmarking

Your benchmark framework exists, but the real datasets are missing.

**Add:**
- Netlib LP instances.
- MIPLIB instances.
- QPLIB instances.
- Mittelmann instances.
- Industrial test models.
- MRPL synthetic models.
- MRPL anonymized models.
- Large sparse models.
- Ill-conditioned models.
- Degenerate models.
- Infeasible models.
- Unbounded models.
- Highly symmetric models.

**Record:**
- Runtime.
- Objective value.
- Optimality gap.
- Number of nodes.
- Number of cuts.
- Presolve reduction.
- Memory usage.
- GPU utilization.
- CPU utilization.
- Energy consumption.
- Numerical warnings.
- Reproducibility.
- Timeout status.

**Compare HyperNova with:**
- Gurobi.
- CPLEX.
- Xpress.
- HiGHS.
- SCIP.
- Open-source baseline solvers.

Do not claim equality before you have these results.

## M. Web console

**Current**
- Design specification exists.
- Console implementation is empty.

**Add pages/features**
- Model upload.
- Interactive model builder.
- Solver selection.
- CPU/GPU selection.
- Parameter editor.
- Solve monitor.
- Progress chart.
- Branch-and-bound tree.
- Integrality-gap chart.
- Presolve report.
- Cut statistics.
- Heuristic statistics.
- Numerical diagnostics.
- IIS visualizer.
- Solution comparison.
- Scenario comparison.
- Export/report page.

**MRPL-specific dashboard**
Show:
- Production plan.
- Crude blend.
- Inventory.
- Tank utilization.
- Product yield.
- Transportation.
- Energy consumption.
- Constraint violations.
- Scenario comparison.
- Cost and profit impact.

## 3. Correct order for implementation

**Stage 1: Trustworthy solver**
Add first:
- Regression test suite.
- Benchmark data.
- Sparse Cholesky.
- Sparse LDLᵀ.
- Dual simplex.
- Crossover.
- Iterative refinement.
- IIS.
- Infeasibility/unbounded certificates.
- Numerical diagnostics.

**Stage 2: Strong MILP**
Then add:
- Reliability branching.
- Strong branching.
- Cover cuts.
- Clique cuts.
- Flow-cover cuts.
- Conflict analysis.
- Node presolve.
- Symmetry detection.
- Feasibility pump.
- Diving heuristics.
- Cut pool management.
- Deterministic parallel MIP.

**Stage 3: Ecosystem**
Then add:
- Python bindings.
- Callbacks.
- Lazy constraints.
- User cuts.
- Warm starts.
- Solution pool.
- Feasibility relaxation.
- Parameter tuner.
- Pyomo/JuMP support.
- REST/gRPC API.

**Stage 4: GPU and distributed execution**
Then add:
- CUDA sparse kernels.
- GPU PDHG.
- GPU root relaxation.
- GPU cut checking.
- GPU batch scenarios.
- Hybrid CPU-GPU MILP.
- Multi-GPU support.
- HIP backend.
- SYCL backend.
- Distributed MIP.

**Stage 5: Product differentiation**
Finally add:
- Web console.
- Explainable optimization.
- MRPL templates.
- Energy-aware solving.
- On-premise cluster deployment.
- Audit logs.
- Model/version management.
- Solver-as-a-service API.
- Tamil/English documentation.
- Secure enterprise deployment.

## 4. Final innovation statement

Use this wording:

> HyperNova is a sovereign, license-free mathematical optimization platform for LP, MILP, convex QP, and MIQP. It provides a complete CPU-based parse–presolve–solve–validate pipeline and is designed for hybrid CPU-GPU and distributed execution. The platform combines adaptive branch-and-cut, GPU-accelerated sparse numerical computation, explainable infeasibility analysis, deterministic parallel solving, energy-aware optimization, and secure on-premise deployment for Indian industrial applications such as refinery planning, blending, scheduling, inventory, and logistics.

## Most important next additions

If you want only the exact top priorities, implement these first:

- Real GPU kernels.
- Sparse Cholesky and LDLᵀ.
- Dual simplex.
- Crossover.
- Reliability branching.
- Strong branching.
- More cut families.
- Cut pool management.
- Node presolve.
- Conflict analysis.
- IIS and infeasibility certificates.
- Benchmark datasets.
- Regression tests.
- Python bindings.
- Callbacks and warm starts.
- Hybrid CPU-GPU MILP.
- MRPL-specific templates.
- Explainable web console.
- Deterministic and opportunistic parallel modes.
- Distributed solving.

This roadmap takes HyperNova from a functional CPU solver foundation toward a production-grade sovereign optimization platform comparable in architecture to Gurobi, CPLEX, and Xpress, while giving it additional differentiation through GPU portability, explainability, security, and MRPL-specific intelligence.