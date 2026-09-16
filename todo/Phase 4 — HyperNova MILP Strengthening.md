Continue from the existing HyperNova repository.

Goal:
Turn the current MILP implementation into a more complete and effective branch-and-cut solver.

Do not remove existing cuts or heuristics.
Do not use CBC, HiGHS, SCIP, GLPK, Gurobi, CPLEX or another optimization solver internally.

Tasks:

1. Audit Branch-and-Bound.
2. Implement real pseudocost updates after branching.
3. Use pseudocosts for future branching decisions.
4. Implement reliability branching correctly.
5. Verify strong branching behavior.
6. Connect the existing node-selection strategies to the actual B&B queue:
   - best-first
   - depth-first
   - best-estimate
   - hybrid
7. Ensure HYBRID is genuinely different from best-first.
8. Improve node-level presolve.
9. Improve incumbent management.
10. Audit symmetry detection and document its current scope.
11. Improve rounding heuristic.
12. Improve diving heuristic.
13. Improve feasibility pump where practical.
14. Improve RINS where practical.
15. Audit every cut family:
   - Gomory
   - MIR
   - knapsack-cover
   - clique
   - flow-cover
16. Ensure cuts are numerically safe.
17. Improve cut efficacy and aging management.
18. Add cut separation statistics.
19. Add B&B statistics:
   - nodes created
   - nodes processed
   - nodes pruned
   - incumbent improvements
   - bound progression
   - cuts generated
   - heuristic solutions
20. Add randomized MILP regression tests.
21. Test small models against independently calculated expected solutions.
22. Add MIPLIB benchmark support preparation.

Acceptance criteria:
- Pseudocosts are actually updated and used.
- Node selection options actually affect execution.
- Existing tests remain green.
- MILP objective verification remains independent.
- Performance is compared against the frozen baseline.
- No feature is claimed as complete unless it is actually used by the solver.