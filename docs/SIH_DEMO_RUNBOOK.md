# SIH Evaluation Demo Runbook

## 1. Introduction (1 min)
*   State the Problem Statement: **PS-26119 ?" Indigenous GPU-Accelerated Optimization Solver**.
*   Introduce **HyperNova**: An indigenous, from-scratch, C++20 mathematical solver capable of LP, MILP, QP, and MIQP, with zero external mathematical dependencies.

## 2. Model Loading & CLI (1 min)
*   **Action:** hypernova solve examples/production_planning.lp
*   **Highlight:** Show the CLI cleanly parsing the LP format, building the internal mathematical model without crashing.

## 3. Presolve & Diagnostics (1 min)
*   **Action:** Point to the CLI output where the Presolve engine eliminates redundant constraints and bounds variables.
*   **Highlight:** This proves industrial scalability, a core requirement of PS-26119.

## 4. Branch-and-Cut & True MIG (2 min)
*   **Action:** hypernova solve benchmarks/miplib/mas74.mps
*   **Highlight:** Explain that mas74 is an academic benchmark that tests the Gomory Cut capabilities. Show the logs where True MIG extraction generates cuts and reduces the fractional LP bound, drastically dropping the node count.

## 5. GPU Acceleration Architecture (2 min)
*   **Action:** Show core/execution/gpu_backend.cpp.
*   **Highlight:** Explain the custom PTX JIT-compiled GPU offload for Sparse Matrix-Vector (SpMV). HyperNova achieves this with native NVIDIA Driver API calls, meaning it runs on any modern GPU without a 4GB CUDA toolkit install. 

## 6. API & Scalability (1 min)
*   **Action:** ctest -R test_rest_system
*   **Highlight:** HyperNova ships with a C++ API, a C ABI for FFI (Python/Java), and a JSON REST API for microservice deployment in Indian industrial plants.
