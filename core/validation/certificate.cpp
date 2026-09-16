#include "certificate.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hypernova::validation {

namespace {

bool is_inf(double v) {
    return v == std::numeric_limits<double>::infinity() ||
           v == -std::numeric_limits<double>::infinity();
}

// Column-major view of the constraint matrix: for each variable, the list of
// (row, coefficient) pairs. Builds in O(nnz + n).
std::vector<std::vector<std::pair<std::size_t, double>>> columns_of(
        const numerical::SparseMatrix& A, std::size_t ncols) {
    std::vector<std::vector<std::pair<std::size_t, double>>> cols(ncols);
    const auto& rp = A.row_ptr();
    const auto& ci = A.col_indices();
    const auto& av = A.values();
    for (std::size_t i = 0; i < A.rows(); ++i) {
        for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
            std::size_t j = static_cast<std::size_t>(ci[k]);
            if (j < ncols) cols[j].push_back({i, av[k]});
        }
    }
    return cols;
}

} // namespace

CertificateAnalyzer::CertificateAnalyzer(const numerical::ToleranceConfig& tol,
                                         const lp::SimplexOptions& options)
    : tol_(tol), options_(options) {}

FarkasCertificate CertificateAnalyzer::verify_farkas(const model::Problem& p,
                                                     const std::vector<double>& y,
                                                     const numerical::ToleranceConfig& tol) {
    FarkasCertificate c;
    c.multipliers = y;
    const std::size_t m = p.constraints.size();
    const std::size_t n = p.variables.size();
    if (y.size() < m) {
        c.message = "multiplier vector is shorter than the constraint count";
        return c;
    }

    // Sign consistency with the row sense.
    bool sign_ok = true;
    std::string failure;
    for (std::size_t i = 0; i < m; ++i) {
        if (p.constraints[i].sense == model::ConstraintSense::LE &&
            y[i] < -tol.feasibility_tol()) {
            sign_ok = false;
            failure = "LE row has a negative multiplier";
            break;
        }
        if (p.constraints[i].sense == model::ConstraintSense::GE &&
            y[i] > tol.feasibility_tol()) {
            sign_ok = false;
            failure = "GE row has a positive multiplier";
            break;
        }
    }
    if (!sign_ok) {
        c.message = "sign mismatch: " + failure;
        return c;
    }

    // Stationarity normals, bound absorption, and the violation value.
    const auto& A = p.constraint_matrix;
    double value = 0.0;
    double max_violation = 0.0;
    bool neg_inf = false;
    for (std::size_t i = 0; i < m; ++i) {
        value += y[i] * p.constraints[i].rhs;
    }

    for (std::size_t j = 0; j < n; ++j) {
        double N = 0.0;
        const double lb = p.variables[j].lower_bound;
        const double ub = p.variables[j].upper_bound;
        for (std::size_t i = 0; i < m; ++i) {
            double aij = A.get(i, j);
            if (aij != 0.0) N += y[i] * aij;
        }
        if (N >= tol.feasibility_tol()) {
            max_violation = std::max(max_violation, N);
            if (is_inf(lb)) {
                neg_inf = true;
            } else {
                value += lb * N;
            }
        } else if (N <= -tol.feasibility_tol()) {
            max_violation = std::max(max_violation, -N);
            if (is_inf(ub)) {
                neg_inf = true;
            } else {
                value -= ub * (-N);
            }
        }
    }

    c.farkas_value = value;
    c.violation = max_violation;
    if (neg_inf || value < -tol.feasibility_tol()) {
        c.valid = true;
        c.message = "verified Farkas certificate";
    } else {
        c.message = "cannot certify infeasibility (no negative violation value)";
    }
    return c;
}

UnboundedRay CertificateAnalyzer::verify_ray(const model::Problem& p,
                                             const std::vector<double>& d,
                                             const numerical::ToleranceConfig& tol) {
    UnboundedRay r;
    r.direction = d;
    const std::size_t m = p.constraints.size();
    const std::size_t n = p.variables.size();
    if (d.size() < n) {
        r.message = "direction vector is shorter than the variable count";
        return r;
    }

    const auto& A = p.constraint_matrix;
    const auto& rp = A.row_ptr();
    const auto& ci = A.col_indices();
    const auto& av = A.values();

    double worst = 0.0;
    for (std::size_t i = 0; i < m; ++i) {
        double ad = 0.0;
        for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
            ad += av[k] * d[static_cast<std::size_t>(ci[k])];
        }
        const auto sense = p.constraints[i].sense;
        if (sense == model::ConstraintSense::LE) {
            worst = std::max(worst, ad - tol.feasibility_tol());
        } else if (sense == model::ConstraintSense::GE) {
            worst = std::max(worst, -ad - tol.feasibility_tol());
        } else {
            worst = std::max(worst, std::abs(ad) - tol.feasibility_tol());
        }
    }

    for (std::size_t j = 0; j < n; ++j) {
        const double dj = d[j];
        const double lb = p.variables[j].lower_bound;
        const double ub = p.variables[j].upper_bound;
        if (dj > 0.0 && !is_inf(ub)) {
            r.message = "direction increases a variable with a finite upper bound";
            return r;
        }
        if (dj < 0.0 && !is_inf(lb)) {
            r.message = "direction decreases a variable with a finite lower bound";
            return r;
        }
        if (std::abs(dj) > 1e308) {
            r.message = "direction entry is not finite";
            return r;
        }
    }

    double cd = 0.0;
    for (std::size_t j = 0; j < n; ++j) {
        cd += p.variables[j].objective_coeff * d[j];
    }
    r.objective_direction = cd;

    const bool improving = (p.obj_sense == model::ObjectiveSense::MINIMIZE && cd < -tol.optimality_tol()) ||
                           (p.obj_sense == model::ObjectiveSense::MAXIMIZE && cd > tol.optimality_tol());
    r.violation = worst;
    if (worst <= 0.0 && improving) {
        r.valid = true;
        r.message = "verified recession direction";
    } else if (worst <= 0.0) {
        r.message = "direction is not objective improving";
    } else {
        r.message = "direction violates a constraint";
    }
    return r;
}

CertificateResult CertificateAnalyzer::analyze(const model::Problem& problem) {
    CertificateResult result;

    // Rowize the problem: every variable is shifted/complemented into a
    // nonnegative auxiliary variable, and every finite upper bound is
    // materialized as an explicit <= row. The resulting model is pure
    // standard form (all variables >= 0, no finite variable bounds), which is
    // exactly the class the simplex primal path handles correctly. Bounds are
    // re-expressed as rows, so row-order is preserved: the first m rows are the
    // user rows, the remaining rows encode upper bounds.
    model::ProblemBuilder rb("cert_rowized");
    const std::size_t m = problem.constraints.size();
    const std::size_t n = problem.variables.size();

    std::vector<std::vector<std::pair<std::size_t, double>>> var_terms(n);
    std::vector<double> var_const(n, 0.0);

    for (std::size_t j = 0; j < n; ++j) {
        const double lb = problem.variables[j].lower_bound;
        const double ub = problem.variables[j].upper_bound;
        const bool lo = is_inf(lb);
        const bool hi = is_inf(ub);
        const std::string base = problem.variables[j].name.empty()
                                     ? "x" + std::to_string(j)
                                     : problem.variables[j].name;
        if (!lo) {
            std::size_t v = rb.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                            model::VarType::CONTINUOUS, base + "_v");
            var_terms[j] = {{v, 1.0}};
            var_const[j] = lb;
            (void)hi;
        } else if (hi) {
            std::size_t p = rb.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                            model::VarType::CONTINUOUS, base + "_p");
            std::size_t q = rb.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                            model::VarType::CONTINUOUS, base + "_q");
            var_terms[j] = {{p, 1.0}, {q, -1.0}};
        } else {
            std::size_t t = rb.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                            model::VarType::CONTINUOUS, base + "_t");
            var_terms[j] = {{t, -1.0}};
            var_const[j] = ub;
        }
    }

    const auto& A = problem.constraint_matrix;
    const auto& rp = A.row_ptr();
    const auto& ci = A.col_indices();
    const auto& av = A.values();

    for (std::size_t i = 0; i < m; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        double rhs = problem.constraints[i].rhs;
        for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
            const std::size_t j = static_cast<std::size_t>(ci[k]);
            const double aij = av[k];
            if (aij == 0.0) continue;
            rhs -= aij * var_const[j];
            for (const auto& [v, s] : var_terms[j]) {
                coeffs.emplace_back(v, s * aij);
            }
        }
        rb.add_constraint(coeffs, problem.constraints[i].sense, rhs, problem.constraints[i].name);
    }

    double obj_const = problem.obj_offset;
    std::vector<std::pair<std::size_t, double>> obj_coeffs;
    for (std::size_t j = 0; j < n; ++j) {
        const double c = problem.variables[j].objective_coeff;
        if (c == 0.0 && var_terms[j].empty()) continue;
        for (const auto& [v, s] : var_terms[j]) {
            obj_coeffs.emplace_back(v, s * c);
        }
        obj_const += var_const[j] * c;
    }
    rb.set_objective(obj_coeffs, problem.obj_sense);
    rb.get_problem().obj_offset = obj_const;

    // Materialize finite upper bounds as explicit LE rows on the nonnegative
    // rep variable once per original variable. Variables represented as p-q or
    // ub-t already cover the box via nonnegativity, so only lb-shifted reps
    // with a finite upper bound need a row.
    for (std::size_t j = 0; j < n; ++j) {
        const double lb = problem.variables[j].lower_bound;
        const double ub = problem.variables[j].upper_bound;
        if (is_inf(lb) || is_inf(ub)) continue;
        if (var_terms[j].size() != 1) continue;   // only the lb+rep form
        std::size_t v = var_terms[j][0].first;
        std::string name = problem.variables[j].name.empty()
                               ? "x" + std::to_string(j)
                               : problem.variables[j].name;
        rb.add_constraint({{v, 1.0}}, model::ConstraintSense::LE, ub - lb, "_ub_" + name);
    }

    model::Problem T = rb.build();
    const std::size_t mT = T.constraints.size();

    lp::SimplexSolver probe(tol_, options_);
    lp::SimplexResult status = probe.solve(T);
    result.status = status.status;

    if (result.status == model::ProblemStatus::INFEASIBLE) {
        // Build the classical Farkas LP on the rowized standard-form model using
        // ONLY nonnegative variables (column-sign complements for GE/EQ rows):
        //   y_i >= 0  for LE rows          (direct)
        //   y_i <= 0  for GE rows, y_i=-u  (complemented)
        //   y_i free  for EQ rows, y=p-q   (split)
        //   w_j >= 0
        // Stationarity:  -sum_i y_i a'_ij - w_j <= 0  (one row per aux variable)
        // Normalization:  sum_i y_i b'_i = -1
        // A zero optimum certifies infeasibility of the rowized polyhedron.
        model::ProblemBuilder builder("farkas_lp");
        enum class YRep { POS, NEG, SPLIT };
        std::vector<YRep> y_rep(mT);
        std::vector<std::size_t> y_plus(mT), y_minus(mT);
        for (std::size_t i = 0; i < mT; ++i) {
            switch (T.constraints[i].sense) {
                case model::ConstraintSense::LE:
                    y_rep[i] = YRep::POS;
                    y_plus[i] = builder.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                                     model::VarType::CONTINUOUS, "y" + std::to_string(i));
                    break;
                case model::ConstraintSense::GE:
                    y_rep[i] = YRep::NEG;
                    y_plus[i] = builder.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                                     model::VarType::CONTINUOUS, "u" + std::to_string(i));
                    break;
                case model::ConstraintSense::EQ:
                    y_rep[i] = YRep::SPLIT;
                    y_plus[i] = builder.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                                     model::VarType::CONTINUOUS, "p" + std::to_string(i));
                    y_minus[i] = builder.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                                      model::VarType::CONTINUOUS, "q" + std::to_string(i));
                    break;
            }
        }
        const std::size_t nT = T.variables.size();
        std::vector<std::size_t> w_idx(nT);
        for (std::size_t j = 0; j < nT; ++j) {
            w_idx[j] = builder.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                            model::VarType::CONTINUOUS, "w" + std::to_string(j));
        }

        auto cols = columns_of(T.constraint_matrix, nT);
        for (std::size_t j = 0; j < nT; ++j) {
            std::vector<std::pair<std::size_t, double>> coeffs;
            coeffs.reserve(cols[j].size() + 1);
            for (const auto& [i, aij] : cols[j]) {
                if (y_rep[i] == YRep::POS) {
                    coeffs.emplace_back(y_plus[i], -aij);
                } else if (y_rep[i] == YRep::NEG) {
                    coeffs.emplace_back(y_plus[i], aij);
                } else {
                    coeffs.emplace_back(y_plus[i], -aij);
                    coeffs.emplace_back(y_minus[i], aij);
                }
            }
            coeffs.emplace_back(w_idx[j], -1.0);
            builder.add_constraint(coeffs, model::ConstraintSense::LE, 0.0, "st_j" + std::to_string(j));
        }

        std::vector<std::pair<std::size_t, double>> norm;
        for (std::size_t i = 0; i < mT; ++i) {
            const double b_i = T.constraints[i].rhs;
            if (y_rep[i] == YRep::POS) {
                norm.emplace_back(y_plus[i], b_i);
            } else if (y_rep[i] == YRep::NEG) {
                norm.emplace_back(y_plus[i], -b_i);
            } else {
                norm.emplace_back(y_plus[i], b_i);
                norm.emplace_back(y_minus[i], -b_i);
            }
        }
        builder.add_constraint(norm, model::ConstraintSense::EQ, -1.0, "norm");

        std::vector<std::pair<std::size_t, double>> obj;
        obj.reserve(nT);
        for (std::size_t j = 0; j < nT; ++j) {
            obj.emplace_back(w_idx[j], 1.0);
        }
        builder.set_objective(obj, model::ObjectiveSense::MINIMIZE);
        model::Problem lp = builder.build();

        lp::SimplexOptions aux_opt = options_;
        aux_opt.algorithm = lp::SimplexAlgorithm::PRIMAL;
        lp::SimplexSolver solver(tol_, aux_opt);
        lp::SimplexResult r = solver.solve(lp);
        if (r.status == model::ProblemStatus::OPTIMAL) {
            double wsum = 0.0;
            for (std::size_t j = 0; j < nT; ++j) {
                if (w_idx[j] < r.primal.size()) wsum += r.primal[w_idx[j]];
            }
            if (wsum <= std::max(tol_.feasibility_tol(), 1e-7) *
                              std::max(1.0, static_cast<double>(nT))) {
                std::vector<double> y(mT, 0.0);
                for (std::size_t i = 0; i < mT; ++i) {
                    const double pv = (y_plus[i] < r.primal.size()) ? r.primal[y_plus[i]] : 0.0;
                    if (y_rep[i] == YRep::POS) {
                        y[i] = pv;
                    } else if (y_rep[i] == YRep::NEG) {
                        y[i] = -pv;
                    } else {
                        const double qv = (y_minus[i] < r.primal.size()) ? r.primal[y_minus[i]] : 0.0;
                        y[i] = pv - qv;
                    }
                }
                std::vector<double> y_user(y.begin(), y.begin() + m);
                result.farkas = verify_farkas(problem, y_user, tol_);
                if (result.farkas.valid) {
                    result.message = "verified Farkas certificate (row multipliers)";
                } else {
                    // Fall back to the full rowized certificate, which includes
                    // multipliers for the materialized bound rows.
                    result.farkas = verify_farkas(T, y, tol_);
                    if (result.farkas.valid) {
                        result.message = "verified Farkas certificate (expanded form)";
                    } else {
                        result.message = "auxiliary Farkas LP gave an unverifiable certificate";
                    }
                }
            } else {
                result.message = "auxiliary Farkas LP could not produce a zero-residual certificate";
            }
        } else {
            result.message = "auxiliary Farkas LP did not solve to optimality";
        }
        return result;
    }

    if (result.status == model::ProblemStatus::UNBOUNDED) {
        // Build a recession-direction LP on the rowized standard-form model with
        // only nonnegative variables. All aux variables are >= 0, so the
        // direction is bounded by the cone rows; the direction d'_j is >= 0
        // here and can be mapped back to the original variables.
        model::ProblemBuilder builder("ray_lp");
        const std::size_t nT = T.variables.size();
        std::vector<std::size_t> d_idx(nT);
        for (std::size_t j = 0; j < nT; ++j) {
            d_idx[j] = builder.add_variable(-std::numeric_limits<double>::infinity(),
                                            std::numeric_limits<double>::infinity(),
                                            model::VarType::CONTINUOUS, "d" + std::to_string(j));
        }

        std::vector<std::size_t> w_idx;
        const auto& TA = T.constraint_matrix;
        const auto& Trp = TA.row_ptr();
        const auto& Tci = TA.col_indices();
        const auto& Tav = TA.values();
        auto add_residual_row = [&](std::vector<std::pair<std::size_t, double>>& coeffs) {
            std::size_t w = builder.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                                 model::VarType::CONTINUOUS,
                                                 "w" + std::to_string(w_idx.size()));
            coeffs.emplace_back(w, -1.0);
            w_idx.push_back(w);
            builder.add_constraint(coeffs, model::ConstraintSense::LE, 0.0, "cone");
        };

        for (std::size_t i = 0; i < mT; ++i) {
            std::vector<std::pair<std::size_t, double>> ck;
            for (std::size_t k = Trp[i]; k < Trp[i + 1]; ++k) {
                ck.emplace_back(d_idx[static_cast<std::size_t>(Tci[k])], Tav[k]);
            }
            const auto sense = T.constraints[i].sense;
            if (sense == model::ConstraintSense::LE) {
                add_residual_row(ck);
            } else if (sense == model::ConstraintSense::GE) {
                for (auto& p : ck) p.second = -p.second;
                add_residual_row(ck);
            } else {
                add_residual_row(ck);
                for (auto& p : ck) p.second = -p.second;
                add_residual_row(ck);
            }
        }

        const double norm_rhs = (T.obj_sense == model::ObjectiveSense::MINIMIZE) ? -1.0 : 1.0;
        std::vector<std::pair<std::size_t, double>> norm;
        norm.reserve(nT);
        for (std::size_t j = 0; j < nT; ++j) {
            norm.emplace_back(d_idx[j], T.variables[j].objective_coeff);
        }
        builder.add_constraint(norm, model::ConstraintSense::EQ, norm_rhs, "norm");

        std::vector<std::pair<std::size_t, double>> obj;
        for (std::size_t w = 0; w < w_idx.size(); ++w) {
            obj.emplace_back(w_idx[w], 1.0);
        }
        builder.set_objective(obj, model::ObjectiveSense::MINIMIZE);
        model::Problem lp = builder.build();

        lp::SimplexOptions aux_opt = options_;
        aux_opt.algorithm = lp::SimplexAlgorithm::PRIMAL;
        lp::SimplexSolver solver(tol_, aux_opt);
        lp::SimplexResult r = solver.solve(lp);
        if (r.status == model::ProblemStatus::OPTIMAL) {
            double wsum = 0.0;
            for (std::size_t w = 0; w < w_idx.size(); ++w) {
                if (w_idx[w] < r.primal.size()) wsum += r.primal[w_idx[w]];
            }
            if (wsum <= std::max(tol_.feasibility_tol(), 1e-7) *
                              std::max(1.0, static_cast<double>(w_idx.size()))) {
                std::vector<double> dT(nT, 0.0);
                for (std::size_t j = 0; j < nT; ++j) {
                    if (d_idx[j] < r.primal.size()) dT[j] = r.primal[d_idx[j]];
                }
                // Map the aux-variable direction back to the original variables.
                std::vector<double> d(n, 0.0);
                for (std::size_t j = 0; j < n; ++j) {
                    if (var_terms[j].empty()) continue;
                    if (var_terms[j].size() == 1) {
                        d[j] = var_terms[j][0].second * dT[var_terms[j][0].first];
                    } else {
                        d[j] = var_terms[j][0].second * dT[var_terms[j][0].first] +
                               var_terms[j][1].second * dT[var_terms[j][1].first];
                    }
                }
                result.ray = verify_ray(problem, d, tol_);
                if (!result.ray.valid) {
                    result.message = "auxiliary ray LP gave an unverifiable direction";
                } else {
                    result.message = "verified recession direction";
                }
            } else {
                result.message = "auxiliary ray LP could not produce a recession direction";
            }
        } else {
            result.message = "auxiliary ray LP did not solve to optimality";
        }
        return result;
    }

    if (result.status == model::ProblemStatus::OPTIMAL ||
        result.status == model::ProblemStatus::SUBOPTIMAL) {
        result.message = "model is feasible; no certificate needed";
    } else if (result.message.empty()) {
        result.message = "no certificate computed for status " +
                         std::to_string(static_cast<int>(result.status));
    }

    return result;
}

} // namespace hypernova::validation