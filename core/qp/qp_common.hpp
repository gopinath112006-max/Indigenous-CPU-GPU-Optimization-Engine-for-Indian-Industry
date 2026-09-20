#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include <cstddef>

namespace hypernova::qp {

// Classification of a QP objective's Hessian.
enum class ConvexityClassification {
    UNKNOWN,                // no quadratic objective (linear program), or factorization unavailable
    CONVEX,                 // all pivots well above the singular tolerance
    NUMERICALLY_UNCERTAIN,  // near-singular / PSD-to-roundoff: no definite negative
                            // pivot at the baseline, but a pivot that is zero to
                            // numerical precision (the KKT may be ill-conditioned)
    NONCONVEX,              // a definite negative pivot (indefinite Hessian)
};

// Classifies the symmetric Hessian Q implied by `problem.quadratic_terms`.
//
// Convention: the objective is  1/2 x^T Q x + c^T x   with  Q_ii = stored
// diagonal term and  Q_ij = Q_ji = stored cross term (the cross term is stored
// once and mirrored, never doubled).  Classification runs an LDLT on Q and
// probes increasing diagonal regularization:
//   * Q clean at no regularization           -> CONVEX
//   * Q singular but Q + k_clean*I clean     -> NUMERICALLY_UNCERTAIN (PSD)
//   * Q still singular at Q + k_big*I        -> NONCONVEX (indefinite)
//
// The baseline LDLT clamps negative pivots to +singular_tol, so a naive
// "any pivot < 0" check can never fire; the diagonal-regularization probe
// recovers the sign information: a PSD matrix becomes clean as soon as a tiny
// positive multiple of I is added, while an indefinite matrix stays singular
// until the shift exceeds the magnitude of its most negative eigenvalue.
ConvexityClassification classify_qp_convexity(const model::Problem& problem,
                                              const numerical::ToleranceConfig& tol,
                                              double* min_pivot = nullptr);

} // namespace hypernova::qp