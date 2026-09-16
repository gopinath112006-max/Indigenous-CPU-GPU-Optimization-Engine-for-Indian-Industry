Continue from the existing HyperNova repository after the sparse numerical engine work.

Goal:
Strengthen LP solving performance and numerical reliability without changing the public architecture unnecessarily.

Tasks:

1. Audit Revised Simplex completely.
2. Implement real basis-update mechanisms instead of refactorizing the entire basis after every pivot wherever practical.
3. Preserve periodic full refactorization for numerical stability.
4. Implement the currently declared but unused pricing strategies:
   - Devex
   - steepest-edge
5. Implement Harris ratio testing.
6. Improve degeneracy handling.
7. Preserve Bland fallback and anti-cycling safeguards.
8. Improve dual-simplex eligibility and behavior.
9. Audit warm basis support.
10. Improve IPM factorization reuse.
11. Avoid unnecessary repeated factorization of identical normal-equation matrices.
12. Improve IPM numerical stability.
13. Implement a genuine simplex crossover from an interior-point solution if practical.
14. Do not describe a simple re-solve as crossover.
15. Add LP regression tests for:
   - degeneracy
   - cycling
   - ill-conditioned models
   - warm starts
   - dual feasibility
   - primal feasibility
16. Re-run all 21 Netlib instances.
17. Compare against the Phase 1 baseline.

Acceptance criteria:
- Existing LP results do not regress.
- Netlib success count improves or difficult cases show measurable runtime/memory improvement.
- Pricing options actually change solver behavior.
- Basis updates reduce unnecessary factorization work.
- Numerical verification remains mandatory.

Document:
- algorithm used
- solver selection logic
- tolerances
- benchmark results
- known limitations