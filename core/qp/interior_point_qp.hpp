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

struct InteriorPointQPOptions {
    int max_iterations = 100;
    double time_limit_seconds = 0.0;
    double convergence_tol = 1e-8;
    double complementarity_tol = 1e-8;
    // Baseline H + delta*I regularization; raised adaptively up to
    // regularization_max when the KKT factorization is singular or the
    // iterative-refinement residual does not converge.
    double regularization = 1e-8;
    double regularization_max = 1e-4;
    bool check_convexity = true;
    std::function<bool()> interrupt_callback;
};

struct InteriorPointQPResult {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<double> slack;
    std::size_t iterations = 0;
    double solve_time_ms = 0.0;
    // Convexity of the (minimized) Hessian.
    ConvexityClassification convexity = ConvexityClassification::UNKNOWN;
    // Total iterative-refinement correction sweeps across all KKT solves.
    std::size_t refinement_sweeps = 0;
    // Number of times the regularization was bumped because the KKT solve was
    // singular or the residual was not reduced.
    std::size_t regularization_retries = 0;
    // Multipliers for variable bounds / reduced costs
    std::vector<double> reduced_costs;
    // Diagnostic residuals for validation and reporting
    double primal_residual = 0.0;
    double dual_residual = 0.0;
    double complementarity_residual = 0.0;
    double max_bound_violation = 0.0;
};

// Primal-dual interior-point solver for convex QPs with a fully bound-aware
// barrier.  Variables may carry arbitrary combinations of finite/infinite
// lower/upper bounds (including fixed variables, lb == ub), and constraints
// may be LE/GE/EQ.  LE/GE rows are converted to equalities with explicit
// nonnegative slack variables g; variable bounds are handled through bound
// slacks sl (x - lb) and su (ub - x) with their own duals, so every
// complementarity pair lives inside the barrier.
class InteriorPointQPSolver {
public:
    explicit InteriorPointQPSolver(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                                    const InteriorPointQPOptions& options = InteriorPointQPOptions());
    ~InteriorPointQPSolver() = default;

    InteriorPointQPResult solve(const model::Problem& problem);

    const InteriorPointQPOptions& options() const { return options_; }
    void set_options(const InteriorPointQPOptions& opts) { options_ = opts; }

private:
    numerical::ToleranceConfig tol_;
    InteriorPointQPOptions options_;

    std::size_t nvars_ = 0;
    std::size_t ncons_ = 0;
    std::size_t n_lb_ = 0;    // variables with a finite lower bound
    std::size_t n_ub_ = 0;    // variables with a finite upper bound
    std::size_t n_ineq_ = 0;  // LE/GE constraints (explicit slack, g)

    // lower-bound slack l -> variable j and lb value
    std::vector<std::size_t> lb_var_;
    std::vector<double> lb_val_;
    // upper-bound slack u -> variable j and ub value
    std::vector<std::size_t> ub_var_;
    std::vector<double> ub_val_;
    // inequality slack k -> constraint row i and sigma (+1 LE, -1 GE)
    std::vector<std::size_t> ineq_row_;
    std::vector<int> ineq_sigma_;

    // inverse maps: variable j -> its bound-slack index, npos if absent
    std::vector<std::size_t> lb_idx_of_;
    std::vector<std::size_t> ub_idx_of_;
    // constraint row i -> its inequality-slack index, npos if EQ
    std::vector<std::size_t> ineq_idx_of_;

    std::vector<double> x_, y_;                     // primal, equality duals
    std::vector<double> sl_, su_, g_;               // bound/constraint slacks
    std::vector<double> lambdal_, lambdau_, lambdag_;  // slack duals

    std::vector<double> dx_, dy_, dsl_, dsu_, dg_;
    std::vector<double> dlambdal_, dlambdau_, dlambdag_;

    std::unique_ptr<numerical::SparseFactorization> kkt_factorization_;
    numerical::SparseMatrix kkt_mat_;

    void initialize(const model::Problem& problem);
    double compute_mu() const;
    double compute_sigma(double mu) const;
    void assemble_kkt(const model::Problem& problem, double regularization);
    void form_affine_rhs(const model::Problem& problem, std::vector<double>& rhs) const;
    void unpack_directions(const std::vector<double>& sol);
    void compute_affine_step(const model::Problem& problem);
    void compute_centering_step(const model::Problem& problem, double mu, double sigma);
    double compute_alpha_primal(double tau) const;
    double compute_alpha_dual(double tau) const;
    double compute_alpha_pair(const std::vector<double>& vals, const std::vector<double>& dirs) const;
    double kkt_relative_residual(const std::vector<double>& sol, const std::vector<double>& rhs) const;
    void update_variables(double alpha_p, double alpha_d);
    bool check_convergence(const model::Problem& problem, double mu);

    std::size_t comp_count() const { return n_lb_ + n_ub_ + n_ineq_; }
    std::size_t kkt_size() const { return nvars_ + ncons_ + 2 * (n_lb_ + n_ub_ + n_ineq_); }

    // Per-solve KKT health counters (reset at the start of each solve()).
    std::size_t sweep_total_ = 0;
    std::size_t reg_retries_ = 0;
    double final_regularization_ = 1e-8;
    bool kkt_step_ok_ = true;
};

} // namespace hypernova::qp