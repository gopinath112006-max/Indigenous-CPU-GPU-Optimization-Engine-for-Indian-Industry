You are working on the existing HyperNova C++20 mathematical optimization solver for SIH/MRPL Problem Statement 26119.

DO NOT rewrite the project.
DO NOT replace working implementations.
DO NOT introduce an existing optimization solver library.

First inspect the entire repository and existing docs/BASELINE.md.

Goal:
Create a reproducible HyperNova baseline and correctness freeze.

Tasks:

1. Build the complete project from a clean build directory.
2. Run every existing test target.
3. Record:
   - build configuration
   - compiler
   - C++ standard
   - test target count
   - GTest case count
   - pass/fail count
4. Run the complete current Netlib benchmark suite.
5. Record for every instance:
   - solver selected
   - status
   - objective value
   - reference objective if available
   - runtime
   - peak memory if available
   - verification result
6. Run representative:
   - LP
   - MILP
   - diagonal QP
   - cross-term QP
   - infeasible model
   - unbounded model
7. Verify IIS and Farkas/recession certificates independently.
8. Record all known limitations without hiding failures.
9. Update docs/BASELINE.md.

Acceptance criteria:
- Clean build succeeds.
- All currently expected tests pass.
- Benchmark results are reproducible.
- No existing functionality is removed.
- Every unsupported or incomplete capability is explicitly documented.

At the end, provide:
A. Files changed
B. Tests executed
C. Benchmark results
D. New failures discovered
E. Remaining technical limitations
F. Recommended next phase