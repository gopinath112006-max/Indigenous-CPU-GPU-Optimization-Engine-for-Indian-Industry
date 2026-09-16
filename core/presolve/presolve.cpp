#include "presolve.hpp"
#include "../numerical/scaling.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <queue>

namespace hypernova::presolve {

Presolver::Presolver(const PresolveOptions& options)
    : options_(options), tol_(numerical::ToleranceConfig::industrial_defaults()) {}

PresolveResult Presolver::presolve(const model::Problem& problem) {
    auto start_time = std::chrono::high_resolution_clock::now();

    PresolveResult result;
    result.row_mapping.resize(problem.constraints.size());
    result.col_mapping.resize(problem.variables.size());
    std::iota(result.row_mapping.begin(), result.row_mapping.end(), 0);
    std::iota(result.col_mapping.begin(), result.col_mapping.end(), 0);

    std::size_t nrows = problem.constraints.size();
    std::size_t ncols = problem.variables.size();

    std::vector<bool> row_keep(nrows, true);
    std::vector<bool> col_keep(ncols, true);

    std::vector<double> lb(ncols), ub(ncols);
    for (std::size_t j = 0; j < ncols; ++j) {
        lb[j] = problem.variables[j].lower_bound;
        ub[j] = problem.variables[j].upper_bound;
    }

    int passes = 0;
    bool changed = true;

    while (changed && passes < options_.max_passes) {
        changed = false;
        ++passes;

        if (options_.fix_variables) {
            std::vector<std::pair<std::size_t, std::pair<std::size_t, double>>> fixed;
            fix_variables(problem, lb, ub, fixed);
            for (const auto& [idx, val_pair] : fixed) {
                if (col_keep[idx]) {
                    col_keep[idx] = false;
                    result.fixed_variables.push_back({idx, val_pair});
                    result.removed_cols.push_back(idx);
                    changed = true;
                }
            }
        }

        if (options_.remove_singletons) {
            std::vector<bool> row_singleton(nrows, false);
            std::vector<bool> col_singleton(ncols, false);
            find_singletons(problem, row_singleton, col_singleton);

            std::vector<std::size_t> col_counts(ncols, 0);
            const auto& A = problem.constraint_matrix;
            if (A.order() == model::StorageOrder::CSR) {
                for (std::size_t i = 0; i < nrows; ++i) {
                    for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                        col_counts[A.col_indices()[k]]++;
                    }
                }
            }

            for (std::size_t i = 0; i < nrows; ++i) {
                if (!row_singleton[i] || !row_keep[i]) continue;
                if (A.order() != model::StorageOrder::CSR) continue;
                std::size_t nnz = A.row_ptr()[i + 1] - A.row_ptr()[i];
                if (nnz != 1) continue;
                std::size_t j = A.col_indices()[A.row_ptr()[i]];
                double a = A.values()[A.row_ptr()[i]];
                if (!col_keep[j] || std::abs(a) < 1e-15) continue;
                const bool exclusive = col_counts[j] == 1;
                const double b = problem.constraints[i].rhs;
                double solved = b / a;

                if (problem.constraints[i].sense == model::ConstraintSense::EQ) {
                    if (lb[j] - tol_.feasibility_tol() <= solved &&
                        solved <= ub[j] + tol_.feasibility_tol()) {
                        col_keep[j] = false;
                        row_keep[i] = false;
                        lb[j] = ub[j] = solved;
                        std::size_t flag = problem.variables[j].type == model::VarType::BINARY ? 0 : 1;
                        result.fixed_variables.push_back({j, {flag, solved}});
                        result.removed_cols.push_back(j);
                        result.removed_rows.push_back(i);
                        result.dropped_singletons.push_back(
                            {i, j, a, b, model::ConstraintSense::EQ, b / a});
                        changed = true;
                    }
                } else {
                    double new_lb = lb[j];
                    double new_ub = ub[j];
                    if (problem.constraints[i].sense == model::ConstraintSense::LE) {
                        if (a > 0.0) new_ub = solved;
                        else new_lb = solved;
                    } else {  // GE
                        if (a > 0.0) new_lb = solved;
                        else new_ub = solved;
                    }
                    if (new_ub + tol_.feasibility_tol() < lb[j] ||
                        new_lb - tol_.feasibility_tol() > ub[j]) {
                        continue;
                    }
                    row_keep[i] = false;
                    result.removed_rows.push_back(i);
                    result.dropped_singletons.push_back(
                        {i, j, a, b, problem.constraints[i].sense, b / a});
                    if (new_lb > lb[j] + tol_.feasibility_tol() ||
                        new_ub < ub[j] - tol_.feasibility_tol()) {
                        if (new_lb > new_ub) new_lb = new_ub = (new_lb + new_ub) / 2.0;
                        lb[j] = new_lb;
                        ub[j] = new_ub;
                        result.tightened_bounds.push_back({j, {lb[j], ub[j]}});
                    }

                    if (options_.dual_reductions && exclusive && col_keep[j]) {
                        const double c = problem.variables[j].objective_coeff;
                        if (c != 0.0) {
                            const bool lower_side =
                                (problem.obj_sense == model::ObjectiveSense::MINIMIZE) == (c > 0.0);
                            if (problem.variables[j].type == model::VarType::CONTINUOUS) {
                                const double target = lower_side ? lb[j] : ub[j];
                                if (std::isfinite(target)) {
                                    col_keep[j] = false;
                                    lb[j] = ub[j] = target;
                                    result.fixed_variables.push_back({j, {1, target}});
                                    result.removed_cols.push_back(j);
                                    changed = true;
                                }
                            } else {
                                const double v = lower_side ? std::ceil(lb[j] - 1e-9)
                                                            : std::floor(ub[j] + 1e-9);
                                if (lb[j] - tol_.feasibility_tol() <= v &&
                                    v <= ub[j] + tol_.feasibility_tol()) {
                                    col_keep[j] = false;
                                    lb[j] = ub[j] = v;
                                    std::size_t flag =
                                        problem.variables[j].type == model::VarType::BINARY ? 0 : 1;
                                    result.fixed_variables.push_back({j, {flag, v}});
                                    result.removed_cols.push_back(j);
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (options_.tighten_bounds) {
            std::vector<std::pair<std::size_t, std::pair<double, double>>> tightened;
            tighten_bounds(problem, lb, ub, tightened);
            for (const auto& [idx, bounds] : tightened) {
                if (col_keep[idx]) {
                    result.tightened_bounds.push_back({idx, bounds});
                    changed = true;
                }
            }
        }

        if (options_.dual_reductions) {
            for (std::size_t j = 0; j < ncols; ++j) {
                if (!col_keep[j]) continue;
                const auto& var = problem.variables[j];
                if (var.type == model::VarType::CONTINUOUS) continue;
                const double lo = std::ceil(lb[j] - 1e-9);
                const double hi = std::floor(ub[j] + 1e-9);
                if (lo > hi) continue;  // integer-infeasible: left for the solver
                if (lo == hi) {
                    col_keep[j] = false;
                    lb[j] = ub[j] = lo;
                    std::size_t flag = var.type == model::VarType::BINARY ? 0 : 1;
                    result.fixed_variables.push_back({j, {flag, lo}});
                    result.removed_cols.push_back(j);
                    changed = true;
                }
            }
        }

        if (options_.remove_redundant_rows || options_.remove_redundant_cols) {
            std::vector<bool> row_redundant(nrows, false);
            std::vector<bool> col_redundant(ncols, false);
            find_redundant_rows(problem, row_redundant, col_redundant);

            for (std::size_t i = 0; i < nrows; ++i) {
                if (row_redundant[i] && row_keep[i]) {
                    row_keep[i] = false;
                    result.removed_rows.push_back(i);
                    changed = true;
                }
            }
            for (std::size_t j = 0; j < ncols; ++j) {
                if (col_redundant[j] && col_keep[j]) {
                    col_keep[j] = false;
                    result.removed_cols.push_back(j);
                    changed = true;
                }
            }
        }
    }

    result.reduced_problem = build_reduced_problem(problem, row_keep, col_keep, lb, ub);
    if (options_.coefficient_strengthening) {
        apply_coefficient_strengthening(result.reduced_problem);
    }
    result.passes = passes;

    auto end_time = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return result;
}

void Presolver::find_singletons(const model::Problem& prob,
                                 std::vector<bool>& row_singleton,
                                 std::vector<bool>& /*col_singleton*/) const {
    const auto& A = prob.constraint_matrix;

    if (A.order() == model::StorageOrder::CSR) {
        std::vector<std::size_t> col_counts(A.cols(), 0);
        for (std::size_t i = 0; i < A.rows(); ++i) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                col_counts[A.col_indices()[k]]++;
            }
        }
        for (std::size_t i = 0; i < A.rows(); ++i) {
            std::size_t nnz = A.row_ptr()[i + 1] - A.row_ptr()[i];
            if (nnz == 1) {
                std::size_t j = A.col_indices()[A.row_ptr()[i]];
                bool exclusive = col_counts[j] == 1;
                bool equality = prob.constraints[i].sense == model::ConstraintSense::EQ;
                row_singleton[i] = exclusive || !equality;
            }
        }
    }
}

void Presolver::tighten_bounds(const model::Problem& prob,
                                std::vector<double>& lb,
                                std::vector<double>& ub,
                                std::vector<std::pair<std::size_t, std::pair<double, double>>>& tightened) const {
    const auto& A = prob.constraint_matrix;

    for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
        const auto& con = prob.constraints[i];

        if (A.order() != model::StorageOrder::CSR) continue;

        std::size_t start = A.row_ptr()[i];
        std::size_t end = A.row_ptr()[i + 1];

        double min_activity = 0.0;
        double max_activity = 0.0;
        for (std::size_t k = start; k < end; ++k) {
            std::size_t col = A.col_indices()[k];
            double coeff = A.values()[k];
            if (coeff > 0) {
                min_activity += coeff * lb[col];
                max_activity += coeff * ub[col];
            } else {
                min_activity += coeff * ub[col];
                max_activity += coeff * lb[col];
            }
        }

        if (con.sense == model::ConstraintSense::LE) {
            if (max_activity > con.rhs + tol_.feasibility_tol()) {
                for (std::size_t k = start; k < end; ++k) {
                    std::size_t col = A.col_indices()[k];
                    double coeff = A.values()[k];

                    if (coeff > 0.0 && std::isfinite(ub[col])) {
                        double others_min = min_activity - coeff * lb[col];
                        double new_ub = (con.rhs - others_min) / coeff;
                        if (new_ub < ub[col] - tol_.feasibility_tol()) {
                            tightened.push_back({col, {lb[col], new_ub}});
                            ub[col] = new_ub;
                        }
                    } else if (coeff < 0.0 && std::isfinite(lb[col])) {
                        double others_min = min_activity - coeff * ub[col];
                        double new_lb = (con.rhs - others_min) / coeff;
                        if (new_lb > lb[col] + tol_.feasibility_tol()) {
                            tightened.push_back({col, {new_lb, ub[col]}});
                            lb[col] = new_lb;
                        }
                    }
                }
            }
        } else if (con.sense == model::ConstraintSense::GE) {
            if (min_activity < con.rhs - tol_.feasibility_tol()) {
                for (std::size_t k = start; k < end; ++k) {
                    std::size_t col = A.col_indices()[k];
                    double coeff = A.values()[k];

                    if (coeff > 0.0 && std::isfinite(lb[col])) {
                        double others_max = max_activity - coeff * ub[col];
                        double new_lb = (con.rhs - others_max) / coeff;
                        if (new_lb > lb[col] + tol_.feasibility_tol()) {
                            tightened.push_back({col, {new_lb, ub[col]}});
                            lb[col] = new_lb;
                        }
                    } else if (coeff < 0.0 && std::isfinite(ub[col])) {
                        double others_max = max_activity - coeff * lb[col];
                        double new_ub = (con.rhs - others_max) / coeff;
                        if (new_ub < ub[col] - tol_.feasibility_tol()) {
                            tightened.push_back({col, {lb[col], new_ub}});
                            ub[col] = new_ub;
                        }
                    }
                }
            }
        }
    }
}

void Presolver::fix_variables(const model::Problem& prob,
                               const std::vector<double>& lb,
                               const std::vector<double>& ub,
                               std::vector<std::pair<std::size_t, std::pair<std::size_t, double>>>& fixed) const {
    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (ub[j] - lb[j] <= tol_.feasibility_tol()) {
            double fixed_val = (lb[j] + ub[j]) / 2.0;
            fixed.push_back({j, {prob.variables[j].type == model::VarType::BINARY ? 0 : 1, fixed_val}});
        }
    }
}

void Presolver::apply_coefficient_strengthening(model::Problem& prob) const {
    // Row GCD strengthening for all-integer rows. When every column in a row is
    // integer-typed and every coefficient is (near-)integral, the left-hand
    // side is always a multiple of g = gcd(|a_j|), so the row can be divided
    // by g and the RHS rounded down (LE) / up (GE). Equality rows are only
    // divided when the scaled RHS stays integral (a fractional scaled RHS would
    // make the row infeasible). Rows containing continuous variables are left
    // untouched.
    const double int_tol = 1e-9;
    auto& A = prob.constraint_matrix;
    if (A.order() != model::StorageOrder::CSR) return;

    for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
        std::size_t start = A.row_ptr()[i];
        std::size_t end = A.row_ptr()[i + 1];
        if (start == end) continue;

        long long g = 0;
        bool valid = true;
        for (std::size_t k = start; k < end; ++k) {
            std::size_t col = A.col_indices()[k];
            if (prob.variables[col].type == model::VarType::CONTINUOUS) {
                valid = false;
                break;
            }
            double v = std::abs(A.values()[k]);
            double vr = std::round(v);
            if (std::abs(v - vr) > int_tol ||
                vr > static_cast<double>(std::numeric_limits<long long>::max())) {
                valid = false;
                break;
            }
            g = std::gcd(g, static_cast<long long>(vr));
        }
        if (!valid || g <= 1) continue;

        const double rhs_div = prob.constraints[i].rhs / static_cast<double>(g);
        double new_rhs = rhs_div;

        if (prob.constraints[i].sense == model::ConstraintSense::EQ) {
            const double rounded = std::round(rhs_div);
            if (std::abs(rhs_div - rounded) > int_tol) continue;
            new_rhs = rounded;
        } else if (prob.constraints[i].sense == model::ConstraintSense::LE) {
            new_rhs = std::floor(rhs_div);
        } else {  // GE
            new_rhs = std::ceil(rhs_div);
        }

        for (std::size_t k = start; k < end; ++k) {
            A.mutable_values()[k] = A.values()[k] / static_cast<double>(g);
        }
        prob.constraints[i].rhs = new_rhs;
    }
}

void Presolver::find_redundant_rows(const model::Problem& prob,
                                     std::vector<bool>& row_redundant,
                                     std::vector<bool>& /*col_redundant*/) const {
    const auto& A = prob.constraint_matrix;

    if (A.order() != model::StorageOrder::CSR) return;

    for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
        if (A.row_ptr()[i + 1] - A.row_ptr()[i] != 0) continue;
        const double rhs = prob.constraints[i].rhs;
        bool satisfiable = true;
        switch (prob.constraints[i].sense) {
            case model::ConstraintSense::LE:
                satisfiable = rhs >= -tol_.feasibility_tol();
                break;
            case model::ConstraintSense::GE:
                satisfiable = rhs <= tol_.feasibility_tol();
                break;
            case model::ConstraintSense::EQ:
                satisfiable = std::abs(rhs) <= tol_.feasibility_tol();
                break;
        }
        if (satisfiable) row_redundant[i] = true;
    }
}

model::Problem Presolver::build_reduced_problem(const model::Problem& prob,
                                                 const std::vector<bool>& row_keep,
                                                 const std::vector<bool>& col_keep,
                                                 const std::vector<double>& new_lb,
                                                 const std::vector<double>& new_ub) const {
    model::ProblemBuilder builder;

    std::vector<std::size_t> row_map(prob.constraints.size(), std::numeric_limits<std::size_t>::max());
    std::vector<std::size_t> col_map(prob.variables.size(), std::numeric_limits<std::size_t>::max());

    std::size_t new_row_idx = 0;
    for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
        if (row_keep[i]) {
            row_map[i] = new_row_idx++;
        }
    }

    std::size_t new_col_idx = 0;
    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (col_keep[j]) {
            col_map[j] = new_col_idx++;
        }
    }

    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (col_keep[j]) {
            builder.add_variable(new_lb[j], new_ub[j], prob.variables[j].type, prob.variables[j].name);
        }
    }

    std::vector<std::vector<std::pair<std::size_t, double>>> new_matrix(new_row_idx);
    std::vector<double> rhs_shift(new_row_idx, 0.0);

    if (prob.constraint_matrix.order() == model::StorageOrder::CSR) {
        for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
            if (!row_keep[i]) continue;
            for (std::size_t k = prob.constraint_matrix.row_ptr()[i];
                 k < prob.constraint_matrix.row_ptr()[i + 1]; ++k) {
                std::size_t j = prob.constraint_matrix.col_indices()[k];
                if (col_keep[j]) {
                    new_matrix[row_map[i]].push_back({col_map[j], prob.constraint_matrix.values()[k]});
                } else if (std::abs(new_lb[j] - new_ub[j]) <= tol_.feasibility_tol()) {
                    rhs_shift[row_map[i]] -= prob.constraint_matrix.values()[k] * new_lb[j];
                }
            }
        }
    }

    for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
        if (!row_keep[i]) continue;
        builder.add_constraint(new_matrix[row_map[i]], prob.constraints[i].sense,
                               prob.constraints[i].rhs + rhs_shift[row_map[i]],
                               prob.constraints[i].name);
    }

    std::vector<std::pair<std::size_t, double>> obj_coeffs;
    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (col_keep[j] && prob.variables[j].objective_coeff != 0.0) {
            obj_coeffs.push_back({col_map[j], prob.variables[j].objective_coeff});
        }
    }
    builder.set_objective(obj_coeffs, prob.obj_sense);

    for (const auto& term : prob.quadratic_terms) {
        if (col_keep[term.row] && col_keep[term.col]) {
            builder.add_quadratic_term(col_map[term.row], col_map[term.col], term.coeff);
        }
    }

    for (const auto& sos : prob.sos_constraints) {
        std::vector<std::size_t> new_vars;
        for (std::size_t idx : sos.variable_indices) {
            if (col_keep[idx]) new_vars.push_back(col_map[idx]);
        }
        if (new_vars.size() >= 2) {
            if (sos.type == model::SOS::Type::SOS1) {
                builder.add_sos1(new_vars, sos.weights, sos.name);
            } else {
                builder.add_sos2(new_vars, sos.weights, sos.name);
            }
        }
    }

    return builder.build();
}

model::Solution Presolver::postsolve(const model::Problem& original,
                                     const model::Solution& reduced_solution,
                                     const PresolveResult& presolve_result) const {
    model::Solution solution = reduced_solution;

    std::size_t orig_cols = presolve_result.col_mapping.size();
    std::size_t orig_rows = presolve_result.row_mapping.size();
    solution.primal.assign(orig_cols, 0.0);
    solution.dual.assign(orig_rows, 0.0);
    solution.reduced_costs.assign(orig_cols, 0.0);

    for (const auto& [idx, val_pair] : presolve_result.fixed_variables) {
        solution.primal[idx] = val_pair.second;
    }

    std::vector<bool> dropped_row(orig_rows, false);
    for (std::size_t r : presolve_result.removed_rows) {
        if (r < orig_rows) dropped_row[r] = true;
    }
    std::size_t reduced_row = 0;
    for (std::size_t i = 0; i < orig_rows; ++i) {
        if (!dropped_row[i] && reduced_row < reduced_solution.dual.size()) {
            solution.dual[i] = reduced_solution.dual[reduced_row];
        }
        if (!dropped_row[i]) ++reduced_row;
    }

    std::size_t reduced_idx = 0;
    for (std::size_t j = 0; j < orig_cols; ++j) {
        bool is_fixed = false;
        for (const auto& [idx, _] : presolve_result.fixed_variables) {
            if (idx == j) { is_fixed = true; break; }
        }
        if (!is_fixed && reduced_idx < reduced_solution.primal.size()) {
            solution.primal[j] = reduced_solution.primal[reduced_idx];
            if (reduced_idx < reduced_solution.reduced_costs.size()) {
                solution.reduced_costs[j] = reduced_solution.reduced_costs[reduced_idx];
            }
            ++reduced_idx;
        }
    }

    // Reconstruct duals for dropped singleton rows: the row's multiplier can be
    // recovered from stationarity when the variable sits on the implied bound.
    // Multi-row interactions are left at 0 (no reliable unique split).
    if (orig_cols > 0 && !presolve_result.dropped_singletons.empty()) {
        std::vector<std::size_t> touches(orig_cols, 0);
        for (const auto& ds : presolve_result.dropped_singletons) {
            if (ds.col < orig_cols) ++touches[ds.col];
        }

        std::vector<double> kept_dual_sum(orig_cols, 0.0);
        const auto& A = original.constraint_matrix;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t i = 0; i < orig_rows && i < A.rows(); ++i) {
                if (dropped_row[i]) continue;
                for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                    std::size_t j = A.col_indices()[k];
                    if (j < orig_cols) kept_dual_sum[j] += A.values()[k] * solution.dual[i];
                }
            }
        }

        for (const auto& ds : presolve_result.dropped_singletons) {
            if (ds.row >= orig_rows || ds.col >= orig_cols) continue;
            if (touches[ds.col] != 1) continue;
            const double xj = solution.primal[ds.col];
            const double bound_scale = std::max(1.0, std::abs(ds.implied_bound));
            if (std::abs(xj - ds.implied_bound) > 1e-7 * bound_scale) continue;
            const double cprime =
                original.variables[ds.col].objective_coeff - kept_dual_sum[ds.col];
            solution.dual[ds.row] = cprime / ds.coeff;
        }
    }

    return solution;
}

} // namespace hypernova::presolve