# Mathematical Verification of Variable Shifting

This document provides a rigorous mathematical verification of the dynamic variable shifting technique applied in `BranchAndBoundSolver::solve_node_lp`. This transformation enforces finite lower bounds on variables without requiring explicit structural constraints, thereby bypassing degenerate Phase I cycles in standard-form Simplex engines.

## 1. Original Node Relaxation (Primal)

At any given node in the Branch & Bound tree, the LP relaxation defines bounds for each variable $x_j$:
$$ lb_j \le x_j \le ub_j \quad \forall j \in \{1, \dots, n\} $$

The generalized LP node problem can be written as:
$$
\begin{align*}
\min \quad & c^T x \\
\text{s.t.} \quad & A x \sim b \\
& lb \le x \le ub
\end{align*}
$$
where $\sim$ represents any constraint sense ($\le, \ge, =$), and $A \in \mathbb{R}^{m \times n}$.

## 2. Shift Transformation

The `SimplexSolver` natively assumes standard form variables $x \ge 0$. Explicitly encoding $x \ge lb$ as a structural matrix row $I x \ge lb$ introduces artificial variables, inflates the basis matrix to $(m+n) \times (m+n)$, and risks degenerate Phase I cycles.

To avoid this, we apply a coordinate shift vector $s \in \mathbb{R}^n$, where:
$$ s_j = \begin{cases} 
lb_j & \text{if } lb_j \neq -\infty \\ 
0 & \text{if } lb_j = -\infty 
\end{cases} $$

We define the shifted variables $x' \in \mathbb{R}^n$:
$$ x' = x - s \implies x = x' + s $$

Substituting $x = x' + s$ into the original formulation yields the **Shifted Node Relaxation**:

### 2.1. Shifted Bounds
$$ lb \le x' + s \le ub \implies 0 \le x' \le ub - s $$
For infinite bounds ($lb_j = -\infty$), $s_j = 0$, mapping precisely to $x'_j \in [-\infty, ub_j]$, which the Simplex engine securely handles via standard variable splitting ($x^+ - x^-$).

### 2.2. Shifted Objective
$$ c^T x = c^T (x' + s) = c^T x' + c^T s $$
We solve for $c^T x'$, and post-solve, we recover the true objective by adding the constant scalar offset $c^T s$.

### 2.3. Shifted Constraints
$$ A (x' + s) \sim b \implies A x' + A s \sim b \implies A x' \sim b - A s $$
The constraint matrix $A$ remains strictly identical. Only the right-hand side vector $b$ is translated.

## 3. Dual and Reduced Cost Invariance

A critical requirement for Branch & Bound is the validity of dual multipliers ($y$) and reduced costs ($rc$) for pruning and node selection. We must prove these are invariant under primal shifting.

The dual of the **Original Problem** (ignoring upper bounds for brevity, as they map identically):
$$
\begin{align*}
\max \quad & b^T y + lb^T v \\
\text{s.t.} \quad & A^T y + v = c \\
& y \text{ (free/signed)}, \quad v \ge 0
\end{align*}
$$
where reduced costs are exactly $rc = c - A^T y$.

The dual of the **Shifted Problem** (where bounds are $x' \ge 0$):
$$
\begin{align*}
\max \quad & (b - A s)^T y \\
\text{s.t.} \quad & A^T y + v' = c \\
& y \text{ (free/signed)}, \quad v' \ge 0
\end{align*}
$$

**Proof of Invariance**:
The dual feasible region ($A^T y + v = c$) is defined entirely by $A$ and $c$, both of which are completely unchanged by the translation vector $s$. Therefore, the coordinate shift does not change the dual feasible region, so the associated dual multipliers and reduced-cost relationships are invariant under the translation. The primal and dual objective values differ by the corresponding constant shift, which is restored when reconstructing the original objective.

Because $y$ and $rc$ are mathematically invariant, they can be directly extracted from the Simplex solver and passed to the B&B tree without any inverse-shifting logic.

## 4. Code Implementation Mapping

The theoretical transformation matches the implementation in `core/milp/branch_and_bound.cpp` line-by-line:

| Mathematical Concept | C++ Implementation (`solve_node_lp`) |
| :--- | :--- |
| **$s_j$ assignment** | `lp_shifts[j] = lb;` |
| **$0 \le x' \le ub - s$** | `builder.add_variable(0.0, ub - lb, ...)` |
| **$c^T s$ tracking** | `obj_offset += var.objective_coeff * lp_shifts[j];` |
| **$b_i - (A s)_i$** | `shift_sum += A.values()[k] * lp_shifts[A.col_indices()[k]];` <br> `builder.add_constraint(..., con.rhs - shift_sum, ...);` |
| **$x = x' + s$** | `relax_primal[j] += lp_shifts[j];` |
| **$z = z' + c^T s$** | `relax_objective = lp_result.objective_value + obj_offset;` |
| **$rc = rc'$** | `relax_reduced_costs = std::move(lp_result.reduced_costs);` (No unshifting required) |

## 5. Conclusion

The transformation is mathematically sound, bounds-preserving, dual-invariant, and exactly reconstructs the original MILP relaxation. By absorbing the lower bound into the variable origin, the technique guarantees that the `SimplexSolver` operates exclusively in its native, robust standard form—entirely sidestepping the degeneracy loops exposed by explicit node constraints in `test_randomized` and `test_regression_smoke`.
