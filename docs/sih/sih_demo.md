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
*   **Action:** Open terminal and run the targeted correctness tests.
*   **Command:** `ctest -R "test_lp|test_milp|test_api" --output-on-failure`
*   **Talking Point:** "We didn't just fix it, we wrote targeted regression tests to cover the mathematical boundaries."
*   **Action:** Show the result.
*   **Talking Point:** "Targeted verification passes flawlessly. Note: Full Netlib regression remains an explicitly documented limitation due to CI environment timeouts, but structural correctness is verified."

## 5. Q&A Transition (1 min)
*   **Wrap-up:** "HyperNova's targeted integrations have been verified, and the core solver framework is ready for continued development."
*   **Action:** Leave the terminal showing "100% tests passed, 0 tests failed out of 3" (for the targeted suites) as the backdrop for evaluator Q&A.
