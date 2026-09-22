#include "qp_common.hpp"

#include "../numerical/factorization.hpp"
#include "../numerical/sparse_matrix.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace hypernova::qp {

namespace {

struct ProbeResult {
    double min_signed = 0.0;          // most negative pivot observed (signed)
    bool has_small_pivot = false;     // a pivot |d| within the singular tolerance,
                                      // or the factorization flagged a singular state
    bool has_strong_negative = false; // a pivot d < -tau_psd
    bool factor_ok = true;
};

// LDLT of Q + shift*I and its signed pivot spectrum.
ProbeResult clean_probe(const numerical::SparseMatrix& Q,
                        const numerical::ToleranceConfig& tol,
                        double shift,
                        double tau_psd) {
    ProbeResult out;
    if (Q.rows() == 0) return out;

    std::vector<numerical::Triplet> triplets = Q.to_triplets();
    if (shift != 0.0) {
        for (std::size_t i = 0; i < Q.rows(); ++i) {
            triplets.emplace_back(i, i, shift);
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
        if (d < -tau_psd) out.has_strong_negative = true;
    }
    out.min_signed = min_pivot;
    out.has_small_pivot = small;
    return out;
}

} // namespace

ConvexityDiagnostics diagnose_qp_convexity(const model::Problem& problem,
                                          const numerical::ToleranceConfig& tol) {
    ConvexityDiagnostics diag;
    const std::size_t nvars = problem.variables.size();

    if (problem.quadratic_terms.empty() || nvars == 0) {
        diag.classification = ConvexityClassification::UNKNOWN;
        diag.is_psd = true;
        diag.is_supported = true;
        diag.is_symmetric = true;
        diag.min_curvature = 0.0;
        return diag;
    }

    // 1. Validate dimensions
    for (const auto& term : problem.quadratic_terms) {
        if (term.row >= nvars || term.col >= nvars) {
            diag.classification = ConvexityClassification::CONVEX_OUTSIDE_SCOPE;
            diag.is_supported = false;
            diag.rejection_reason = "Quadratic term index (" + std::to_string(term.row) + ", " +
                                    std::to_string(term.col) + ") out of bounds for nvars=" +
                                    std::to_string(nvars);
            return diag;
        }
    }

    // 2. Accumulate entries and evaluate symmetry
    const double sense_factor = (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) ? -1.0 : 1.0;
    std::map<std::pair<std::size_t, std::size_t>, double> term_map;
    double max_coeff = 0.0;
    for (const auto& term : problem.quadratic_terms) {
        term_map[{term.row, term.col}] += sense_factor * term.coeff;
        max_coeff = std::max(max_coeff, std::abs(term.coeff));
    }

    const double tau_sym = std::max(tol.feasibility_tol(), 1e-8) * std::max(1.0, max_coeff);
    diag.symmetry_error = 0.0;
    diag.is_symmetric = true;

    for (const auto& [rc, val] : term_map) {
        std::size_t r = rc.first;
        std::size_t c = rc.second;
        if (r < c) {
            auto it_rev = term_map.find({c, r});
            if (it_rev != term_map.end()) {
                double diff = std::abs(val - it_rev->second);
                diag.symmetry_error = std::max(diag.symmetry_error, diff);
            }
        }
    }

    if (diag.symmetry_error > tau_sym) {
        diag.is_symmetric = false;
        diag.is_supported = false;
        diag.classification = ConvexityClassification::ASYMMETRIC;
        diag.rejection_reason = "Significantly asymmetric Hessian: asymmetry error " +
                                std::to_string(diag.symmetry_error) + " exceeds tolerance " +
                                std::to_string(tau_sym);
        return diag;
    }

    // 3. Build symmetrized matrix Q
    std::vector<numerical::Triplet> triplets;
    std::vector<double> h_diag(nvars, 0.0);
    std::set<std::pair<std::size_t, std::size_t>> processed;

    for (const auto& [rc, val] : term_map) {
        std::size_t r = rc.first;
        std::size_t c = rc.second;
        if (r == c) {
            h_diag[r] += val;
        } else {
            std::size_t min_idx = std::min(r, c);
            std::size_t max_idx = std::max(r, c);
            if (processed.count({min_idx, max_idx})) continue;
            processed.insert({min_idx, max_idx});

            double sym_val = val;
            auto it_rev = term_map.find({c, r});
            if (it_rev != term_map.end()) {
                sym_val = 0.5 * (val + it_rev->second);
            }
            triplets.emplace_back(min_idx, max_idx, sym_val);
            triplets.emplace_back(max_idx, min_idx, sym_val);
        }
    }
    for (std::size_t j = 0; j < nvars; ++j) {
        triplets.emplace_back(j, j, h_diag[j]);
    }
    numerical::SparseMatrix Q = numerical::SparseMatrix::from_triplets(nvars, nvars, triplets);

    // 4. Determine positive semidefiniteness against tau_PSD
    const double tau_psd = std::max(tol.optimality_tol(), 1e-8) * std::max(1.0, max_coeff);
    diag.convexity_tol = tau_psd;

    ProbeResult p0 = clean_probe(Q, tol, 0.0, tau_psd);
    if (!p0.factor_ok) {
        diag.classification = ConvexityClassification::NUMERICALLY_UNCERTAIN;
        diag.is_psd = false;
        diag.is_supported = true;
        diag.rejection_reason = "Factorization failure during convexity classification";
        return diag;
    }

    if (p0.has_strong_negative) {
        diag.min_curvature = p0.min_signed;
        diag.is_psd = false;
        diag.is_supported = false;
        diag.classification = ConvexityClassification::NONCONVEX;
        diag.rejection_reason = "Indefinite Hessian: detected negative curvature " +
                                std::to_string(p0.min_signed) + " exceeds tolerance -" +
                                std::to_string(tau_psd);
        return diag;
    }

    if (!p0.has_small_pivot) {
        diag.min_curvature = p0.min_signed;
        if (p0.min_signed < -tau_psd) {
            diag.is_psd = false;
            diag.is_supported = false;
            diag.classification = ConvexityClassification::NONCONVEX;
            diag.rejection_reason = "Indefinite Hessian";
        } else if (p0.min_signed < 0.0) {
            // Negative pivot within tau_psd: numerically PSD
            diag.is_psd = true;
            diag.is_supported = true;
            diag.classification = ConvexityClassification::NUMERICALLY_UNCERTAIN;
        } else {
            diag.is_psd = true;
            diag.is_supported = true;
            diag.classification = ConvexityClassification::CONVEX;
        }
        return diag;
    }

    // Singular or near-singular pivot at baseline: probe with small shifts
    const double k_clean = std::min(1e-8, tau_psd);
    const double k_big = std::max(1e-4, 10.0 * tau_psd);

    ProbeResult p_clean = clean_probe(Q, tol, k_clean, tau_psd);
    if (p_clean.factor_ok && p_clean.has_strong_negative) {
        diag.min_curvature = p_clean.min_signed;
        diag.is_psd = false;
        diag.is_supported = false;
        diag.classification = ConvexityClassification::NONCONVEX;
        diag.rejection_reason = "Indefinite Hessian: negative curvature persists after clean shift";
        return diag;
    }

    ProbeResult p_big = clean_probe(Q, tol, k_big, tau_psd);
    if (p_big.factor_ok && p_big.has_strong_negative) {
        diag.min_curvature = p_big.min_signed;
        diag.is_psd = false;
        diag.is_supported = false;
        diag.classification = ConvexityClassification::NONCONVEX;
        diag.rejection_reason = "Indefinite Hessian: negative curvature persists after big shift";
        return diag;
    }

    if (p_clean.factor_ok && !p_clean.has_small_pivot) {
        // Cured by tiny shift -> singular PSD
        diag.min_curvature = std::max(0.0, p0.min_signed);
        diag.is_psd = true;
        diag.is_supported = true;
        diag.classification = ConvexityClassification::NUMERICALLY_UNCERTAIN;
        return diag;
    }

    if (p_big.factor_ok && !p_big.has_small_pivot) {
        diag.min_curvature = p_big.min_signed;
        diag.is_psd = true;
        diag.is_supported = true;
        diag.classification = ConvexityClassification::NUMERICALLY_UNCERTAIN;
        return diag;
    }

    // Still singular after big shift: indefinite
    diag.min_curvature = p_big.factor_ok ? p_big.min_signed : p0.min_signed;
    diag.is_psd = false;
    diag.is_supported = false;
    diag.classification = ConvexityClassification::NONCONVEX;
    diag.rejection_reason = "Indefinite Hessian: singular structure persists after regularization";
    return diag;
}

ConvexityClassification classify_qp_convexity(const model::Problem& problem,
                                              const numerical::ToleranceConfig& tol,
                                              double* min_pivot) {
    ConvexityDiagnostics diag = diagnose_qp_convexity(problem, tol);
    if (min_pivot) *min_pivot = diag.min_curvature;
    return diag.classification;
}

} // namespace hypernova::qp