#include "symmetry.hpp"
#include <algorithm>
#include <cmath>

namespace hypernova::milp {

namespace {

struct VariableSignature {
    model::VarType type;
    double lb = 0.0;
    double ub = 0.0;
    double obj = 0.0;
    std::vector<std::pair<std::size_t, double>> column; // sorted by row

    bool is_symmetric(const VariableSignature& o, double feas_tol, double opt_tol) const {
        if (type != o.type || std::abs(lb - o.lb) > feas_tol || std::abs(ub - o.ub) > feas_tol) return false;
        double range = std::max(1.0, std::abs(ub - lb));
        if (std::abs(obj - o.obj) > opt_tol / range) return false;
        if (column.size() != o.column.size()) return false;
        for (std::size_t k = 0; k < column.size(); ++k) {
            if (column[k].first != o.column[k].first) return false;
            if (column[k].second != o.column[k].second) return false;
        }
        return true;
    }
};

} // namespace

SymmetryDetection SymmetryDetector::detect(const model::Problem& problem) const {
    SymmetryDetection det;
    const std::size_t n = problem.variables.size();
    const std::size_t m = problem.constraints.size();
    if (m == 0 || n == 0) return det;

    // Mark variables that participate in quadratic terms: they cannot be
    // grouped (the signature would ignore the quadratic objective coupling).
    std::vector<char> in_quadratic(n, 0);
    for (const auto& t : problem.quadratic_terms) {
        if (t.row < n) in_quadratic[t.row] = 1;
        if (t.col < n) in_quadratic[t.col] = 1;
    }

    const auto& A = problem.constraint_matrix;
    if (A.order() != model::StorageOrder::CSR || A.rows() != m || A.cols() != n) {
        return det;
    }

    std::vector<VariableSignature> sigs(n);
    for (const auto& v : problem.variables) {
        sigs[v.index].type = v.type;
        sigs[v.index].lb = v.lower_bound;
        sigs[v.index].ub = v.upper_bound;
        sigs[v.index].obj = v.objective_coeff;
    }
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
            const std::size_t j = A.col_indices()[k];
            if (j < n) sigs[j].column.push_back({i, A.values()[k]});
        }
    }

    std::vector<std::size_t> assigned(n, n);
    for (std::size_t a = 0; a < n; ++a) {
        if (in_quadratic[a]) continue;
        if (sigs[a].column.empty()) continue;
        if (assigned[a] != n) continue;
        std::vector<std::size_t> group;
        group.push_back(a);
        assigned[a] = a;
        for (std::size_t b = a + 1; b < n; ++b) {
            if (in_quadratic[b]) continue;
            if (sigs[b].column.empty()) continue;
            if (assigned[b] != n) continue;
            if (sigs[a].is_symmetric(sigs[b], tol_.feasibility_tol(), tol_.optimality_tol())) {
                group.push_back(b);
                assigned[b] = a;
            }
        }
        if (group.size() >= 2) {
            det.variable_groups.push_back(group);
            det.num_symmetric_variables += group.size();
            // Ordering rows x[0] >= x[1] >= ... >= x[n-1].
            for (std::size_t k = 1; k < group.size(); ++k) {
                det.ordering_rows.push_back({{group[k - 1], 1.0}, {group[k], -1.0}});
                det.row_names.push_back("symorder_g" + std::to_string(det.variable_groups.size()) +
                                        "_" + std::to_string(k));
            }
        }
    }

    return det;
}

} // namespace hypernova::milp