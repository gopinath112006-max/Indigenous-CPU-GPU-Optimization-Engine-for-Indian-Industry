Continue from the existing HyperNova repository.

Goal:
Build a reproducible benchmark suite covering LP, MILP, QP and industrial optimization.

Organize:

benchmarks/
  lp/
  milp/
  qp/
  industrial/

Include legally usable/public benchmark instances from:

- Netlib
- MIPLIB
- Mittelmann
- QPLIB
- open industrial case studies where appropriate

Tasks:

1. Keep the existing 21 Netlib baseline.
2. Add a representative MIPLIB subset.
3. Add a representative QPLIB subset.
4. Add suitable Mittelmann instances.
5. Add HyperNova industrial models.
6. Create one automated benchmark command.
7. Produce CSV and JSON output.
8. Record:
   - instance
   - problem type
   - variables
   - constraints
   - nonzeros
   - objective
   - status
   - runtime
   - memory if available
   - nodes for MILP
   - verification status
9. Calculate useful aggregate metrics.
10. Track regressions against the frozen baseline.
11. Separate:
   - solved optimally
   - feasible but not proven optimal
   - time limit
   - numerical failure
   - unsupported
12. Never classify timeout as failure of mathematical correctness without qualification.

Acceptance criteria:
- Benchmark execution is reproducible.
- Results are machine-readable.
- Existing Netlib results remain available.
- MIPLIB/QPLIB/Mittelmann coverage is real rather than placeholder directories.
- Benchmark reports accurately reflect limitations.