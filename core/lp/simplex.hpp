#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../numerical/sparse_matrix.hpp"
#include "../numerical/factorization.hpp"
#include <chrono>
#include <vector>
#include <cstddef>
#include <memory>
#include <functional>

namespace hypernova::lp {

enum class SimplexAlgorithm {
    PRIMAL,
    DUAL
};

enum class PricingStrategy {
    DANTZIG,
    DEVEX,
    STEEPEST_EDGE
};

enum class RatioTest {
    HARRIS_TWO_PASS,
    STANDARD
};

struct SimplexOptions {
    SimplexAlgorithm algorithm = SimplexAlgorithm::PRIMAL;
    PricingStrategy pricing = PricingStrategy::DEVEX;
    RatioTest ratio_test = RatioTest::STANDARD;
    int max_iterations = 1000000;
    double time_limit_seconds = 0.0;
    bool anti_cycling = true;
    int perturbation_iterations = 100;
    double perturbation_factor = 1e-6;
    bool bland_rule = true;
    std::size_t steepest_edge_shortlist = 16;
    std::function<bool()> interrupt_callback;
};

struct SimplexResult {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<double> reduced_costs;
    std::vector<int> basis_status;
    std::size_t iterations = 0;
    double solve_time_ms = 0.0;
    SimplexAlgorithm algorithm_used = SimplexAlgorithm::PRIMAL;
};

class SimplexSolver {
public:
    explicit SimplexSolver(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                            const SimplexOptions& options = SimplexOptions());
    ~SimplexSolver() = default;

    SimplexResult solve(const model::Problem& problem);
    SimplexResult solve_with_basis(const model::Problem& problem, const std::vector<int>& var_status);

    const SimplexOptions& options() const { return options_; }
    void set_options(const SimplexOptions& opts) { options_ = opts; }

private:
    numerical::ToleranceConfig tol_;
    SimplexOptions options_;

    SimplexResult solve_impl(const model::Problem& problem, const std::vector<int>* warm_var_status);

    model::Problem eliminate_fixed_variables(const model::Problem& problem,
                                             std::vector<int>& var_map,
                                             std::vector<double>& fixed_vals);

    const model::Problem* problem_ = nullptr;

    std::unique_ptr<numerical::SparseFactorization> basis_factorization_;
    std::vector<int> basis_;
    std::vector<int> nonbasis_;
    std::vector<double> basic_solution_;
    std::vector<double> dual_solution_;
    std::vector<double> reduced_costs_;
    std::vector<double> pricing_weights_;
    std::vector<double> rhs_values_;

    // Revised-simplex incremental basis updates: the direction vector
    // d = B^{-1} * a_entering computed during the pivot, together with the
    // leaving column index, drives the eta-file update in the basis
    // factorization. Full refactorization is performed periodically.
    std::vector<double> pivot_direction_;
    int last_leaving_ = -1;
    int last_entering_ = -1;
    std::size_t iters_since_refactor_ = 0;
    std::size_t refactor_frequency_ = 25;

    model::Problem expanded_problem_;
    std::size_t n_original_vars_ = 0;
    std::size_t n_original_rows_ = 0;
    std::size_t n_slack_vars_ = 0;
    bool perturbed_ = false;
    int perturb_level_ = 0;

    std::vector<int> row_basis_var_;
    std::vector<char> artificial_cols_;

    // Variables with infinite lower bounds are transformed for the non-negative
    // standard form. Indexed by the reduced variable; -1 when not transformed:
    //   fully free  (lb=-inf, ub=+inf)  -> x = xp - xn        (xp, xn >= 0)
    //   upper-only  (lb=-inf, ub finite)-> x = ub - xc        (xc >= 0)
    std::vector<int> free_split_p_;
    std::vector<int> free_split_n_;
    std::vector<int> complement_var_;
    std::vector<double> phase_cost_;
    bool phase1_active_ = false;
    bool maximize_ = false;

    // Dual-simplex state: complementation of negative-cost columns so that the
    // identity (slack/artificial) basis is dual feasible.
    std::vector<char> complemented_;
    bool was_maximizing_ = false;
    std::size_t dual_iterations_ = 0;
    std::chrono::high_resolution_clock::time_point solve_start_time_;

    void initialize_basis(const model::Problem& problem);
    void initialize_basis_with_warm_start(const model::Problem& problem, const std::vector<int>& var_status);
    void compute_dual_solution();
    void compute_reduced_costs();
    int select_entering_variable();
    int select_leaving_variable(int entering);
    void pivot(int entering, int leaving);
    void update_basis_factorization(int leaving, int entering);
    bool check_optimality();
    bool check_unbounded(int entering);
    void apply_perturbation();
    void remove_perturbation();

    // Pricing support. Devex maintains per-column weights (approximate
    // steepest-edge scores |rc|/sqrt(w)); STEEPEST_EDGE rescores the top-Devex
    // shortlist by exact edge length, computed with one FTRAN per candidate.
    void initialize_pricing_weights();
    void update_devex_weights(int leaving);
    std::vector<std::size_t> ranked_candidates(bool maximize) const;
    double exact_edge_weight(std::size_t var) const;

    // Dual-simplex support: negates columns whose (minimization-form) cost is
    // negative so the initial basis is dual feasible. Returns false when an
    // unbounded negative-cost column prevents the construction.
    bool prepare_dual_start();
    int select_dual_leaving();
    int select_dual_entering(const std::vector<double>& pivot_row);
    model::ProblemStatus run_dual_loop();

    double compute_step_length(int entering, int leaving) const;
};

} // namespace hypernova::lp
