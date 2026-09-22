# HyperNova Numerical Robustness & Tolerances

## 1. Core Tolerances
HyperNova implements strict, documented numerical thresholds localized in core/numerical/tolerance.hpp.
- easibility_tol = 1e-6: The primal violation threshold before a constraint is considered broken.
- optimality_tol = 1e-6: The dual residual threshold for a basic solution to be declared optimal.
- integrality_tol = 1e-5: The maximum allowable distance from a whole number for a variable to be deemed integer-feasible.
- pivot_tol = 1e-9: The near-zero cutoff for LU factorization stability.
- zero_tol = 1e-12: Machine-epsilon cutoff for numeric rounding and empty constraint deletion.

## 2. Pathological & Edge Cases Verified
HyperNova has been stressed against:
- **Degenerate Bases:** Bland's Anti-Cycling mechanisms correctly break infinite loops.
- **Nearly Singular Matrices:** Rank-deficiency is caught safely in 
umerical::SparseLU.
- **Duplicate/Redundant Constraints:** Safely eliminated in presolve.cpp.
- **Large & Tiny Coefficients:** Handled safely; the algorithm scales arrays automatically.

## 3. Stability Proof
- **No False Infeasibility:** Verified by randomized search testing and reference MPS solutions.
- **No False Optimality:** The solver requires independent KKT and primal-feasibility checks to confirm optimality.
- **Safe Cut Separation:** The Mixed-Integer Gomory separator explicitly scales tableau rows to eliminate floating-point truncation corruption.
