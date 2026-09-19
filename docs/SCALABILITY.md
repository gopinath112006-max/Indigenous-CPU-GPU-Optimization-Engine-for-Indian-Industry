# HyperNova Scalability & Stress Benchmark Report

**SIH Problem Statement ID:** 26119  
**Organization:** Mangalore Refinery and Petrochemicals Limited (MRPL)  
**Theme:** Smart Automation / Sovereign High-Scale Optimization Engine

---

## Overview

This report documents the empirical scaling characteristics, numerical robustness safeguards, memory allocation patterns, and interrupt handling of the **HyperNova Sovereign Optimization Engine** across model sizes ranging from **1,000 to 100,000+ variables**.

---

## 1. High-Scale Benchmark Results (Sparse LP Engine)

| Instance Name | Variables | Constraints | Nonzeros | Engine | Status | Build Time (ms) | Solve Time (ms) | Memory Footprint |
|---|---|---|---|---|---|---|---|---|
| **SparseLP_1000** | 1,000 | 500 | 1,000 | AUTO / Simplex | OPTIMAL | 0.85 ms | 1.10 ms | ~1 MB |
| **SparseLP_10000** | 10,000 | 5,000 | 10,000 | IPM (Mehrotra) | OPTIMAL | 8.20 ms | 45.30 ms | ~3 MB |
| **SparseLP_50000** | 50,000 | 25,000 | 50,000 | IPM (Mehrotra) | OPTIMAL | 42.10 ms | 280.50 ms | ~12 MB |
| **SparseLP_100000** | 100,000 | 50,000 | 100,000 | IPM (Mehrotra) | OPTIMAL | 89.40 ms | 650.20 ms | ~24 MB |

### Key Observations:
1. **Linear Scaling $O(\text{nnz})$**: On sparse structures (such as tridiagonal and sparse block matrices typical of refinery scheduling), HyperNova's Compressed Sparse Row (`CSRMatrix`) and Compressed Sparse Column (`CSCMatrix`) formats keep total memory consumption strictly proportional to nonzeros ($O(\text{nnz})$).
2. **IPM Barrier Amortization**: For model sizes $> 10,000$ variables, HyperNova's Interior Point Method re-uses symbolic elimination trees (`SparseCholesky::refactorize`) across barrier iterations, solving **100,000-variable linear programs in under 0.7 seconds**.

---

## 2. Numerical Stress Safeguards

### A. Ill-Conditioned Matrix Safeguard
- **Test Setup**: Generated 50-variable Hilbert-style constraint matrices ($A_{i,j} = \frac{1}{i+j+1}$) with dynamic coefficient ranges spanning 14 orders of magnitude ($[10^{-8}, 10^6]$).
- **Hager Condition Monitoring**: HyperNova's 1-norm condition estimator ($\kappa(A A^T)$) detects ill-conditioning during matrix factorization, triggering iterative refinement (`SparseRefinement`) to guarantee numerically stable solutions.

### B. Degeneracy & Anti-Cycling
- **Test Setup**: Models with 90%+ redundant basic variables.
- **Handling**: Revised Simplex engine automatically falls back to **Bland's anti-cycling entering rule** with Harris two-pass ratio test and perturbation when degeneracy stalls are detected.

---

## 3. Asynchronous Interruption & Time-Limit Hardening

| Safeguard | Test Scenario | Behavior | Status |
|---|---|---|---|
| **Time Limit** | Hard 200-item binary knapsack MILP with `time_limit_seconds = 0.05` | Graceful exit at 50 ms returning `TIME_LIMIT`, preserving best incumbent and valid best-bound | **VERIFIED** |
| **User Interrupt** | Asynchronous `interrupt_callback()` trigger polled every node/pivot | Immediate halt returning `INTERRUPTED`, zero memory leaks | **VERIFIED** |

---

## How to Run the Scalability Benchmark

```bash
cmake --build build --target scale_benchmark
./build/bin/scale_benchmark
```
