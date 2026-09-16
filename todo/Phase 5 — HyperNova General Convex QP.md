Continue from the existing HyperNova repository.

Goal:
Correctly support general convex Quadratic Programming rather than only diagonal or toy quadratic cases.

Current known issue:
The current QP implementation has limitations around off-diagonal Hessian terms and uses dense KKT systems.

Tasks:

1. Audit Problem quadratic-term representation.
2. Ensure Q is represented correctly as a symmetric Hessian/objective matrix.
3. Correctly handle diagonal and off-diagonal terms.
4. Ensure x^T Q x conventions are consistent everywhere.
5. Verify gradient calculations.
6. Verify Hessian calculations.
7. Verify KKT construction.
8. Correct Active-Set QP implementation.
9. Correct QP Interior-Point implementation.
10. Ensure convexity checks or appropriate solver safeguards exist.
11. Integrate sparse numerical routines where practical.
12. Add general QP examples containing:
    - diagonal terms
    - cross terms
    - multiple cross terms
    - equality constraints
    - inequality constraints
    - bounds
13. Add independent objective and constraint verification.
14. Expand QP benchmark support.
15. Do not claim general QP support until cross-term cases are reliably solved.

Acceptance criteria:
- General convex QPs with off-diagonal terms are solved correctly.
- Objective values are independently verified.
- Existing diagonal QP tests continue to pass.
- QP numerical failures are correctly reported.
- Dense KKT is documented as a limitation until sparse QP is implemented.