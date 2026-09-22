#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include <cstddef>
#include <string>

namespace hypernova::qp {

// Classification of a QP objective's Hessian conforming to the Convexity Scope Specification.
enum class ConvexityClassification {
    UNKNOWN,                // no quadratic objective (linear program), or factorization unavailable
    CONVEX,                 // strictly convex / positive-definite (all pivots well above singular tolerance)
    NUMERICALLY_UNCERTAIN,  // numerically ambiguous / near-singular PSD within tolerance (supported with regularization)
    NONCONVEX,              // materially indefinite or negative definite (unsupported)
    CONVEX_OUTSIDE_SCOPE,   // convex mathematically but outside current supported model class (e.g. invalid dimensions)
    ASYMMETRIC              // significantly asymmetric Hessian exceeding symmetry tolerance (invalid)
};

// Detailed diagnostic report for convexity classification.
struct ConvexityDiagnostics {
    ConvexityClassification classification = ConvexityClassification::UNKNOWN;
    double min_curvature = 0.0;       // minimum detected curvature / eigenvalue / pivot estimate
    double convexity_tol = 1e-8;      // tolerance tau_PSD used for classification
    double symmetry_error = 0.0;      // maximum asymmetry |Q_ij - Q_ji|
    bool is_symmetric = true;         // whether symmetry error <= tau_sym
    bool is_psd = true;               // whether matrix is strictly or numerically PSD
    bool is_supported = true;         // whether model class is supported
    std::string rejection_reason;     // descriptive diagnostic message if rejected
};

// Diagnoses the symmetric Hessian Q implied by `problem.quadratic_terms`.
// Validates dimensions, verifies numerical symmetry against tau_sym,
// symmetrizes within tolerance, and probes curvature against tau_PSD.
ConvexityDiagnostics diagnose_qp_convexity(const model::Problem& problem,
                                          const numerical::ToleranceConfig& tol);

// Legacy classification interface, delegates to diagnose_qp_convexity.
ConvexityClassification classify_qp_convexity(const model::Problem& problem,
                                              const numerical::ToleranceConfig& tol,
                                              double* min_pivot = nullptr);

} // namespace hypernova::qp