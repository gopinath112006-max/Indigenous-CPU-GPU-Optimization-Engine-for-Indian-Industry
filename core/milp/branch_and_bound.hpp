#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../lp/simplex.hpp"
#include "../lp/interior_point.hpp"
#include "../qp/active_set.hpp"
#include "../qp/interior_point_qp.hpp"
#include "cutting_planes.hpp"
#include "node_presolve.hpp"
#include <vector>
#include <deque>
#include <cstddef>
#include <memory>
#include <queue>
#include <functional>
#include <chrono>
#include <shared_mutex>

namespace hypernova::milp {

enum class BranchingStrategy {
    MOST_FRACTIONAL,
    PSEUDOCOST,
    STRONG_BRANCHING,
    RELIABILITY
};

enum class NodeSelectionStrategy {
    BEST_FIRST,
    DEPTH_FIRST,
    BEST_ESTIMATE,
    HYBRID
};

enum class QpRelaxationSolver {
    ACTIVE_SET,
    INTERIOR_POINT
};

struct BranchingOptions {
    BranchingStrategy strategy = BranchingStrategy::RELIABILITY;
    NodeSelectionStrategy node_strategy = NodeSelectionStrategy::HYBRID;
    int strong_branch_candidates = 10;
    double pseudocost_weight = 0.5;
    int max_depth = 1000;
};

struct CutOptions {
    bool gomory_cuts = true;
    bool mir_cuts = true;
    bool knapsack_cuts = false;
    bool clique_cuts = false;
    bool flow_cover_cuts = false;
    int max_cuts_per_node = 50;
    double cut_efficacy_threshold = 0.01;
};

struct HeuristicOptions {
    bool rounding = true;
    bool diving = true;
    bool feasibility_pump = false;
    bool rins = false;
    int rins_frequency = 10;
    int diving_iterations = 20;
};

struct BranchAndBoundOptions {
    BranchingOptions branching;
    CutOptions cuts;
    HeuristicOptions heuristics;
    bool node_presolve = true;
    bool symmetry_breaking = false;
    double mip_gap_tolerance = 1e-4;
    double time_limit_seconds = 3600.0;
    int node_limit = -1;
    int solution_limit = -1;
    int threads = 1;
    bool use_gpu = false;
    bool qp_relaxation = false;
    QpRelaxationSolver qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    std::function<bool()> interrupt_callback;
};

struct CutSeparationStatistics {
    std::size_t gomory_candidates = 0, gomory_added = 0;
    std::size_t mir_candidates = 0, mir_added = 0;
    std::size_t knapsack_candidates = 0, knapsack_added = 0;
    std::size_t clique_candidates = 0, clique_added = 0;
    std::size_t flow_candidates = 0, flow_added = 0;
};

struct BnBStatistics {
    std::size_t nodes_created = 0;
    std::size_t nodes_pruned_by_bound = 0;
    std::size_t nodes_pruned_infeasible = 0;
    std::size_t incumbent_improvements = 0;
    std::size_t cuts_generated = 0;
    std::size_t cuts_active = 0;
    std::size_t heuristic_solutions = 0;
    double best_bound = std::numeric_limits<double>::infinity();
    std::vector<double> bound_progression;
    CutSeparationStatistics cut_stats;
};

struct BnBNode {
    std::size_t id;
    std::size_t parent_id;
    std::vector<BoundChange> bounds;
    double lower_bound = -std::numeric_limits<double>::infinity();
    double upper_bound = std::numeric_limits<double>::infinity();
    int depth = 0;
    std::vector<double> lp_solution;
    bool lp_solved = false;
    bool pruned = false;
    bool branched = false;  // children created; no longer part of the frontier
    bool claimed = false;   // parallel B&B: exclusively owned by one worker
    int branching_var = -1;  // variable branched on (for pseudocost updates)
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
};

struct BranchAndBoundResult {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    double best_bound = 0.0;
    double gap = 0.0;
    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<double> reduced_costs;
    std::size_t nodes_explored = 0;
    std::size_t nodes_pruned = 0;
    std::size_t simplex_iterations = 0;
    std::size_t ipm_iterations = 0;
    double solve_time_ms = 0.0;
    BnBStatistics stats;
};

class BranchAndBoundSolver {
public:
    // The parallel executor runs per-node routines and touches shared solver
    // state (cut pool, pseudocosts, node helpers) from worker threads.
    friend struct ParallelContext;

    explicit BranchAndBoundSolver(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                                   const BranchAndBoundOptions& options = BranchAndBoundOptions());
    ~BranchAndBoundSolver() = default;

    BranchAndBoundResult solve(const model::Problem& problem);

    const BranchAndBoundOptions& options() const { return options_; }
    void set_options(const BranchAndBoundOptions& opts) { options_ = opts; }

    // Pseudocost bookkeeping (Phase 4): exposed for diagnostics and tests that
    // verify the costs are actually updated and used by branching decisions.
    const std::vector<double>& pseudocost_up() const { return pseudocost_up_; }
    const std::vector<double>& pseudocost_down() const { return pseudocost_down_; }
    const std::vector<int>& pseudocount_up() const { return pseudocount_up_; }
    const std::vector<int>& pseudocount_down() const { return pseudocount_down_; }

private:
    numerical::ToleranceConfig tol_;
    BranchAndBoundOptions options_;

    // deque (not vector): pointers/references into nodes_ must survive the
    // push_back that NodeSelector-based B&B performs while holding a node ref.
    std::deque<BnBNode> nodes_;
    std::vector<std::size_t> id_to_index_;  // node.id -> position in nodes_
    std::size_t next_node_id_ = 0;
    double best_objective_ = std::numeric_limits<double>::infinity();
    double best_bound_ = std::numeric_limits<double>::infinity();
    std::vector<double> best_solution_;
    std::size_t nodes_explored_ = 0;
    std::size_t nodes_pruned_ = 0;
    std::size_t nodes_created_ = 0;
    BnBStatistics stats_;

    std::vector<double> pseudocost_up_;
    std::vector<double> pseudocost_down_;
    std::vector<int> pseudocount_up_;
    std::vector<int> pseudocount_down_;
    // Protects pseudocost state when the parallel solver branches concurrently.
    // Serial mode never contends; the locks are uncontended there.
    mutable std::shared_mutex pseudocost_mutex_;

    // Set when a node relaxation fails numerically (ITER_LIMIT / TIME_LIMIT /
    // NUMERICAL_ERROR): such a node is not certified infeasible, so the
    // overall result must surface the failure instead of claiming OPTIMAL.
    model::ProblemStatus relaxation_failure_ = model::ProblemStatus::UNKNOWN;

    // Captured once at solve() entry so the parallel worker loop can check
    // elapsed time without receiving the start_time argument explicitly.
    std::chrono::high_resolution_clock::time_point solve_start_time_;

    CutPool cut_pool_;

    std::vector<std::vector<std::pair<std::size_t, double>>> symmetry_rows_;
    std::vector<std::string> symmetry_row_names_;

    void initialize(const model::Problem& problem);
    void process_root_node(const model::Problem& problem);
    void process_node(BnBNode& node, const model::Problem& problem);
    BranchAndBoundResult solve_serial(const model::Problem& problem,
                                       const std::chrono::high_resolution_clock::time_point& start_time);
    BranchAndBoundResult solve_parallel(const model::Problem& problem,
                                         const std::chrono::high_resolution_clock::time_point& start_time);
    bool solve_node_lp(BnBNode& node, const model::Problem& problem);
    bool apply_node_presolve(BnBNode& node, const model::Problem& problem);
    int select_branching_variable(const BnBNode& node, const model::Problem& problem);
    std::pair<double, double> estimate_child_bounds(const BnBNode& node, int var, const model::Problem& problem);
    std::vector<std::size_t> branch(const BnBNode& node, int var, const model::Problem& problem);
    void update_pseudocosts(int var, double parent_lb, double parent_frac, const BnBNode& child);
    void apply_heuristics(const BnBNode& node, const model::Problem& problem);
    void apply_cuts(BnBNode& node, const model::Problem& problem);
    bool is_integral(const std::vector<double>& solution, const model::Problem& problem) const;
    double compute_gap() const;
    double compute_best_bound() const;
    void update_best_solution(const BnBNode& node, const model::Problem& problem);

    std::vector<double> pseudocost_branching(int var) const;
    std::vector<double> strong_branching(const BnBNode& node, int var, const model::Problem& problem);
    double reliability_score(int var, const model::Problem& problem) const;
};

} // namespace hypernova::milp