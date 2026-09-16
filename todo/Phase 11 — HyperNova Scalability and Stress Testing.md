Continue from the existing HyperNova repository.

Goal:
Measure and improve HyperNova's scalability toward industrial-scale optimization problems.

Tasks:

1. Create generated sparse LP models of increasing size.
2. Create generated sparse MILP models.
3. Test:
   - 1,000 variables
   - 10,000
   - 100,000
   - larger sizes where hardware permits
4. Measure:
   - memory
   - parse time
   - presolve time
   - factorization time
   - solve time
   - verification time
5. Identify bottlenecks.
6. Ensure sparse storage remains sparse.
7. Test highly degenerate models.
8. Test ill-conditioned models.
9. Test weak LP relaxations.
10. Test difficult MILP formulations.
11. Test time-limit behavior.
12. Test interrupt behavior.
13. Test memory exhaustion behavior.
14. Never claim million-variable capability unless it has actually been demonstrated.

Acceptance criteria:
- Scaling curves are generated.
- Major memory bottlenecks are identified.
- At least one large sparse model is solved successfully.
- Solver failure modes are graceful.
- Results are documented honestly.