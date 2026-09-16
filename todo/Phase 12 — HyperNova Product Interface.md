Continue from the existing HyperNova repository.

Goal:
Polish HyperNova's public API, CLI and basic web console without distracting from the solver core.

CLI tasks:

1. Fix --out so it actually writes solution output.
2. Fix --report so it actually writes reports.
3. Make file-extension handling case-insensitive.
4. Remove leftover debug messages.
5. Replace hard-coded solver status messages with actual SolveStatus values.
6. Ensure errors are meaningful.
7. Add execution summary:
   - solver
   - variables
   - constraints
   - runtime
   - objective
   - verification
8. Add capability inspection.

API tasks:
Document:
- Problem
- ProblemBuilder
- Solver
- Solution
- SolveReport
- IIS
- Certificate
- Diagnostics

Console:
Build only a basic interface:
- upload model
- choose solver/auto
- solve
- show objective
- show status
- show runtime
- show verification
- show basic solver statistics

Do not build unnecessary authentication, billing, mobile apps or complex cloud infrastructure.

Acceptance criteria:
- CLI functions actually work.
- API documentation is usable.
- Console demonstrates the real solver rather than mocking results.