# HyperNova Industrial Optimization Models (MRPL Case Studies)

**SIH Problem Statement ID:** 26119  
**Organization:** Mangalore Refinery and Petrochemicals Limited (MRPL)  
**Theme:** Smart Automation / Indigenous GPU-Accelerated Optimization Engine

---

## Overview

This suite contains five production-grade industrial optimization models engineered using the **HyperNova C++ Core API**. The models simulate real-world process engineering, refinery planning, logistics dispatch, captive power generation, and hydrogen network balance for petroleum refining facilities such as MRPL.

All models run without proprietary foreign solver dependencies (CPLEX, Gurobi, Xpress) and are verified by HyperNova's built-in solution verifiers.

---

## The 5 Industrial Models

### 1. Crude Oil Blending & Quality Giveaway Optimization (Convex QP)
- **Domain:** Single Point Mooring (SPM) Crude Imports & BS-VI / Euro-VI Fuel Quality Compliance.
- **Mathematical Class:** Convex Quadratic Programming (QP) with off-diagonal cross terms.
- **Formulation:**
  - **Decision Variables:** Crude procurement quantities ($x_1, \dots, x_5$) for Arab Light, Bonny Light, Maya Heavy, Murban Sweet, and Basrah Heavy.
  - **Objective:** Minimize procurement cost $+$ quadratic quality giveaway penalty:
    $$\min \sum_{i=1}^5 c_i x_i + \frac{1}{2} w \left( \sum_{i=1}^5 S_i x_i - S_{\text{target}} \sum_{i=1}^5 x_i \right)^2$$
  - **Constraints:** Total distillation target (300 k bbl/day), blend API gravity $\ge 31.0^\circ$ API, max sulfur $\le 1.40\%$ wt, and blend viscosity range ($3.5 \le \nu \le 5.0$ cSt).

### 2. Multi-Period Refinery Production Planning & Mode Switching (MILP)
- **Domain:** Phase-III Hydrocracker & FCCU Mode Selection across a 3-month planning horizon.
- **Mathematical Class:** Mixed-Integer Linear Programming (MILP).
- **Formulation:**
  - **Decision Variables:** Product volumes (Gasoline, Diesel, Jet Fuel) and binary mode indicators ($m_{\text{diesel},t}, m_{\text{atf},t} \in \{0, 1\}$).
  - **Objective:** Maximize net 3-month refining margin (Product Revenue $-$ Operating Costs $-$ Mode Switch Penalties).
  - **Constraints:** Mode mutual exclusion ($m_{\text{diesel},t} + m_{\text{atf},t} = 1$), yield upper bounds tied to active mode, and seasonal minimum supply commitments.

### 3. Multi-Modal Supply Chain & Coastal Freight Dispatch (MILP)
- **Domain:** Coastal Tankers & MHBL Cross-Country Pipeline Product Distribution from Mangalore Refinery.
- **Mathematical Class:** Mixed-Integer Programming (Fixed-Charge Network Flow).
- **Formulation:**
  - **Decision Variables:** Dispatch volumes to Hassan, Bengaluru, Goa, Kochi, and Hyderabad, plus binary batch triggers ($z_{\text{pipe}}, z_{\text{rail}} \in \{0, 1\}$).
  - **Objective:** Minimize total variable shipping cost $+$ fixed batch activation fees.
  - **Constraints:** Minimum batch size triggers ($x \ge \text{MinBatch} \cdot z$), route throughput capacities, and regional terminal demand bounds.

### 4. Captive Power Plant Cogeneration & Unit Commitment (MILP)
- **Domain:** High-Pressure Steam Boilers, Gas Turbines, and State Electrical Grid Integration.
- **Mathematical Class:** Mixed-Integer Programming (Unit Commitment & Steam-Heat Balance).
- **Formulation:**
  - **Decision Variables:** Power output (MW) and binary commitment flags ($u_{i} \in \{0, 1\}$) for Utility Boilers, Gas Turbines, and Grid Import.
  - **Objective:** Minimize fuel cost $+$ fixed hourly unit commitment fees $+$ grid import costs.
  - **Constraints:** Refinery electrical load balance (110 MW), unit operating bounds ($P_{\text{min}} \cdot u_i \le P_i \le P_{\text{max}} \cdot u_i$), and HP steam header baseline generation.

### 5. Refinery Hydrogen Network Purity & Feedstock Optimization (LP)
- **Domain:** DHDS & VGO Hydrotreater Hydrogen Consumption & PSA Unit Purification.
- **Mathematical Class:** Linear Programming (LP Flow Balance).
- **Formulation:**
  - **Decision Variables:** Stream flow rates ($k \text{ Nm}^3/\text{hr}$) for HGU Natural Gas Reformer, CCR Off-Gas, PSA Purge, DHDS, and VGO Hydrotreater.
  - **Objective:** Minimize natural gas reformer feedstock expense.
  - **Constraints:** Hydrogen purity and mass balances, minimum hydrotreater unit demands, and PSA recovery limits.

---

## How to Build & Run

### 1. Execute the Full Industrial Suite
```bash
cmake --build build --target industrial_demo
./build/bin/industrial_demo
```

### 2. Solve Exported Models via CLI
Each case study automatically exports its model to disk (`.lp` or `.mps`):
```bash
./build/bin/hypernova solve benchmarks/industrial-cases/crude_blending_qp.lp --engine qp
./build/bin/hypernova solve benchmarks/industrial-cases/refinery_production_planning_milp.mps --engine branch-and-bound
./build/bin/hypernova solve benchmarks/industrial-cases/logistics_freight_milp.mps --engine branch-and-cut
./build/bin/hypernova solve benchmarks/industrial-cases/cogen_power_milp.mps --engine branch-and-bound
./build/bin/hypernova solve benchmarks/industrial-cases/hydrogen_network_lp.mps --engine auto
```
