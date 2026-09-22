# HyperNova — Next Engineering Phase: Full Requirement Gap Audit & Implementation

You are continuing development of **HyperNova**, our from-scratch C++20 GPU-ready mathematical optimization solver for the SIH problem statement:

**PS-26119 — Indigenous GPU-Accelerated Optimization Solver**

The project already contains substantial implementation across LP, MILP, QP, MIQP, presolve, parsing, validation, diagnostics, GPU backends, API, CLI, REST, web UI, testing, and documentation.

## IMPORTANT: DO NOT START BY REWRITING EXISTING CODE

First inspect the repository and determine the exact current state.

The objective of this phase is:

> **Compare the original problem statement and engineering requirements against the CURRENT repository implementation, identify every missing/partial/weak requirement, then implement the highest-value remaining work with evidence-based verification.**

Do not assume something is implemented merely because a class, function, documentation entry, stub, or test exists.

---

# 1. FIRST: FULL REPOSITORY AUDIT

Inspect:

- entire source tree
- CMake configuration
- solver engines
- model representation
- parsers
- presolve
- numerical utilities
- LP
- MILP
- QP
- MIQP
- convexity validation
- bounds handling
- cuts
- heuristics
- branching
- parallelism
- GPU abstraction
- CUDA/HIP/SYCL code
- API
- C API
- CLI
- REST server
- web UI
- diagnostics
- IIS
- quality reports
- benchmarks
- examples
- tests
- documentation
- build scripts
- CI-related configuration if present

Also inspect the latest commits and existing verification documents.

Do not modify anything during the initial audit.

---

# 2. BUILD A REQUIREMENT TRACEABILITY MATRIX

Create or update a document:

`docs/REQUIREMENT_TRACEABILITY.md`

For every important requirement, classify it as exactly one of:

- IMPLEMENTED
- PARTIALLY IMPLEMENTED
- STUB
- MISSING
- IMPLEMENTED BUT UNTESTED
- IMPLEMENTED BUT NOT INTEGRATED
- IMPLEMENTED BUT NOT DOCUMENTED
- IMPLEMENTED BUT NUMERICALLY UNSAFE
- IMPLEMENTED BUT PERFORMANCE-UNVERIFIED

For each requirement record:

1. Requirement
2. Current implementation
3. Relevant source files
4. Tests proving it
5. Known limitation
6. Evidence
7. Recommended action
8. Priority

Use:

- P0 = correctness/blocker
- P1 = major requirement gap
- P2 = important enhancement
- P3 = optional polish

Do NOT artificially mark features as complete.

---

# 3. RE-CHECK THE ORIGINAL PROBLEM STATEMENT

The target is not simply "make a mathematical solver."

HyperNova must address the actual SIH requirement for an:

> Indigenous GPU-Accelerated Optimization Solver

Therefore specifically verify:

### Solver capability

- LP
- MILP
- QP
- MIQP
- convexity handling
- feasibility detection
- unboundedness detection
- infeasibility detection
- optimality verification
- numerical stability
- variable bounds
- constraints
- objective handling

### Industrial optimization functionality

Check support for:

- continuous variables
- integer variables
- binary variables
- free variables
- lower/upper bounds
- equality constraints
- <= constraints
- >= constraints
- objective sense
- ranged constraints
- fixed variables
- semi-continuous/semi-integer features if claimed
- SOS if claimed
- quadratic objectives
- quadratic constraints if claimed

Do not claim support unless the complete solve path actually handles it.

---

# 4. DEEPLY AUDIT QP / MIQP

Pay special attention to QP and MIQP because this area previously had known limitations.

Verify:

- KKT formulation
- primal feasibility
- dual feasibility
- stationarity
- complementary slackness
- variable bounds
- inequality constraints
- equality constraints
- convexity requirements
- positive semidefinite Hessian handling
- numerical tolerances
- convergence criteria
- infeasibility
- unboundedness
- iteration limits

If QP IPM still does not correctly enforce variable bounds, DO NOT hide this.

Determine whether the correct engineering solution is:

- fixing the KKT system,
- adding bound multipliers,
- active-set treatment,
- reformulation,
- or another mathematically correct approach.

Implement the correct solution rather than adding a superficial workaround.

Then add regression tests specifically targeting the previous failure.

---

# 5. AUDIT CONVEXITY VALIDATION

The latest work added the HyperNova Convexity Scope implementation.

Now independently verify it.

Check:

- matrix dimensions
- symmetry
- numerical symmetry tolerance
- Hessian dimensions
- variable-index validity
- sparse representation
- positive semidefinite detection
- positive definite detection where required
- zero/near-zero eigenvalues
- indefinite matrices
- numerical scaling
- malformed models
- empty Hessians
- single-variable models
- large sparse Hessians

Ensure the convexity result is actually consumed by the solver dispatch and not merely calculated and ignored.

Add tests for every important edge case.

---

# 6. AUDIT LP SOLVERS

Verify both:

### Revised Simplex

- primal simplex
- dual simplex
- basis management
- reduced costs
- ratio test
- degeneracy
- cycling protection
- Bland/anti-cycling logic if claimed
- infeasibility
- unboundedness
- optimality
- numerical tolerances
- basis recovery

### Mehrotra IPM

- predictor/corrector
- barrier parameter
- residual calculation
- convergence
- primal feasibility
- dual feasibility
- complementarity
- step length
- numerical regularization
- termination
- bounds
- infeasibility/unboundedness behavior

Compare both engines on common models.

---

# 7. AUDIT MILP

Inspect the complete branch-and-bound / branch-and-cut pipeline.

Verify:

- LP relaxation
- branching
- node selection
- incumbent management
- bounds
- pruning
- integrality detection
- feasibility tolerance
- MIP gap
- time limit
- node limit if supported
- cut generation
- cut validity
- heuristic integration
- parallel execution
- deterministic behavior where expected

Inspect:

- Gomory
- MIR
- Knapsack-cover
- Clique
- Flow-cover

Do not count a cut generator as implemented merely because the class exists.

Create small mathematical instances where each mechanism can be verified.

---

# 8. AUDIT HEURISTICS

Verify actual integration and correctness of:

- rounding
- diving
- feasibility pump
- RINS

For each one determine:

- Is it actually invoked?
- Under what conditions?
- Does it improve the incumbent?
- Is feasibility validated?
- Can it corrupt solver state?
- Is it tested?

If not properly integrated, fix it.

---

# 9. AUDIT PRESOLVE

Verify every presolve transformation.

For each transformation prove:

- mathematical validity
- objective preservation
- feasibility preservation
- bound preservation
- solution reconstruction

Test:

- redundant constraints
- fixed variables
- singleton rows
- singleton columns
- bound tightening
- duplicate constraints
- dominated constraints
- empty rows
- empty columns
- variable elimination
- scaling

Critically test that the final solution is mapped correctly back to the ORIGINAL model.

---

# 10. PARSER COMPLIANCE

Audit MPS and LP parsing against the formats HyperNova claims to support.

MPS:

- ROWS
- COLUMNS
- RHS
- RANGES
- BOUNDS
- SOS
- QUADOBJ
- QCMATRIX
- OBJSENSE
- free format
- fixed format

LP format:

- objective
- constraints
- bounds
- binaries
- generals/integer variables
- quadratic terms
- sections
- comments
- whitespace variations

For every supported feature create a minimal fixture and solve it.

Do not advertise unsupported syntax as supported.

---

# 11. MODEL VALIDATION AND DIAGNOSTICS

Audit:

- malformed models
- NaN
- infinity
- duplicate indices
- invalid variable indices
- invalid constraint indices
- inconsistent dimensions
- invalid bounds
- contradictory bounds
- empty models
- zero-variable models
- zero-constraint models
- huge coefficients
- tiny coefficients
- poorly scaled models

Ensure diagnostics explain the actual failure.

---

# 12. SOLUTION QUALITY VERIFICATION

Create a single reliable post-solve validation layer.

It must independently calculate:

- primal feasibility
- constraint violation
- bound violation
- integrality violation
- objective value
- dual residual where applicable
- KKT residual where applicable
- MIP gap where applicable

The solver must not be able to report "OPTIMAL" without passing the appropriate validation criteria.

If this architecture already exists, audit it instead of duplicating it.

---

# 13. SOLVER DISPATCH

Verify automatic dispatch.

Current intended behavior includes:

- LP IPM for very large LPs where appropriate
- simplex for smaller LPs
- MILP → branch-and-bound / branch-and-cut
- QP → appropriate QP engine
- MIQP → branch-and-bound with QP relaxation
- convexity-aware routing

Ensure dispatch decisions are based on actual model properties rather than arbitrary heuristics.

Document the decision tree.

---

# 14. GPU ARCHITECTURE

This is one of the most important areas.

Determine exactly what is:

- CPU implemented
- GPU abstraction implemented
- CUDA stub
- PTX-related implementation
- HIP stub
- SYCL stub
- GPU-dispatch capable
- actually GPU executed
- benchmarked on GPU

DO NOT claim "GPU accelerated" merely because CUDA/HIP/SYCL interfaces exist.

Clearly distinguish:

### Level A
GPU architecture/API abstraction.

### Level B
GPU kernels implemented.

### Level C
Solver operation actually offloaded to GPU.

### Level D
End-to-end solver execution using GPU.

### Level E
Measured CPU-vs-GPU benchmark.

If actual GPU execution is currently unavailable in the environment, implement the strongest architecture that can be safely implemented and document the exact limitation.

Do not fake benchmark results.

---

# 15. PERFORMANCE BENCHMARKING

Audit existing benchmarks.

Verify reproducibility for:

- Netlib
- MIPLIB
- QPLIB
- Mittelmann
- MRPL

Measure where possible:

- solve time
- presolve time
- iterations
- nodes
- cuts
- memory
- objective
- feasibility
- optimality status

For parallel B&B verify the existing speedup claims.

Do not replace measurements with theoretical claims.

---

# 16. API / CLI / REST / WEB

Verify that the user-facing system actually works end-to-end.

CLI:

- solve
- iis
- diagnose
- quality
- capabilities
- interactive
- convert
- inspect
- benchmark

REST:

- health
- capabilities
- solve
- inspect

Verify:

- valid JSON
- malformed requests
- large models
- errors
- timeouts
- concurrent requests
- solver cancellation if supported
- result serialization

Web UI:

- model selection
- solve
- status
- result
- diagnostics
- presets
- error handling

Every UI feature must connect to a real backend capability.

---

# 17. C API

Audit the opaque-handle API.

Verify:

- creation/destruction
- memory ownership
- error handling
- model construction
- solve
- result retrieval
- thread safety expectations
- ABI considerations
- DLL exports
- invalid-handle behavior

Create a small external C smoke-test program if practical.

---

# 18. TESTING

Run the complete test suite first.

Record:

- total tests
- passing
- failing
- skipped
- execution time

Then add missing tests.

Target:

### Unit tests
Mathematical primitives.

### Integration tests
Model → presolve → solve → validate.

### Regression tests
Every previously discovered bug.

### Property tests
Small randomized LP/QP/MILP models where practical.

### Parser tests
Every claimed syntax feature.

### API tests
CLI + REST + C API.

### Numerical stress tests
Degenerate, ill-scaled and boundary cases.

### End-to-end tests
Real benchmark models.

Never delete or weaken an existing test merely to obtain a green build.

---

# 19. DOCUMENTATION HONESTY

Audit:

- README
- BASELINE.md
- FINAL_REQUIREMENT_VERIFICATION.md
- architecture docs
- solver documentation
- GPU documentation
- API documentation
- limitations

Every claim must correspond to actual tested behavior.

Use explicit terminology:

- Implemented
- Partial
- Experimental
- Stub
- Benchmark-only
- Tested
- Untested
- Known limitation

Do not use marketing language to hide engineering gaps.

---

# 20. PRIORITIZATION

After the audit, produce:

`docs/NEXT_PHASE_PLAN.md`

Rank remaining work using:

### P0 — Correctness
Anything that can produce an incorrect optimization result.

### P1 — Problem-statement compliance
Anything required to credibly satisfy PS-26119.

### P2 — Demonstration strength
Features that materially improve the SIH demonstration.

### P3 — Polish
Documentation, UI improvements, convenience features.

Do not spend time on P3 while P0/P1 issues remain.

---

# 21. IMPLEMENTATION RULES

When implementing fixes:

1. Preserve existing working functionality.
2. Do not perform unnecessary rewrites.
3. Do not introduce external commercial solver dependencies.
4. Do not use CBC, HiGHS, SCIP, GLPK, Gurobi, CPLEX, Xpress, or copied solver implementations.
5. HiGHS may be used only as a benchmark/reference where already permitted.
6. Keep C++20 compatibility.
7. Keep the project buildable with the current supported toolchains.
8. Preserve the existing public API unless a change is genuinely necessary.
9. Prefer mathematically correct implementations over quick hacks.
10. Add regression tests for every correctness fix.
11. Do not fabricate GPU execution.
12. Do not fabricate benchmark results.
13. Do not silently downgrade functionality.
14. Do not remove existing tests to make CI pass.

---

# 22. REQUIRED EXECUTION ORDER

Follow this exact order:

### Phase A — Inspect
No modifications.

### Phase B — Build baseline
Run the complete existing build/test suite.

### Phase C — Requirement mapping
Create/update `docs/REQUIREMENT_TRACEABILITY.md`.

### Phase D — Identify blockers
Find P0/P1 issues.

### Phase E — Fix correctness
Implement P0 fixes first.

### Phase F — Implement major missing requirements
Implement P1 items.

### Phase G — Regression testing
Add tests for every change.

### Phase H — Full verification
Rebuild everything and run all tests.

### Phase I — Documentation
Update all affected documentation.

### Phase J — Final engineering report
Create:

`docs/NEXT_PHASE_COMPLETION_REPORT.md`

---

# 23. COMPLETION REPORT FORMAT

The final report must contain:

## Executive Summary

## Repository State Before

## Requirements Audited

## Requirements Implemented

## Requirements Still Partial

## Requirements Still Missing

## Bugs Found

## Bugs Fixed

## Tests Added

## Tests Before vs After

## Benchmark Results

## GPU Status

Clearly distinguish:

- GPU-ready architecture
- GPU kernel implementation
- actual GPU execution
- measured acceleration

## Known Limitations

## Remaining P0/P1/P2/P3 Work

## Files Changed

## Git Commit

---

# 24. GIT SAFETY

Before committing:

- inspect `git status`
- inspect `git diff`
- ensure no secrets
- ensure no generated junk
- ensure no accidental deletion
- ensure tests pass

Make one clean commit for this phase.

Commit message:

`phase-next: requirement audit and correctness hardening`

Do NOT push to a remote unless explicitly instructed.

---

# 25. MOST IMPORTANT RULE

Do not optimize for the appearance of completeness.

Optimize for:

> **Mathematical correctness + requirement traceability + reproducible evidence + honest capability reporting.**

If you discover that an existing HyperNova feature is weaker than previously documented, **downgrade its status and fix it rather than hiding the limitation**.

At the end, give a concise final summary containing:

1. What you inspected
2. What was actually missing
3. What you implemented
4. What remains
5. Exact test count
6. Exact benchmark evidence
7. Exact GPU capability status
8. Commit hash

Then stop.

Do not begin another speculative feature phase after this.
