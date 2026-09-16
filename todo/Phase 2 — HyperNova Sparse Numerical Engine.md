Continue from the existing HyperNova repository.

Do not replace the solver architecture.
Do not use Eigen, SuiteSparse, CHOLMOD, MKL optimization solvers, or any existing sparse optimization library as the solver implementation.

Goal:
Turn HyperNova's numerical layer into a genuinely sparse and scalable numerical foundation.

Current context:
Sparse CSR/CSC structures already exist.
Sparse Cholesky/LDLT work has recently been improved using minimum-degree ordering and symbolic fill.
SparseLU still requires careful investigation.

Tasks:

1. Audit core/numerical completely.
2. Identify every place where sparse matrices are converted into dense matrices.
3. Separate:
   - symbolic analysis
   - ordering
   - permutation
   - numerical factorization
   - triangular solve
   - refinement
4. Validate minimum-degree ordering.
5. Validate elimination tree construction.
6. Validate symbolic fill computation.
7. Implement correct sparse Cholesky where applicable.
8. Implement correct sparse LDLT where applicable.
9. Add required permutation handling.
10. Add sparse triangular solves.
11. Avoid unnecessary dense O(n²) storage.
12. Investigate SparseLU and remove its dense-Gaussian-elimination bottleneck where practical.
13. Add numerical pivoting/stability safeguards where required.
14. Reuse factorizations when the solver can safely reuse them.
15. Preserve ToleranceConfig, scaling, condition estimation and iterative refinement.
16. Add regression tests for:
    - sparse matrices
    - fill-in
    - permutations
    - singular matrices
    - ill-conditioned matrices
    - numerical stability
17. Benchmark before vs after on the existing Netlib suite.
18. Measure:
    - runtime
    - peak memory
    - factorization time
    - solve time
    - residual
19. Investigate dfl001 specifically.

Acceptance criteria:
- Existing tests remain green.
- Sparse factorization is genuinely sparse rather than dense storage disguised as sparse.
- Numerical residuals remain within configured tolerances.
- Memory usage improves on large sparse cases.
- At least one previously difficult benchmark demonstrates measurable improvement.
- No unsupported claim is added to the README.

At the end, report:
- exact algorithms implemented
- complexity characteristics
- memory behavior
- benchmark improvements
- remaining limitations
- files modified
- tests added