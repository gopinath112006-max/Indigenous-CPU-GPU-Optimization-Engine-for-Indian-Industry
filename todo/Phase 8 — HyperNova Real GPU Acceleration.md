Continue from the existing HyperNova repository.

Goal:
Implement measurable GPU acceleration for selected numerical operations in HyperNova.

IMPORTANT:
Do not claim GPU acceleration before actual GPU kernels execute.
Do not move the entire solver to the GPU blindly.
First profile the CPU implementation.

Tasks:

1. Profile HyperNova on representative LP/MILP/QP workloads.
2. Identify the most expensive numerical kernels.
3. Determine which operations are GPU-suitable.
4. Start with one high-value operation, such as sparse matrix-vector multiplication or another measured bottleneck.
5. Implement a real CUDA backend.
6. Keep CPU fallback fully functional.
7. Design a backend interface so future HIP/SYCL support remains possible.
8. Minimize CPU-GPU memory transfers.
9. Implement device memory management.
10. Add correctness tests comparing GPU and CPU results.
11. Add numerical tolerance checks.
12. Benchmark:
    - CPU computation
    - GPU computation
    - CPU + transfer
    - GPU end-to-end
13. Only retain GPU execution where it produces measurable benefit.
14. Integrate the GPU path into the solver rather than leaving it as a standalone demo.
15. Expose execution information in SolveReport.

Acceptance criteria:
- Actual GPU kernels execute.
- CPU fallback works.
- GPU and CPU results agree within numerical tolerances.
- At least one real solver workload benefits measurably from GPU acceleration.
- Benchmark results are documented.
- README clearly states exactly what is GPU-accelerated.
- No fake/stub GPU claim remains.