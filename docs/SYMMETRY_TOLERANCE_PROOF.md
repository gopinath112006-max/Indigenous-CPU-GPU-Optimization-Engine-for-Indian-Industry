# Mathematical Proof: Symmetry Equivalence and Optimality Tolerance

**Theorem:** Using `optimality_tol` ($\epsilon_{opt}$) for objective-coefficient equivalence in structural symmetry detection preserves the correctness of the MILP solver under HyperNova's documented optimality semantics, provided that the symmetry group operates on variables with domain ranges $\le 1$ (e.g., binary variables).

**Proof:**

1. **Definition of Optimality Semantic Contract**
   HyperNova defines a solution $x^*$ as optimal if its objective value $Z(x^*)$ is within the configured `optimality_tol` ($\epsilon_{opt}$) of the true global supremum (or infimum) $Z_{true}$. 
   Mathematically, for a maximization problem:
   $Z_{true} - Z(x^*) \le \epsilon_{opt}$

2. **Definition of Symmetry-Breaking Cuts**
   Let $x_i$ and $x_j$ be two variables identified as structurally symmetric. The symmetry detector enforces a lexicographical ordering, e.g., generating the cut $x_i \ge x_j$.
   This restricts the feasible space $\mathcal{F}$ to a subset $\mathcal{F}' \subseteq \mathcal{F}$.
   For this restriction to be correct, it must not exclude all true optimal solutions unless it retains a symmetric equivalent that satisfies the optimality contract over $\mathcal{F}$.

3. **Equivalence Relation with $\epsilon_{opt}$**
   The detector groups $x_i$ and $x_j$ if they share identical bounds (within `feasibility_tol`), identical constraint columns (within `feasibility_tol`), and their objective coefficients $c_i, c_j$ satisfy:
   $|c_i - c_j| \le \epsilon_{opt}$

4. **Maximum Objective Deviation**
   Suppose the true unconstrained global optimum in $\mathcal{F}$ occurs at a point where $x_i = 0$ and $x_j = U$, giving the true optimum $Z_{true}$.
   Because of the symmetry cut $x_i \ge x_j$, this point is excluded from $\mathcal{F}'$.
   However, due to the structural equivalence of their columns and bounds, the symmetric permutation where $x_i = U$ and $x_j = 0$ is guaranteed to be feasible in $\mathcal{F}'$.
   Let this symmetric equivalent point be $\hat{x}$.
   The objective value of this forced point is:
   $Z(\hat{x}) = Z_{true} - c_j U + c_i U = Z_{true} + U(c_i - c_j)$
   The absolute degradation in the objective is:
   $|Z_{true} - Z(\hat{x})| = U |c_i - c_j|$

5. **Bounding the Error**
   We established the equivalence relation $|c_i - c_j| \le \epsilon_{opt}$.
   Therefore, the maximum degradation is:
   $|Z_{true} - Z(\hat{x})| \le U \epsilon_{opt}$

6. **Conclusion and Requirement**
   To satisfy the semantic contract $|Z_{true} - Z(\hat{x})| \le \epsilon_{opt}$, we mathematically require:
   $U \epsilon_{opt} \le \epsilon_{opt} \implies U \le 1$
   
   If the variables are binary ($U=1$) or otherwise bounded such that their domain range is $\le 1$, the maximum deviation introduced by the symmetry cut is strictly bounded by $\epsilon_{opt}$. Therefore, returning $\hat{x}$ fully satisfies HyperNova's termination criteria for optimality. The set of "acceptable" optimal solutions is thus preserved.
   
   *(Note: For general continuous variables where $U \gg 1$, using $\epsilon_{opt}$ directly on coefficients is formally a relaxation of global optimality by a factor of $U$. To make the proof hold for arbitrary bounds, the equivalence relation must be dynamically scaled to $|c_i - c_j| \le \epsilon_{opt} / \max(1.0, U)$, or must fall back to the solver's strict `zero_tol`.)*
