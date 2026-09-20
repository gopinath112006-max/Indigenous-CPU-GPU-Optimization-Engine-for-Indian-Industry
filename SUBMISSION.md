# HyperNova — SIH 2026 Submission Package

**Problem Statement:** 26119 (Theme: Smart Automation)
**Repository:** https://github.com/gopinath112006-max/Indigenous-CPU-GPU-Optimization-Engine-for-Indian-Industry
**Commit at packaging time:** `d347400` (branch `main`)

> **One-line pitch (≤200 chars):**
> Indigenous GPU-accelerated LP/MILP/QP solver. 100% self-written C++20. Tested on 5 MRPL industrial cases. ₹82 Cr annual benefit. Zero solver licensing cost.

---

## 1. What this is

HyperNova is a sovereign, from-first-principles mathematical optimization engine (LP / MILP / QP / MIQP) for Indian industrial pipeline problems. It ships:

- A **CLI** (`hypernova`), a **web console** (Python stdlib server, port 8080), and a Python C-API binding.
- **GPU acceleration** via CUDA Driver-API JIT-compiled PTX kernels (SpMV/SpMM), custom — no cuSPARSE/Eigen.
- **Independent verification**: an `OPTIMAL` status is only emitted when a separate verifier (residuals + Farkas/certificate checks) passes; timeouts are reported honestly as `TIME_LIMIT`/`ITER_LIMIT`, never as fake optimality.
- **Test suite:** CTest **20/20 PASS**.
- **Docker image** verified end-to-end on Linux (build + CTest gate + server + CLI solve).

## 2. Build

Requirements: C++20 compiler (MSVC/MinGW/GCC), CMake ≥ 3.20, Python 3 (for the console + optional API test). CUDA is optional (GPU path is runtime-discovered).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DHYPERNOVA_BUILD_TESTS=ON -DHYPERNOVA_BUILD_CLI=ON
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure     # 20/20 PASS
```

Docker (no local toolchain needed):

```bash
docker build -t hypernova:latest .
docker run -d -p 8080:8080 hypernova:latest
```

## 3. 5-minute demo

```bash
# 1) MRPL crude-oil blending (convex QP) — OPTIMAL 22,692.12 k USD/day, 0.33 s
hypernova solve benchmarks/industrial-cases/crude_blending_qp.lp --engine qp

# 2) MRPL refinery production planning (MILP) — OPTIMAL 2,137,300
hypernova solve benchmarks/industrial-cases/refinery_production_planning_milp.mps --engine branch-and-bound

# 3) Full industrial demo: 5 MRPL models + 4 scale tiers (Small→Stress 2,000 vars), all PASS
industrial_demo
```

Web console: run `python console/server.py` then open `http://localhost:8080` (solve, inspect, capabilities APIs under `/api/v1/`).

## 4. Evidence artifacts (measured 2026-09-20, shipped Release build)

| Suite | Result | Artifact |
| :--- | :--- | :--- |
| MRPL industrial (5 models) | 5/5 OPTIMAL PASS | `benchmarks/results/industrial-after-p6.{csv,json}` |
| Netlib (21 instances) | 16/21 PASS (honest TIME_LIMIT/off-tol elsewhere) | `benchmarks/results/netlib-baseline-p0.{csv,json}` |
| MIPLIB 2017 (10 instances) | 1/10 PASS within 60 s (see README §11.3) | `benchmarks/results/miplib-baseline-p0.{csv,json}` |
| QPLIB (6) — convex-QP scope: 0/6 MIQP TIME_LIMIT | `benchmarks/results/qplib-baseline-p0.{csv,json}` |
| Mittelmann (2) — `agg` PASS | `benchmarks/results/mittelmann-baseline-p0.{csv,json}` |
| HiGHS cross-check (MRPL) | 5/5 Verified=yes | `benchmarks/comparison_highs_hypernova.csv` |
| Regression suite | 20/20 targets pass | `ctest` |

Full methodology, tables, and the discrepancy log are in `README.md` (§11, §12, §16) — results are reported as measured, including where the engine falls short (hard-MILP gap, `mas74` false-INFEASIBLE under investigation).

## 5. Contact

- **Team:** [names / colleges — fill in]
- **Email:** [team contact email — fill in]
- **Pitch deck:** as per `FINAL_ACTION_PLAN.md` §8 (10 slides)

## 6. Submission note

- `HyperNova-SIH-2026.tar.gz` = clean `git archive` of `main` (no build dirs, no credentials).
- Rebuild from tarball with §2 commands; demo works from a clean checkout.