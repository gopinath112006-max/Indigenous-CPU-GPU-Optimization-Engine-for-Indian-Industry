# HyperNova Regression Matrix

| Subsystem | Feature / Edge Case | Test Coverage | Expected Result | Status |
|---|---|---|---|---|
| **LP** | Revised Simplex (Primal) | 	est_lp.cpp, 	est_integration.cpp | Correct objective, status | PASS |
| **LP** | Revised Simplex (Dual) | 	est_lp.cpp | Dual feasibility, fallback to Primal | PASS |
| **LP** | Mehrotra IPM | 	est_lp.cpp | Fast convergence, interior point solution | PASS |
| **LP** | Infeasible LP | 	est_integration.cpp | INFEASIBLE status detected accurately | PASS |
| **LP** | Unbounded LP | 	est_integration.cpp | UNBOUNDED status detected accurately | PASS |
| **LP** | Degenerate / Cycling | 	est_lp.cpp | Bland's rule or pivot limits resolve cycling | PASS |
| **MILP** | Branch and Bound (B&B) | 	est_milp.cpp | Optimal integer solution | PASS |
| **MILP** | Parallel B&B | 	est_parallel_bnb.cpp | Identical objective as sequential, speedup | PASS |
| **MILP** | Mixed-Integer Gomory Cuts | 	est_milp.cpp | Active cut generation, fractional reduction | PASS |
| **MILP** | Flow-Cover / MIR / Clique | 	est_milp.cpp | Cuts strengthen bounds before branching | PASS |
| **MILP** | Heuristics (Rounding/Diving) | 	est_milp.cpp | Early incumbent discovery | PASS |
| **QP** | Convex Quadratic | 	est_qp.cpp | KKT satisfied, objective correct | PASS |
| **QP** | Inequality & Equality Const. | 	est_qp.cpp, 	est_integration.cpp | Bound multipliers accurate | PASS |
| **QP** | Indefinite Hessian | 	est_qp.cpp | Rejected or diagonal-regularized | PASS |
| **MIQP** | Integer QP | 	est_qp.cpp | Node relaxations evaluate correctly | PASS |
| **Parsers** | MPS (Fixed & Free) | 	est_roundtrip.cpp | Loads seamlessly, constraints map accurately | PASS |
| **Parsers** | LP Format | 	est_roundtrip.cpp | Algebraic parsing and sectioning valid | PASS |
| **API** | C++ API | 	est_api.cpp | Programmatic building successful | PASS |
| **API** | C API | 	est_api.cpp | Stable ABI and memory management | PASS |
| **API** | REST Server | 	est_rest_system.cpp | JSON deserialization, asynchronous solving | PASS |
| **Diag** | IIS / Diagnostics | 	est_validation.cpp | Detects conflicts, certifiable bounds | PASS |
| **Regress**| Float-Truncation Presolve Bug | mas74 regression suite | Presolve maps coefficients safely, passes mas74 | PASS |
| **Regress**| ABI / State Corruption Bug | Integration Edge Cases | Clean tear-down, map original vars cleanly | PASS |

*All components above form the authoritative release regression suite.*
