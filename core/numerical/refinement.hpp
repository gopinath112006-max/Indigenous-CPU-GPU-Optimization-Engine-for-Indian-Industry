#pragma once

#include "factorization.hpp"
#include "sparse_matrix.hpp"
#include <vector>

namespace hypernova::numerical {

// Iterative refinement for linear solves of the form  A x = b.
//
// Given a factorization `fac` of a matrix that approximates `A` (typically A
// itself, but possibly with pivoting that perturbs the system), and an initial
// solution `x`, this repeatedly computes the residual  r = b - A*x  and applies
// a factorization-based correction until the relative max-norm residual drops
// below `relative_tol` or `max_iter` corrections have been performed.
//
// Returns the number of correction sweeps actually applied (0 if the initial
// solution already meets the tolerance).
int iterative_refinement(const SparseFactorization& fac,
                         const SparseMatrix& A,
                         std::vector<double>& x,
                         const std::vector<double>& rhs,
                         double relative_tol = 1e-10,
                         int max_iter = 5);

// Dense KKT-style solve with iterative refinement.
//
// `kkt` is a dense `total x total` matrix indexed as kkt[r*total+c], `rhs` the
// right-hand side. Solves Gauss-elimination (partial pivoting, tiny-pivot clamp)
// and then refines against the pristine matrix until the relative max-norm
// residual drops below `relative_tol` or `max_iter` corrections are performed.
// Returns the number of correction sweeps applied.
int dense_solve_refined(const std::vector<double>& kkt,
                        std::size_t total,
                        const std::vector<double>& rhs,
                        std::vector<double>& sol,
                        double relative_tol = 1e-10,
                        int max_iter = 4);

} // namespace hypernova::numerical