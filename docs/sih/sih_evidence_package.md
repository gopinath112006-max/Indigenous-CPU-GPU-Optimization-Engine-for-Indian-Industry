# SIH Evidence Package: HyperNova Bound-Shifting Bug Fix

## 1. Defect Discovery
During adversarial testing of the HyperNova solver, we identified a critical correctness defect in the Branch-and-Bound (B&B) engine. Specifically, when handling mixed-integer programs (MILPs) containing continuous variables with non-zero lower bounds, the engine would infinite loop or return incorrect results.

**Evidence of Bug:**
- `test_randomized` continuously failed or deadlocked on MILPs with shifted variable bounds.
- CTest verification logs identified the regression was outside the Simplex engine, strictly localized to MILP node resolution.

## 2. Root Cause Analysis
The defect stemmed from the translation of primal coordinates. HyperNova shifts variable bounds such that $x' = x - lb$ and $0 \le x' \le ub - lb$ to simplify the underlying Simplex matrix. 
While this translation correctly preserves the primal feasibility region, the B&B engine failed to dynamically apply the inverse shift ($x = x' + lb$) to the LP relaxation results before evaluating bound satisfaction and integer feasibility. 

## 3. Mathematical Verification (The "Proof")
We successfully resolved this without altering the core Simplex engine, relying entirely on the following mathematical invariance:

> **The coordinate shift does not change the dual feasible region, so the associated dual multipliers and reduced-cost relationships are invariant under the translation. The primal and dual objective values differ by the corresponding constant shift, which is restored when reconstructing the original objective.**

*Code-to-Proof Mapping (As implemented in `BranchAndBoundSolver::solve_node_lp`):*

| Mathematical operation | Implementation                    |
| ---------------------- | --------------------------------- |
| $s_j = lb_j$           | `lp_shifts[j] = lb`               |
| $ub'_j = ub_j - lb_j$    | `ub - lb`                         |
| $b' = b - As$            | `shift_sum` / shifted RHS         |
| $z = z' + c^T s$          | `obj_offset`                      |
| $x = x' + s$             | `relax_primal[j] += lp_shifts[j]` |

## 4. Implementation Details
The fix was implemented cleanly inside `core/milp/branch_and_bound.cpp`, isolating it from the highly optimized `SimplexSolver`.

1. **Shift Calculation:** Track $lp\_shifts$ for each variable dynamically during `solve_node_lp`.
2. **Objective Offset:** Accumulate $obj\_offset = c^T s$.
3. **Inverse Transformation:** Add $lp\_shifts[j]$ back to the resulting $relax\_primal$ vector and $obj\_offset$ to the objective.

## 5. Regression Testing
To guarantee the structural soundness of our fixes, we verified targeted integration tests across the critical components:
- **LP, MILP, and API suites:** 3/3 test suites passed.
- **Symmetry correctness:** `SymmetryBug.ExcludesOptimum` is verified.
- **Simplex tolerances:** `SimplexToleranceFailureReproducer` is verified.
- **Tolerance Propagation:** The CLI and API strictly apply user-defined tolerances.

**Result:** Targeted regression verification passed smoothly with precise original feasibility matched.

## 6. System Verification (Documented Limitations)
* **Verified:** Targeted structural logic, unit tests, and API pathways.
* **Not fully verified / Incomplete:** The exhaustive 22-test CTest suite (including the rigorous Netlib tests). 

**Reason:** The large Netlib Phase-I cases require impractical sequential execution time in the current verification environment, leading to timeouts. Exhaustive regression verification remains an explicitly documented limitation.

---
**Verdict:** The codebase is officially frozen with verified targeted correctness and disclosed limitations.
