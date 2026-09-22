#pragma once

#include "qp_common.hpp"
#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../numerical/sparse_matrix.hpp"
#include "../numerical/factorization.hpp"
#include <vector>
#include <cstddef>
#include <memory>
#include <functional>

namespace hypernova::qp {

struct ActiveSetOptions {
    int max_iterations = 10000;
    double time_limit_seconds = 0.0;
    double feasibility_tol = 1e-9;
    double optimality_tol = 1e-9;
    bool warm_start = true;
    bool check_convexity = true;
    // Baseline H + delta*I regularization for the KKT solves; bumped up to
    // regularization_max when a factorization reports a singular pivot.
    double regularization = 1e-10;
    double regularization_max = 1e-4;
    std::function<bool()> interrupt_callback;
};

struct ActiveSetResult {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<int> active_set;
    std::size_t iterations = 0;
    double solve_time_ms = 0.0;
    // Convexity of the (minimized) Hessian and detailed diagnostics.
    ConvexityClassification convexity = ConvexityClassification::UNKNOWN;
    ConvexityDiagnostics convexity_diagnostics;
    std::string rejection_reason;
    // Total iterative-refinement correction sweeps across all KKT solves.
    std::size_t refinement_sweeps = 0;
};

class ActiveSetQPSolver {
public:
    explicit ActiveSetQPSolver(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                                const ActiveSetOptions& options = ActiveSetOptions());
    ~ActiveSetQPSolver() = default;

    ActiveSetResult solve(const model::Problem& problem);

    const ActiveSetOptions& options() const { return options_; }
    void set_options(const ActiveSetOptions& opts) { options_ = opts; }

private:
    numerical::ToleranceConfig tol_;
    ActiveSetOptions options_;

    std::vector<double> x_;
    std::vector<double> lambda_;
    std::vector<int> active_set_;
    std::unique_ptr<numerical::SparseFactorization> kkt_factorization_;
    ConvexityClassification last_convexity_ = ConvexityClassification::UNKNOWN;
    double kkt_regularization_ = 1e-10;
    std::size_t refinement_sweeps_ = 0;

    void initialize(const model::Problem& problem);
    bool check_convexity(const model::Problem& problem);
    bool is_kkt_optimal(const model::Problem& problem);
    int find_blocking_constraint(const model::Problem& problem);
    int find_redundant_constraint(const model::Problem& problem);
    void add_constraint(int idx);
    void remove_constraint(int idx);
    void solve_kkt_system(const model::Problem& problem);

    // Cycling / rank-deficient recovery helpers.
    double compute_objective(const model::Problem& problem, const std::vector<double>& x) const;
    void compute_gradient(const model::Problem& problem, const std::vector<double>& x,
                          std::vector<double>& grad) const;
    double max_constraint_violation(const model::Problem& problem, const std::vector<double>& x) const;
    bool project_to_feasible(const model::Problem& problem, std::vector<double>& x) const;
    void rebuild_active_set(const model::Problem& problem);
    void recompute_multipliers(const model::Problem& problem);
    // Returns true when a feasible, objective-improving point was recovered.
    bool projected_gradient_recovery(const model::Problem& problem);
};

} // namespace hypernova::qp