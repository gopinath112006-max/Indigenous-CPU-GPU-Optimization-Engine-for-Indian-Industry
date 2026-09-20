#include "qp_common.hpp"

#include "../numerical/factorization.hpp"
#include "../numerical/sparse_matrix.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hypernova::qp {

namespace {

constexpr double K_CLEAN = 1e-8;  // diagonal shift that cures a PSD matrix
constexpr double K_BIG = 1e-4;    // diagonal shift that only cures an indefinite
                                  // matrix whose most negative eigenvalue is
                                  // within the numerically-uncertain band

numerical::SparseMatrix build_symmetric_Q(const model::Problem& problem) {
    const std::size_t nvars = problem.variables.size();
    std::vector<numerical::Triplet> triplets;
    triplets.reserve(2 * problem.quadratic_terms.size());
    std::vector<double> h_diag(nvars, 0.0);
    for (const auto& term : problem.quadratic_terms) {
        if (term.row == term.col) {
            h_diag[term.row] += term.coeff;
        } else {
            triplets.emplace_back(term.row, term.col, term.coeff);
            triplets.emplace_back(term.col, term.row, term.coeff);
        }
    }
    for (std::size_t j = 0; j < nvars; ++j) {
        triplets.emplace_back(j, j, h_diag[j]);
    }
    return numerical::SparseMatrix::from_triplets(nvars, nvars, triplets);
}

struct ProbeResult {
    double min_signed = 0.0;          // most negative pivot observed (signed)
    bool has_small_pivot = false;     // a pivot |d| within the singular tolerance,
                                      // or the factorization flagged a singular state
    bool has_strong_negative = false; // a pivot d < -K_BIG: a definite negative
                                      // eigenvalue safely outside the uncertain band
    bool factor_ok = true;
};

// LDLT of Q + shift*I and its signed pivot spectrum.
ProbeResult clean_probe(const numerical::SparseMatrix& Q,
                        const numerical::ToleranceConfig& tol,
                        double shift) {
    ProbeResult out;
    if (Q.rows() == 0) return out;

    std::vector<numerical::Triplet> triplets = Q.to_triplets();
    if (shift != 0.0) {
        bool patched = false;
        for (auto& t : triplets) {
            if (t.row == t.col) {
                t.value += shift;
                patched = true;
            }
        }
        if (!patched) {
            for (std::size_t i = 0; i < Q.rows(); ++i) {
                triplets.emplace_back(i, i, shift);
            }
        }
    }
    numerical::SparseMatrix shifted =
        numerical::SparseMatrix::from_triplets(Q.rows(), Q.cols(), triplets);
    auto ldlt = numerical::create_factorization(numerical::FactorizationType::LDLT, shifted, tol);
    if (!ldlt) {
        out.factor_ok = false;
        return out;
    }

    double min_pivot = std::numeric_limits<double>::infinity();
    bool small = ldlt->stats().singular;
    for (double d : ldlt->D_values()) {
        min_pivot = std::min(min_pivot, d);
        if (std::abs(d) <= tol.singular_tol()) small = true;
        if (d < -K_BIG) out.has_strong_negative = true;
    }
    out.min_signed = min_pivot;
    out.has_small_pivot = small;
    return out;
}

} // namespace

ConvexityClassification classify_qp_convexity(const model::Problem& problem,
                                              const numerical::ToleranceConfig& tol,
                                              double* min_pivot) {
    if (min_pivot) *min_pivot = 0.0;
    if (problem.quadratic_terms.empty()) return ConvexityClassification::UNKNOWN;

    numerical::SparseMatrix Q = build_symmetric_Q(problem);

    ProbeResult p0 = clean_probe(Q, tol, 0.0);
    if (!p0.factor_ok) return ConvexityClassification::NUMERICALLY_UNCERTAIN;

    // A definite negative pivot (safely outside the uncertain band) at any
    // probe level is indefeasible evidence of an indefinite Hessian.
    if (p0.has_strong_negative) {
        if (min_pivot) *min_pivot = p0.min_signed;
        return ConvexityClassification::NONCONVEX;
    }

    if (!p0.has_small_pivot) {
        // No pivot within the singular tolerance: the sign spectrum is
        // trustworthy.
        if (p0.min_signed < -tol.singular_tol()) {
            // Mildly negative: within the numerically-uncertain band. Never
            // classed as convex.
            if (min_pivot) *min_pivot = p0.min_signed;
            return ConvexityClassification::NUMERICALLY_UNCERTAIN;
        }
        if (min_pivot) *min_pivot = p0.min_signed;
        return ConvexityClassification::CONVEX;
    }

    // A near-zero pivot exists at baseline; the LDLT is not reliably signed so
    // probe with diagonal shifts.  A PSD (or PSD-singular) matrix becomes
    // regular at a tiny positive shift.
    ProbeResult p_clean = clean_probe(Q, tol, K_CLEAN);
    if (p_clean.factor_ok && p_clean.has_strong_negative) {
        if (min_pivot) *min_pivot = p_clean.min_signed;
        return ConvexityClassification::NONCONVEX;
    }

    ProbeResult p_big = clean_probe(Q, tol, K_BIG);
    if (p_big.factor_ok && p_big.has_strong_negative) {
        if (min_pivot) *min_pivot = p_big.min_signed;
        return ConvexityClassification::NONCONVEX;
    }

    if (p_clean.factor_ok && !p_clean.has_small_pivot) {
        if (min_pivot) *min_pivot = p_clean.min_signed;
        return ConvexityClassification::NUMERICALLY_UNCERTAIN;
    }

    if (p_big.factor_ok && !p_big.has_small_pivot) {
        // Indefinite only within the numerically-uncertain magnitude band.
        if (min_pivot) *min_pivot = p_big.min_signed;
        return ConvexityClassification::NUMERICALLY_UNCERTAIN;
    }

    // Still singular even after a 1e-4 diagonal shift: a definite negative
    // eigenvalue of magnitude >= ~1e-4.  Report it as nonconvex.
    if (min_pivot) *min_pivot = p_big.factor_ok ? p_big.min_signed : 0.0;
    return ConvexityClassification::NONCONVEX;
}

} // namespace hypernova::qp