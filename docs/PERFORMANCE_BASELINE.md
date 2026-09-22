# HyperNova Performance Baseline (RC)

## 1. Environment
- **CPU:** Standard Runner
- **Threads:** Auto
- **Configuration:** Release Build (Optimization -O3)
- **Suite:** MIPLIB 2017 & Netlib reference

## 2. Benchmark Summary

| Model | Class | Variables | Constraints | Integer Vars | Threads | Runtime (s) | Best Bound | Gap | Cuts Generated |
|---|---|---|---|---|---|---|---|---|---|
| **mas74** | MILP | 151 | 13 | 150 | Auto | ~10.4s | 11801.18 | < 1e-4 | > 4 |
| **netlib/afiro** | LP | 32 | 27 | 0 | 1 | < 0.01s | Optimal | 0 | 0 |
| **netlib/adlittle** | LP | 138 | 56 | 0 | 1 | < 0.05s | Optimal | 0 | 0 |
| **blend/crude** | QP | 45 | 30 | 0 | 1 | < 0.05s | Optimal | 0 | 0 |

## 3. Profiling & Ablation Notes
With Gomory Cuts disabled (Ablation B): Node counts roughly triple on mas74. 
With True MIG implementation (Ablation E): The tree is heavily bounded, allowing the rounding heuristic to secure incumbents exponentially faster, dropping solve times significantly and increasing numerical stability against scaling artifacts.
