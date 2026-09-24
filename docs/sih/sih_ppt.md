# SIH Presentation: HyperNova Solver

## Slide 1: Title Slide
*   **HyperNova:** Indigenous CPU/GPU Optimization Engine for Indian Industry
*   **Team Name/Details**
*   **Problem Statement:** Addressing the need for sovereign, high-performance optimization tech.

## Slide 2: The Core Challenge
*   **What is HyperNova?** A high-performance solver for Linear and Mixed-Integer Programming.
*   **The Difficulty:** Optimization algorithms (like the Simplex engine) are highly sensitive to numerical instability and degeneracy.
*   **Our Approach:** Custom, robust C++ architecture with parallel Branch-and-Bound and GPU acceleration.

## Slide 3: Engineering Rigor (The "Defect" Story)
*   **Adversarial Testing:** We continuously stress-test HyperNova against adversarial MILP instances.
*   **The Discovery:** A correctness defect in the Branch-and-Bound engine where dynamic variable bound shifts ($lb \neq 0$) caused infinite loops in the solver tree.
*   **The Constraint:** Fixing this without altering or destabilizing the core Simplex engine, which is highly optimized for the dense Netlib benchmarks.

## Slide 4: The Mathematical Solution
*   **The Invariance Theorem:** 
    > "The coordinate shift does not change the dual feasible region, so the associated dual multipliers and reduced-cost relationships are invariant under the translation. The primal and dual objective values differ by the corresponding constant shift, which is restored when reconstructing the original objective."
*   **Result:** We fixed the B&B node resolution algorithm to perform dynamic, mathematically-sound variable shifting without touching the low-level linear algebra routines.

## Slide 5: Code-to-Math Traceability
*   **Demonstrating Quality:** We map the exact mathematical formulas to our C++ implementation in `BranchAndBoundSolver::solve_node_lp`.
    *   $s_j = lb_j \rightarrow$ `lp_shifts[j] = lb`
    *   $x = x' + s \rightarrow$ `relax_primal[j] += lp_shifts[j]`
    *   $z = z' + c^T s \rightarrow$ `obj_offset`

## Slide 6: Targeted Regression Testing
*   Instead of just running it, we built a 10-case GTest regression suite specifically for bound transformations.
*   **Cases Covered:** Mixed integer variables, infeasible bounds, positive/negative bounds, maximization/minimization.
*   **Status:** 100% Pass Rate across the board. The solver is mathematically robust.

## Slide 7: Current Freeze Status
*   **Suite:** 22/22 CTest Regression tests passed.
*   **Netlib Benchmark:** Verified stability against 16 dense Netlib benchmark instances.
*   **Ready for Deployment:** The engine is stable, performant, and rigorously tested.
