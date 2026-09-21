#include "node_presolve.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hypernova::milp {

namespace {

struct RowData {
    std::vector<std::size_t> cols;
    std::vector<double> coeffs;
    model::ConstraintSense sense;
    double rhs = 0.0;
};

double contrib_min(double a, double lb, double ub) {
    return a > 0.0 ? a * lb : a * ub;
}

double contrib_max(double a, double lb, double ub) {
    return a > 0.0 ? a * ub : a * lb;
}

bool is_integer_var(model::VarType t) {
    return t == model::VarType::INTEGER || t == model::VarType::BINARY;
}

} // namespace

NodePresolveResult NodePresolver::tighten(const model::Problem& problem,
                                          const std::vector<BoundChange>& node_bounds) const {
    NodePresolveResult result;
    const std::size_t n = problem.variables.size();
    const std::size_t m = problem.constraints.size();
    const double eps = tol_.feasibility_tol();

    // NB: the feasible region of a QP/MIQP node is the same linear set as its
    // LP relaxation (quadratic terms only shape the objective), so the
    // feasibility detection and bound tightening below remain valid for
    // models with quadratic terms. No early-return guard is applied.

    std::vector<double> L(n), U(n), L0(n), U0(n);
    auto orig_ub = [&](std::size_t j) { return problem.variables[j].upper_bound; };
    auto orig_lb = [&](std::size_t j) { return problem.variables[j].lower_bound; };

    for (std::size_t j = 0; j < n; ++j) {
        double lb = orig_lb(j);
        double ub = orig_ub(j);
        for (const auto& bc : node_bounds) {
            if (bc.var == j) {
                if (bc.is_lower) {
                    if (bc.value > lb) lb = bc.value;
                } else {
                    if (bc.value < ub) ub = bc.value;
                }
            }
        }
        L0[j] = lb;
        U0[j] = ub;
        L[j] = lb;
        U[j] = ub;
    }

    if (m == 0 || n == 0) {
        result.message = "no rows to propagate";
        return result;
    }

    const auto& A = problem.constraint_matrix;
    std::vector<RowData> rows(m);
    if (A.order() == model::StorageOrder::CSR) {
        for (std::size_t i = 0; i < m; ++i) {
            RowData& rd = rows[i];
            rd.sense = problem.constraints[i].sense;
            rd.rhs = problem.constraints[i].rhs;
            rd.cols.reserve(A.row_ptr()[i + 1] - A.row_ptr()[i]);
            rd.coeffs.reserve(A.row_ptr()[i + 1] - A.row_ptr()[i]);
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                if (A.values()[k] != 0.0) {
                    rd.cols.push_back(A.col_indices()[k]);
                    rd.coeffs.push_back(A.values()[k]);
                }
            }
        }
    }

    const unsigned max_passes = 20;
    for (unsigned pass = 0; pass < max_passes; ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < m; ++i) {
            const RowData& rd = rows[i];
            if (rd.cols.empty()) {
                // Empty row: check the constant is satisfied.
                if (rd.sense == model::ConstraintSense::LE && rd.rhs < -eps) {
                    result.feasible = false;
                    result.message = "infeasible empty row";
                    return result;
                }
                if (rd.sense == model::ConstraintSense::GE && rd.rhs > eps) {
                    result.feasible = false;
                    result.message = "infeasible empty row";
                    return result;
                }
                if (rd.sense == model::ConstraintSense::EQ && std::abs(rd.rhs) > eps) {
                    result.feasible = false;
                    result.message = "infeasible empty row";
                    return result;
                }
                continue;
            }

            // Row activity bounds under the current (valid) variable bounds.
            double min_act = 0.0;
            double max_act = 0.0;
            int min_inf_count = 0;
            int max_inf_count = 0;
            for (std::size_t t = 0; t < rd.cols.size(); ++t) {
                const double c = rd.coeffs[t];
                const std::size_t j = rd.cols[t];
                const double cmin = contrib_min(c, L[j], U[j]);
                const double cmax = contrib_max(c, L[j], U[j]);
                if (std::isinf(cmin)) ++min_inf_count; else min_act += cmin;
                if (std::isinf(cmax)) ++max_inf_count; else max_act += cmax;
            }

            switch (rd.sense) {
                case model::ConstraintSense::LE:
                    if (min_inf_count == 0 && min_act > rd.rhs + eps) {
                        result.feasible = false;
                        result.message = "row minimum activity exceeds LE rhs";
                        return result;
                    }
                    break;
                case model::ConstraintSense::GE:
                    if (max_inf_count == 0 && max_act < rd.rhs - eps) {
                        result.feasible = false;
                        result.message = "row maximum activity falls below GE rhs";
                        return result;
                    }
                    break;
                case model::ConstraintSense::EQ:
                    if ((min_inf_count == 0 && min_act > rd.rhs + eps) || (max_inf_count == 0 && max_act < rd.rhs - eps)) {
                        result.feasible = false;
                        result.message = "row activity cannot reach EQ rhs";
                        return result;
                    }
                    break;
            }

            // Bound tightening from the current row.
            for (std::size_t t = 0; t < rd.cols.size(); ++t) {
                const double c = rd.coeffs[t];
                const std::size_t j = rd.cols[t];
                const double bmin_j = contrib_min(c, L[j], U[j]);
                const double bmax_j = contrib_max(c, L[j], U[j]);

                if (rd.sense == model::ConstraintSense::LE || rd.sense == model::ConstraintSense::EQ) {
                    bool can_propagate = false;
                    double base_min_excl = 0.0;
                    if (min_inf_count == 0) {
                        can_propagate = true;
                        base_min_excl = min_act - bmin_j;
                    } else if (min_inf_count == 1 && std::isinf(bmin_j)) {
                        can_propagate = true;
                        base_min_excl = min_act;
                    }
                    if (can_propagate) {
                        const double num = rd.rhs - base_min_excl;
                        if (c > 0.0) {
                            const double new_ub = num / c;
                            if (new_ub < U[j] - eps) { U[j] = std::max(L[j], new_ub); changed = true; }
                        } else if (c < 0.0) {
                            const double new_lb = num / c; // c < 0
                            if (new_lb > L[j] + eps) { L[j] = std::min(U[j], new_lb); changed = true; }
                        }
                    }
                }
                if (rd.sense == model::ConstraintSense::GE || rd.sense == model::ConstraintSense::EQ) {
                    bool can_propagate = false;
                    double base_max_excl = 0.0;
                    if (max_inf_count == 0) {
                        can_propagate = true;
                        base_max_excl = max_act - bmax_j;
                    } else if (max_inf_count == 1 && std::isinf(bmax_j)) {
                        can_propagate = true;
                        base_max_excl = max_act;
                    }
                    if (can_propagate) {
                        const double num = rd.rhs - base_max_excl;
                        if (c > 0.0) {
                            const double new_lb = num / c;
                            if (new_lb > L[j] + eps) { L[j] = std::min(U[j], new_lb); changed = true; }
                        } else if (c < 0.0) {
                            const double new_ub = num / c; // c < 0
                            if (new_ub < U[j] - eps) { U[j] = std::max(L[j], new_ub); changed = true; }
                        }
                    }
                }
            }
        }

        // Round integer bounds and report early infeasibility from integer gaps.
        for (std::size_t j = 0; j < n; ++j) {
            if (is_integer_var(problem.variables[j].type)) {
                if (std::isfinite(L[j])) {
                    const double rounded_lb = std::ceil(L[j] - eps);
                    if (rounded_lb > L[j] + eps) { L[j] = rounded_lb; changed = true; }
                }
                if (std::isfinite(U[j])) {
                    const double rounded_ub = std::floor(U[j] + eps);
                    if (rounded_ub < U[j] - eps) { U[j] = rounded_ub; changed = true; }
                }
                if (problem.variables[j].type == model::VarType::BINARY) {
                    if (L[j] > 1.0 + eps) { result.feasible = false; result.message = "binary bound exceeds 1"; return result; }
                    if (U[j] < -eps) { result.feasible = false; result.message = "binary bound below 0"; return result; }
                    if (L[j] < 0.0 && std::isfinite(U[j]) && U[j] <= 0.0 - eps) { U[j] = 0.0; }
                    if (U[j] > 1.0 && std::isfinite(L[j]) && L[j] >= 1.0 + eps) { L[j] = 1.0; }
                }
                if (std::isfinite(L[j]) && std::isfinite(U[j]) && U[j] < L[j] - eps) {
                    result.feasible = false;
                    result.message = "integer bound conflict";
                    return result;
                }
            }
        }

        ++result.passes;
        if (!changed) break;
    }

    // Collect tightenings relative to the original node-effective bounds.
    for (std::size_t j = 0; j < n; ++j) {
        if (L[j] > L0[j] + eps) result.tightened_lb.push_back({j, L[j]});
        if (U[j] < U0[j] - eps) result.tightened_ub.push_back({j, U[j]});
    }
    result.message = result.tightened_lb.empty() && result.tightened_ub.empty()
                         ? "no tightening found"
                         : "bounds tightened";
    return result;
}

} // namespace hypernova::milp