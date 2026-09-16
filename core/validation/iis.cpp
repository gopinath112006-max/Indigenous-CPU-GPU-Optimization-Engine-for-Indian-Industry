#include "iis.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>

namespace hypernova::validation {

IISComputer::IISComputer(const numerical::ToleranceConfig& tol, const lp::SimplexOptions& options)
    : tol_(tol), options_(options) {}

bool IISComputer::is_infeasible(const model::Problem& candidate, std::size_t& solves) {
    lp::SimplexSolver solver(tol_, options_);
    lp::SimplexResult res = solver.solve(candidate);
    ++solves;
    if (res.status == model::ProblemStatus::INFEASIBLE) return true;
    if (res.status == model::ProblemStatus::NUMERICAL_ERROR) return true;  // conservative
    return false;
}

IISResult IISComputer::compute(const model::Problem& problem) {
    auto start = std::chrono::high_resolution_clock::now();

    IISResult result;
    result.message = "feasible model: no IIS to report";

    if (!problem.quadratic_terms.empty()) {
        result.message = "IIS computation is not supported for models with quadratic terms";
        return result;
    }
    if (problem.constraint_matrix.rows() != problem.constraints.size()) {
        result.message = "inconsistent constraint matrix and constraint list";
        return result;
    }

    const std::size_t n = problem.variables.size();
    const std::size_t m = problem.constraints.size();

    auto build_red = [&](const std::vector<char>& row_kept,
                         const std::vector<char>& lb_kept,
                         const std::vector<char>& ub_kept) {
        model::Problem red = problem;
        red.variables = problem.variables;
        for (std::size_t j = 0; j < n; ++j) {
            if (!lb_kept[j]) red.variables[j].lower_bound = -std::numeric_limits<double>::infinity();
            if (!ub_kept[j]) red.variables[j].upper_bound =  std::numeric_limits<double>::infinity();
        }

        std::vector<model::Constraint> kept;
        std::vector<numerical::Triplet> triplets;
        kept.reserve(m);
        std::size_t r = 0;
        const auto& rp = problem.constraint_matrix.row_ptr();
        const auto& ci = problem.constraint_matrix.col_indices();
        const auto& av = problem.constraint_matrix.values();
        for (std::size_t i = 0; i < m; ++i) {
            if (!row_kept[i]) continue;
            kept.push_back(problem.constraints[i]);
            for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
                triplets.push_back({r, static_cast<std::size_t>(ci[k]), av[k]});
            }
            ++r;
        }

        red.constraints = std::move(kept);
        red.constraint_matrix = numerical::SparseMatrix::from_triplets(
            r, n, triplets, numerical::StorageOrder::CSR);
        return red;
    };

    std::vector<char> row_kept(m, 1);
    std::vector<char> lb_kept(n, 0);
    std::vector<char> ub_kept(n, 0);
    for (std::size_t j = 0; j < n; ++j) {
        if (problem.variables[j].lower_bound > -std::numeric_limits<double>::infinity() &&
            problem.variables[j].lower_bound == problem.variables[j].lower_bound) {
            lb_kept[j] = 1;
        }
        if (problem.variables[j].upper_bound < std::numeric_limits<double>::infinity() &&
            problem.variables[j].upper_bound == problem.variables[j].upper_bound) {
            ub_kept[j] = 1;
        }
    }

    std::size_t solves = 0;
    if (!is_infeasible(build_red(row_kept, lb_kept, ub_kept), solves)) {
        auto end = std::chrono::high_resolution_clock::now();
        result.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        result.lp_solves = solves;
        return result;
    }

    // Rows: deletion filter.
    for (std::size_t i = 0; i < m; ++i) {
        if (!row_kept[i]) continue;
        row_kept[i] = 0;
        if (is_infeasible(build_red(row_kept, lb_kept, ub_kept), solves)) {
            continue;  // still infeasible without the row -> drop it
        }
        row_kept[i] = 1;  // restoring the row made the subsystem feasible -> it belongs to the IIS
    }

    // Bounds: deletion filter.
    for (std::size_t j = 0; j < n; ++j) {
        if (lb_kept[j]) {
            lb_kept[j] = 0;
            if (is_infeasible(build_red(row_kept, lb_kept, ub_kept), solves)) {
                continue;
            }
            lb_kept[j] = 1;
        }
        if (ub_kept[j]) {
            ub_kept[j] = 0;
            if (is_infeasible(build_red(row_kept, lb_kept, ub_kept), solves)) {
                continue;
            }
            ub_kept[j] = 1;
        }
    }

    result.found = true;
    result.lp_solves = solves;
    result.message.clear();
    for (std::size_t i = 0; i < m; ++i) {
        if (row_kept[i]) result.constraint_indices.push_back(i);
    }
    for (std::size_t j = 0; j < n; ++j) {
        if (lb_kept[j]) result.bounds.push_back({j, false});
        if (ub_kept[j]) result.bounds.push_back({j, true});
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

std::string IISResult::to_string(const model::Problem& p) const {
    std::ostringstream os;
    os << "IIS found: " << (found ? "yes" : "no") << "\n";
    if (!found) {
        if (!message.empty()) os << "Message: " << message << "\n";
        return os.str();
    }
    if (!constraint_indices.empty()) {
        os << "Constraints (" << constraint_indices.size() << "):\n";
        for (std::size_t idx : constraint_indices) {
            if (idx < p.constraints.size()) {
                os << "  " << p.constraints[idx].name << " (index " << idx << ")\n";
            } else {
                os << "  index " << idx << "\n";
            }
        }
    }
    if (!bounds.empty()) {
        os << "Bounds (" << bounds.size() << "):\n";
        for (const auto& b : bounds) {
            if (b.variable < p.variables.size()) {
                os << "  " << p.variables[b.variable].name << " "
                   << (b.upper ? "upper bound" : "lower bound") << "\n";
            }
        }
    }
    os << "LP solves: " << lp_solves << "\n";
    os << "Time: " << time_ms << " ms\n";
    return os.str();
}

} // namespace hypernova::validation