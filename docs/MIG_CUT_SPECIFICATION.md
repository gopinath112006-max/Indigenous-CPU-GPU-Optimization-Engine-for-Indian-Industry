# Mixed-Integer Gomory (MIG) Cut Specification

## 1. Introduction
This document defines the mathematical contract for the Mixed-Integer Gomory (MIG) cuts implemented in HyperNova. This corresponds to the correct tableau-based separation handling rows with both integer and continuous variables.

## 2. Tableau Row Representation
Assume the LP relaxation of a node has been solved to optimality. Let $B$ be the optimal basis matrix. For a fractional basic integer variable $x_i$, the corresponding row of the optimal simplex tableau can be expressed as:

$$x_i + \sum_{j \in J} \bar{a}_{ij} x_j = \bar{b}_i$$

where:
- $x_i$ is the basic variable.
- $J$ is the set of non-basic variables.
- $\bar{a}_{ij} = (B^{-1} A_j)_i$ is the tableau coefficient.
- $\bar{b}_i = (B^{-1} b)_i$ is the fractional basic value.

In HyperNova, `SimplexSolver::compute_tableau_row_for_var` provides the coefficients $\bar{a}_{ij}$ mapped back to the original model variables.

## 3. Fractional Parts
For any scalar $v$, we define the fractional part $f_0(v) = v - \lfloor v \rfloor$. 
We define $f_0 = \bar{b}_i - \lfloor \bar{b}_i \rfloor$. 
Because $x_i$ is fractional, $0 < f_0 < 1$.

For coefficients $a = \bar{a}_{ij}$, we define the fractional part $f_j = a - \lfloor a \rfloor$.

## 4. MIG Cut Derivation
The valid MIG cut is derived by grouping the non-basic variables $J$ into:
- $J_I$: Integer non-basic variables
- $J_C$: Continuous non-basic variables

The standard derivation of the Mixed-Integer Gomory cut yields:

$$
\sum_{j \in J_I, f_j \le f_0} f_j x_j 
+ \sum_{j \in J_I, f_j > f_0} \frac{f_0(1 - f_j)}{1 - f_0} x_j 
+ \sum_{j \in J_C, \bar{a}_{ij} \ge 0} \bar{a}_{ij} x_j 
+ \sum_{j \in J_C, \bar{a}_{ij} < 0} \frac{-f_0}{1 - f_0} \bar{a}_{ij} x_j 
\ge f_0
$$

Since HyperNova standardizes constraints to the `LE` (less than or equal to) sense for generation, we multiply by $-1$:

$$
\sum_{j \in J_I, f_j \le f_0} -f_j x_j 
+ \sum_{j \in J_I, f_j > f_0} -\frac{f_0(1 - f_j)}{1 - f_0} x_j 
+ \sum_{j \in J_C, \bar{a}_{ij} \ge 0} -\bar{a}_{ij} x_j 
+ \sum_{j \in J_C, \bar{a}_{ij} < 0} \frac{f_0}{1 - f_0} \bar{a}_{ij} x_j 
\le -f_0
$$

## 5. Handling Slacks and Original Variables
HyperNova's `compute_tableau_row_for_var` extracts the row for the **original** structural variables. Slacks are substituted back automatically or excluded if they are strictly basic. If a slack variable is non-basic, it means the corresponding original inequality constraint is tight.
To handle constraints $\sum a_{kj} x_j \le b_k$ where the slack $s_k$ is non-basic, the cut coefficient for $s_k$ must be computed and then $s_k = b_k - \sum a_{kj} x_j$ must be substituted into the cut so that the final cut is expressed entirely in terms of the original variables $x_j$.

### Substitution Mathematics
If a slack variable $s_k$ has coefficient $\pi_k$ in the MIG cut, we perform:
$$ \dots + \pi_k s_k \le \text{RHS} $$
$$ \dots + \pi_k (b_k - \sum_j A_{kj} x_j) \le \text{RHS} $$
This shifts the RHS by $-\pi_k b_k$ and adds $-\pi_k A_{kj}$ to the coefficient of $x_j$.

## 6. Numerical Safeguards
- **Fractional Tolerance:** Cuts are only generated if $f_0 \in [10^{-4}, 1 - 10^{-4}]$.
- **Coefficient filtering:** Coefficients $|\pi_j| < 10^{-9}$ are dropped.
- **Dynamism/Scaling:** The maximum ratio between the largest and smallest non-zero coefficient must not exceed $10^6$. If it does, the cut is rejected to maintain LP conditioning.
- **Violation:** The current fractional LP solution must violate the generated cut by at least $10^{-4}$.
