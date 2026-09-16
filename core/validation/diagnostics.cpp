#include "diagnostics.hpp"
#include "solution_verifier.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace hypernova::validation {

DiagnosticsAnalyzer::DiagnosticsAnalyzer(const numerical::ToleranceConfig& tol,
                                         const lp::SimplexOptions& options)
    : tol_(tol), options_(options) {}

std::string DiagnosticsResult_to_string_helper(const NumericalDiagnostics& d) {
    std::ostringstream os;
    os << "Quality: " << d.quality << "\n";
    os << "Status:  " << static_cast<int>(d.status) << "\n";
    if (!d.warnings.empty()) {
        for (const auto& w : d.warnings) os << "warning: " << w << "\n";
    }
    os << "variables:         " << d.num_variables << "\n";
    os << "constraints:       " << d.num_constraints << "\n";
    os << "nonzeros:          " << d.num_nonzeros << "\n";
    os << "max |coeff|:       " << d.max_coefficient << "\n";
    os << "min |coeff|:       " << d.min_nonzero_coefficient << "\n";
    os << "dynamic range:     " << d.dynamic_range << "\n";
    os << "kappa(A*A^T):      " << d.condition_estimate << "\n";
    os << "rank deficient:    " << (d.matrix_rank_deficient ? "yes" : "no") << "\n";
    os << "primal infeas:     " << d.primal_infeasibility << "\n";
    os << "dual infeas:       " << d.dual_infeasibility << "\n";
    os << "complementarity:   " << d.complementarity << "\n";
    os << "integrality viol:  " << d.integrality_violation << "\n";
    os << "objective:         " << d.objective_value << "\n";
    return os.str();
}

std::string NumericalDiagnostics::to_string() const {
    return DiagnosticsResult_to_string_helper(*this);
}

std::string NumericalDiagnostics::to_json() const {
    std::ostringstream os;
    os << "{\n";
    os << "  \"status\": " << static_cast<int>(status) << ",\n";
    os << "  \"objective_value\": " << objective_value << ",\n";
    os << "  \"num_variables\": " << num_variables << ",\n";
    os << "  \"num_constraints\": " << num_constraints << ",\n";
    os << "  \"num_nonzeros\": " << num_nonzeros << ",\n";
    os << "  \"max_coefficient\": " << max_coefficient << ",\n";
    os << "  \"min_nonzero_coefficient\": " << min_nonzero_coefficient << ",\n";
    os << "  \"dynamic_range\": " << dynamic_range << ",\n";
    os << "  \"condition_estimate\": " << condition_estimate << ",\n";
    os << "  \"primal_infeasibility\": " << primal_infeasibility << ",\n";
    os << "  \"dual_infeasibility\": " << dual_infeasibility << ",\n";
    os << "  \"complementarity\": " << complementarity << ",\n";
    os << "  \"integrality_violation\": " << integrality_violation << ",\n";
    os << "  \"quality\": \"" << quality << "\",\n";
    os << "  \"warnings\": [";
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i) os << ", ";
        os << "\"" << warnings[i] << "\"";
    }
    os << "],\n";
    os << "  \"matrix_rank_deficient\": " << (matrix_rank_deficient ? "true" : "false") << "\n";
    os << "}\n";
    return os.str();
}

NumericalDiagnostics DiagnosticsAnalyzer::analyze(const model::Problem& problem) {
    NumericalDiagnostics d;
    d.num_variables = problem.variables.size();
    d.num_constraints = problem.constraints.size();
    const std::size_t m = problem.constraints.size();
    const std::size_t n = problem.variables.size();

    if (!problem.quadratic_terms.empty()) {
        d.warnings.push_back("quadratic objective present; probe and condition estimate cover the constraint matrix only");
    }

    if (problem.constraint_matrix.rows() == m &&
        problem.constraint_matrix.cols() == n) {
        const auto& rp = problem.constraint_matrix.row_ptr();
        const auto& ci = problem.constraint_matrix.col_indices();
        const auto& av = problem.constraint_matrix.values();
        if (problem.constraint_matrix.order() == numerical::StorageOrder::CSR) {
            d.num_nonzeros = av.size();
            double maxv = 0.0;
            double minv = std::numeric_limits<double>::infinity();
            for (double v : av) {
                const double a = std::abs(v);
                if (a == 0.0) continue;
                maxv = std::max(maxv, a);
                minv = std::min(minv, a);
            }
            d.max_coefficient = maxv;
            d.min_nonzero_coefficient = (minv == std::numeric_limits<double>::infinity()) ? 0.0 : minv;
            d.dynamic_range = (d.min_nonzero_coefficient > 0.0) ? maxv / d.min_nonzero_coefficient : 0.0;

            if (m > 0 && n > 0) {
                // C = A * A^T via per-column row-pair accumulation.
                std::vector<std::vector<std::pair<std::size_t, double>>> cols_of_rows(
                    n, std::vector<std::pair<std::size_t, double>>());
                for (std::size_t i = 0; i < m; ++i) {
                    for (std::size_t k = rp[i]; k < rp[i + 1]; ++k) {
                        cols_of_rows[static_cast<std::size_t>(ci[k])].push_back({i, av[k]});
                    }
                }
                std::vector<numerical::Triplet> triplets;
                triplets.reserve(av.size());
                for (std::size_t j = 0; j < n; ++j) {
                    const auto& list = cols_of_rows[j];
                    for (std::size_t a = 0; a < list.size(); ++a) {
                        for (std::size_t b = a; b < list.size(); ++b) {
                            const double prod = list[a].second * list[b].second;
                            if (prod == 0.0) continue;
                            triplets.emplace_back(list[a].first, list[b].first, prod);
                            if (a != b) triplets.emplace_back(list[b].first, list[a].first, prod);
                        }
                    }
                }
                numerical::SparseMatrix C = numerical::SparseMatrix::from_triplets(m, m, triplets);
                C.sum_duplicates();
                try {
                    auto fac = numerical::create_factorization(numerical::FactorizationType::Cholesky, C, tol_);
                    const auto stats = fac->stats();
                    if (stats.singular) {
                        d.matrix_rank_deficient = true;
                        d.condition_estimate = 0.0;
                        d.warnings.push_back("normal-equation matrix A*A^T is singular (rank-deficient)");
                    } else {
                        d.condition_estimate = stats.condition_estimate;
                    }
                } catch (const std::exception&) {
                    d.matrix_rank_deficient = true;
                    d.condition_estimate = 0.0;
                    d.warnings.push_back("normal-equation matrix A*A^T could not be factorized");
                }
            }
        }
    }

    // Probe solve and residual verification.
    lp::SimplexSolver probe(tol_, options_);
    lp::SimplexResult result = probe.solve(problem);
    d.status = result.status;
    d.objective_value = result.objective_value;

    if (result.status == model::ProblemStatus::OPTIMAL ||
        result.status == model::ProblemStatus::SUBOPTIMAL) {
        model::Solution sol;
        sol.status = result.status;
        sol.objective_value = result.objective_value;
        sol.primal = result.primal;
        sol.dual = result.dual;
        sol.reduced_costs = result.reduced_costs;
        sol.basis_status = result.basis_status;
        SolutionVerifier verifier(tol_);
        VerificationResult vr = verifier.verify_detailed(problem, sol);
        d.primal_infeasibility = vr.primal_infeasibility;
        d.dual_infeasibility = vr.dual_infeasibility;
        d.complementarity = vr.complementarity;
        d.integrality_violation = vr.integrality_violation;
        if (d.primal_infeasibility > std::max(tol_.feasibility_tol(), 1e-8)) {
            d.warnings.push_back("primal infeasibility above feasibility tolerance");
        }
        if (d.dual_infeasibility > std::max(tol_.optimality_tol(), 1e-8)) {
            d.warnings.push_back("dual infeasibility above optimality tolerance");
        }
    } else if (result.status == model::ProblemStatus::NUMERICAL_ERROR) {
        d.quality = "poor";
        d.warnings.push_back("solver returned NUMERICAL_ERROR");
    }

    if (d.condition_estimate >= 1e12) {
        d.warnings.push_back("ill-conditioned constraint matrix (kappa(A*A^T) >= 1e12)");
    }
    if (d.dynamic_range >= 1e16) {
        d.warnings.push_back("extreme coefficient range (max/min >= 1e16)");
    }

    if (d.quality.empty()) {
        d.quality = d.warnings.empty() ? "good" : "warn";
    }
    if (d.condition_estimate >= 1e14 || d.status == model::ProblemStatus::NUMERICAL_ERROR) {
        d.quality = "poor";
    }

    return d;
}

} // namespace hypernova::validation