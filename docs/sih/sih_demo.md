# SIH 5-10 Min Demo Outline: HyperNova

## 1. Introduction (1 min)
*   State the mission: "We built HyperNova, a sovereign optimization engine."
*   Explain the scope: Solving LPs and MILPs, currently dominated by proprietary foreign software (Gurobi, CPLEX).

## 2. Architecture & CLI Tour (2 mins)
*   **Action:** Show the HyperNova CLI in action.
*   **Command:** Run a quick solve on an MPS file: `hypernova solve data/test.mps`
*   **Highlight:** Point out the phase reporting (Presolve, Phase I, Phase II) and the raw solve time.

## 3. The Engineering Story (3 mins)
*   **Action:** Open the code for `core/milp/branch_and_bound.cpp`.
*   **Talking Point:** Explain that building solvers is hard because of numerical edge cases.
*   **Story:** Describe the bound-shifting bug. "We found a correctness defect where shifting variable bounds caused infinite loops in the B&B tree."
*   **The Fix:** Show the `lp_shifts` array and the inverse transformation (`relax_primal[j] += lp_shifts[j]`). Emphasize that we used mathematical invariance to fix the issue *without* destabilizing the core Simplex engine.

## 4. Live Testing Proof (2 mins)
*   **Action:** Open terminal and run the targeted tests.
*   **Command:** `ctest -R test_bound_shifting --output-on-failure`
*   **Talking Point:** "We didn't just fix it, we wrote 10 targeted regression tests to cover every mathematical boundary (min, max, negative, mixed)."
*   **Action:** Run the full suite. `ctest -j4`
*   **Talking Point:** Show that the entire system (including Netlib regressions) passes flawlessly.

## 5. Q&A Transition (1 min)
*   **Wrap-up:** "HyperNova is robust, computationally sound, and ready for integration."
*   **Action:** Leave the `ctest` "100% tests passed" screen open as the backdrop for evaluator Q&A.
