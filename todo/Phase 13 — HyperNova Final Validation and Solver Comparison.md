Perform a final independent validation of HyperNova.

Goal:
Determine objectively how HyperNova performs against at least one established optimization solver.

Do not alter HyperNova merely to make benchmark results look better.

For each benchmark:

1. Run HyperNova.
2. Run the comparison solver.
3. Record:
   - status
   - objective
   - runtime
   - memory where available
   - optimality gap where applicable
   - verification
4. Use identical or equivalent model input.
5. Separate:
   - optimal
   - feasible
   - time limit
   - numerical failure
   - unsupported
6. Investigate objective mismatches.
7. Verify HyperNova solutions independently.
8. Generate comparison tables.
9. Generate performance summaries.
10. Identify workloads where HyperNova performs well.
11. Identify workloads where HyperNova is weaker.
12. Do not hide unfavorable results.

Final report must contain:
- benchmark methodology
- hardware
- compiler
- solver versions
- tolerances
- time limits
- results
- limitations
- conclusions

Acceptance criteria:
The report provides a scientifically defensible comparison rather than marketing claims.