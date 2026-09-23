# Solver Statuses

- **OPTIMAL**: The solver has satisfied its optimality criteria and established optimality within its configured numerical tolerances.
- **SUBOPTIMAL**: A feasible solution may exist even when the solver does not report OPTIMAL. HyperNova uses SUBOPTIMAL for a valid solution where optimality has not been established (e.g. time limit hit).
- **INFEASIBLE**: The model cannot be mathematically satisfied.
- **UNBOUNDED**: The objective can be improved infinitely.
- **TIME_LIMIT**: The operation was aborted due to SolverOptions.time_limit_seconds.
- **ITER_LIMIT**: The operation exceeded maximum internal iterations.
- **NUMERICAL_ERROR**: The matrix factorizations became too unstable to continue.
