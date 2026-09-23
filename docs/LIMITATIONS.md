# Known Limitations

- **QP IPM Variable Bounds:** The QP interior-point method may struggle to enforce variable bounds directly in complex non-convex relaxations and may return ITER_LIMIT. Fall back to Active-Set or B&B.
- **Hardware-Specific Speedups:** GPU performance depends heavily on PCIe transfer overhead. Small problems will solve faster on the CPU backend.
