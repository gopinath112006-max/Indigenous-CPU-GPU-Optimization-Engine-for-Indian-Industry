Continue from the existing HyperNova repository.

Goal:
Connect HyperNova's existing thread-pool/work-stealing infrastructure to actual MILP Branch-and-Bound execution.

Do not merely add threads to the code.
The solver must actually process independent B&B nodes concurrently.

Tasks:

1. Audit existing ThreadPool and WorkStealingPool.
2. Design thread-safe B&B node queues.
3. Allow workers to process independent nodes concurrently.
4. Protect:
   - incumbent solution
   - global bound
   - node queue
   - cut pool
   - termination state
5. Implement safe worker cancellation.
6. Implement global time/node/solution limits.
7. Ensure interrupt() works with multiple workers.
8. Handle worker exceptions/failures safely.
9. Make statistics thread-safe.
10. Implement deterministic mode if practical.
11. Add worker-count configuration.
12. Benchmark:
   - 1 worker
   - 2 workers
   - 4 workers
   - 8 workers
13. Measure:
   - wall-clock time
   - nodes/sec
   - total nodes
   - memory
   - scaling efficiency
14. Compare parallel vs serial execution.

Acceptance criteria:
- More than one worker genuinely processes B&B nodes.
- Correctness is unchanged.
- Verification passes.
- No data races.
- Interrupt and limits work correctly.
- Parallel speedup is demonstrated on suitable MILP problems.
- If a problem does not benefit from parallelism, that is documented rather than hidden.