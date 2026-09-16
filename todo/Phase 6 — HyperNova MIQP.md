Continue from the existing HyperNova repository.

Goal:
Implement genuine Mixed-Integer Quadratic Programming support.

Current known issue:
MIQP currently routes to NUMERICAL_ERROR.

Architecture:
Use Branch-and-Bound with continuous QP relaxations at nodes.

Tasks:

1. Extend solver routing to recognize MIQP.
2. Use the existing B&B infrastructure.
3. At each node, solve the continuous convex QP relaxation.
4. Apply node bounds and branching constraints.
5. Maintain incumbent integer-feasible solution.
6. Calculate node lower bounds.
7. Prune by bound.
8. Prune infeasible nodes.
9. Handle numerical failures safely.
10. Reuse QP information where mathematically safe.
11. Integrate existing cuts only where valid for MIQP.
12. Integrate appropriate heuristics.
13. Verify final integer solution independently.
14. Add MIQP regression tests.
15. Add small hand-verifiable MIQP models.
16. Add benchmark models where licensing permits.

Acceptance criteria:
- MIQP no longer automatically returns NUMERICAL_ERROR.
- Small MIQP models produce correct verified solutions.
- Incorrect or unsupported cases report meaningful statuses.
- B&B statistics are available.
- No existing LP/MILP/QP behavior regresses.