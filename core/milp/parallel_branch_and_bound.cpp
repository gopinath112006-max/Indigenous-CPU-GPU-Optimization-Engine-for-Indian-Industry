#include "branch_and_bound.hpp"
#include "../execution/thread_pool.hpp"
#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../presolve/presolve.hpp"
#include "../lp/simplex.hpp"
#include "../qp/active_set.hpp"
#include "../qp/interior_point_qp.hpp"
#include "heuristics.hpp"
#include "node_presolve.hpp"
#include "symmetry.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <stdexcept>

namespace hypernova::milp {

// Parallel-local state that lives on the stack of solve_parallel().
struct ParallelContext {
    BranchAndBoundSolver& sol;
    const model::Problem& problem;
    const numerical::ToleranceConfig& tol;
    const BranchAndBoundOptions& opts;

    // --- node storage (own copies; stable indices) ---
    std::deque<BnBNode> nodes_;
    std::vector<std::size_t> id_to_index_;
    std::size_t next_node_id_ = 0;

    struct HeapCompare {
        const std::deque<BnBNode>* nodes;
        bool operator()(std::size_t a, std::size_t b) const {
            return (*nodes)[a].lower_bound > (*nodes)[b].lower_bound;
        }
    };
    std::priority_queue<std::size_t, std::vector<std::size_t>, HeapCompare>
        heap_{HeapCompare{&nodes_}};

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    std::atomic<bool> finished_{false};
    std::atomic<std::size_t> pending_{0};
    std::atomic<std::size_t> active_workers_{0};
    std::atomic<std::size_t> idle_workers_{0};
    std::atomic<std::size_t> nodes_explored_{0};
    std::atomic<double> incumbent_{std::numeric_limits<double>::infinity()};
    std::vector<double> best_solution_;

    std::mutex result_mutex_;
    std::atomic<std::size_t> nodes_pruned_{0};
    std::atomic<std::size_t> nodes_pruned_infeasible_{0};
    std::atomic<std::size_t> nodes_pruned_bound_{0};
    std::atomic<std::size_t> nodes_created_{0};
    std::atomic<std::size_t> incumbent_improvements_{0};
    std::atomic<std::size_t> heuristic_solutions_{0};
    std::atomic<std::size_t> cuts_generated_{0};

    model::ProblemStatus relaxation_failure_ = model::ProblemStatus::UNKNOWN;
    bool had_interrupt_ = false;
    bool time_limit_hit_ = false;
    bool node_limit_hit_ = false;
    bool solution_limit_hit_ = false;

    CutSeparationStatistics cut_stats_;
    std::mutex cut_stats_mutex_;
    std::vector<double> bound_progression_;
    std::mutex bp_mutex_;
    std::exception_ptr pool_failure_;

    explicit ParallelContext(BranchAndBoundSolver& s,
                             const model::Problem& p,
                             const numerical::ToleranceConfig& t,
                             const BranchAndBoundOptions& o)
        : sol(s), problem(p), tol(t), opts(o) {}

    bool has_queued_locked() const { return !heap_.empty(); }
    bool is_finished() const { return finished_.load(std::memory_order_acquire); }

    void record_failure(model::ProblemStatus s) {
        std::lock_guard<std::mutex> lk(result_mutex_);
        if (relaxation_failure_ == model::ProblemStatus::UNKNOWN) relaxation_failure_ = s;
    }

    void commit_incumbent_if_better(const BnBNode& node) {
        double obj = node.lower_bound;
        if (obj >= incumbent_.load(std::memory_order_acquire) - tol.feasibility_tol()) return;
        std::lock_guard<std::mutex> lk(result_mutex_);
        if (obj < incumbent_.load(std::memory_order_acquire) - tol.feasibility_tol()) {
            incumbent_.store(obj, std::memory_order_release);
            best_solution_ = node.lp_solution;
            incumbent_improvements_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void push_children(const BnBNode& parent, int var, std::vector<std::size_t>& warm) {
        double val = parent.lp_solution[static_cast<std::size_t>(var)];
        double floor_val = std::floor(val);
        double ceil_val  = std::ceil(val);

        BnBNode down_child;
        down_child.parent_id = parent.id;
        down_child.depth = parent.depth + 1;
        down_child.bounds = parent.bounds;
        down_child.bounds.push_back({static_cast<std::size_t>(var), floor_val, false});
        down_child.upper_bound = floor_val;

        BnBNode up_child;
        up_child.parent_id = parent.id;
        up_child.depth = parent.depth + 1;
        up_child.bounds = parent.bounds;
        up_child.bounds.push_back({static_cast<std::size_t>(var), ceil_val, true});
        up_child.lower_bound = ceil_val;

        std::vector<std::size_t> child_indices;
        child_indices.reserve(2);
        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            down_child.id = next_node_id_++;
            up_child.id   = next_node_id_++;
            std::size_t d = nodes_.size();
            std::size_t u = nodes_.size() + 1;
            id_to_index_.resize(std::max(id_to_index_.size(), up_child.id + 1),
                                std::numeric_limits<std::size_t>::max());
            id_to_index_[down_child.id] = d;
            id_to_index_[up_child.id]   = u;
            nodes_.push_back(std::move(down_child));
            nodes_.push_back(std::move(up_child));
            // Each child lives in EXACTLY ONE owner: the up child goes to the
            // shared heap, the down child either joins the caller's LIFO warm
            // cache (locality) or, when the cache is full, the shared heap.
            // Never push the same index to both heap and warm: a node claimed
            // twice would double-decrement pending_ and deadlock termination.
            if (warm.size() < 8) {
                warm.push_back(d);
            } else {
                heap_.push(d);
            }
            heap_.push(u);
            pending_.fetch_add(2, std::memory_order_relaxed);
            nodes_created_.fetch_add(2, std::memory_order_relaxed);
            child_indices = {d, u};
        }
        queue_cv_.notify_all();
    }

    std::optional<std::size_t> pop_node(std::vector<std::size_t>& warm) {
        // A node index is reachable from two places: this worker's private
        // warm cache AND the shared best-bound heap (push_children leaves the
        // first child in both). Without an exclusive claim two workers can
        // process the same node and duplicate its whole subtree, which never
        // terminates. Every claim is therefore taken under queue_mutex_ and a
        // claimed node is never processed a second time.
        std::unique_lock<std::mutex> lk(queue_mutex_);
        auto claim = [&](std::size_t idx) -> std::optional<std::size_t> {
            if (idx >= nodes_.size()) return std::nullopt;
            BnBNode& nd = nodes_[idx];
            if (nd.claimed || nd.pruned || nd.branched) return std::nullopt;
            nd.claimed = true;
            return idx;
        };
        while (!warm.empty()) {
            std::size_t idx = warm.back();
            warm.pop_back();
            if (auto claimed = claim(idx)) return claimed;
        }
        while (!heap_.empty() && !is_finished()) {
            std::size_t idx = heap_.top();
            heap_.pop();
            if (auto claimed = claim(idx)) return claimed;
        }
        return std::nullopt;
    }

    void apply_cuts_parallel(BnBNode& node) {
        if (!opts.cuts.gomory_cuts && !opts.cuts.mir_cuts &&
            !opts.cuts.knapsack_cuts && !opts.cuts.clique_cuts &&
            !opts.cuts.flow_cover_cuts) {
            return;
        }

        CutGenerator generator(tol);
        std::vector<Cut> candidates;
        auto tally = [&](const std::vector<Cut>& c, std::size_t CutSeparationStatistics::*per) {
            {
                std::lock_guard<std::mutex> lk(cut_stats_mutex_);
                (cut_stats_.*per) += c.size();
            }
            candidates.insert(candidates.end(), c.begin(), c.end());
        };
        if (opts.cuts.gomory_cuts)
            tally(generator.generate_gomory_cuts(problem, node.lp_solution),
                  &CutSeparationStatistics::gomory_candidates);
        if (opts.cuts.mir_cuts)
            tally(generator.generate_mir_cuts(problem, node.lp_solution),
                  &CutSeparationStatistics::mir_candidates);
        if (opts.cuts.knapsack_cuts)
            tally(generator.generate_knapsack_covers(problem, node.lp_solution),
                  &CutSeparationStatistics::knapsack_candidates);
        if (opts.cuts.clique_cuts)
            tally(generator.generate_clique_cuts(problem, node.lp_solution),
                  &CutSeparationStatistics::clique_candidates);
        if (opts.cuts.flow_cover_cuts)
            tally(generator.generate_flow_covers(problem, node.lp_solution),
                  &CutSeparationStatistics::flow_candidates);

        int added = 0;
        for (auto& cut : candidates) {
            if (cut.efficacy < opts.cuts.cut_efficacy_threshold) continue;
            sol.cut_pool_.add_cut(cut);
            ++added;
            cuts_generated_.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lk(cut_stats_mutex_);
                if (cut.name.rfind("gomory", 0) == 0) ++cut_stats_.gomory_added;
                else if (cut.name.rfind("mir_", 0) == 0) ++cut_stats_.mir_added;
                else if (cut.name.rfind("knapsack_cover", 0) == 0) ++cut_stats_.knapsack_added;
                else if (cut.name.rfind("clique", 0) == 0) ++cut_stats_.clique_added;
                else if (cut.name.rfind("flow_cover", 0) == 0) ++cut_stats_.flow_added;
            }
            if (added >= opts.cuts.max_cuts_per_node) break;
        }

        if (added > 0) {
            if (!sol.solve_node_lp(node, problem)) {
                nodes_pruned_infeasible_.fetch_add(1, std::memory_order_relaxed);
                node.pruned = true;
                nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void apply_heuristics_parallel(const BnBNode& node) {
        if (opts.qp_relaxation) return;

        auto try_incumbent = [&](double obj_min, const std::vector<double>& sol_vec) {
            if (!sol.is_integral(sol_vec, problem)) return;
            if (obj_min >= incumbent_.load(std::memory_order_acquire) - tol.feasibility_tol()) return;
            std::lock_guard<std::mutex> lk(result_mutex_);
            if (obj_min < incumbent_.load(std::memory_order_acquire) - tol.feasibility_tol()) {
                incumbent_.store(obj_min, std::memory_order_release);
                best_solution_ = sol_vec;
                heuristic_solutions_.fetch_add(1, std::memory_order_relaxed);
            }
        };

        if (opts.heuristics.rounding) {
            RoundingHeuristic rounding(tol);
            auto res = rounding.run(problem, node.lp_solution);
            if (res.found_solution) {
                double obj = res.objective_value;
                if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
                try_incumbent(obj, res.solution);
            }
        }

        if (opts.heuristics.diving) {
            DivingHeuristic diving(tol, opts.heuristics.diving_iterations);
            auto res = diving.run(problem, node.lp_solution);
            if (res.found_solution) {
                double obj = res.objective_value;
                if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
                try_incumbent(obj, res.solution);
            }
        }

        if (opts.heuristics.feasibility_pump) {
            FeasibilityPumpHeuristic fp(tol);
            auto res = fp.run(problem, node.lp_solution);
            if (res.found_solution && !res.solution.empty()) {
                double obj = res.objective_value;
                if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
                try_incumbent(obj, res.solution);
            }
        }

        if (opts.heuristics.rins &&
            incumbent_.load(std::memory_order_acquire) < std::numeric_limits<double>::infinity() &&
            !best_solution_.empty() &&
            nodes_explored_.load(std::memory_order_acquire) % static_cast<std::size_t>(opts.heuristics.rins_frequency) == 0) {
            RINSHeuristic rins(tol, opts.heuristics.rins_frequency);
            {
                std::lock_guard<std::mutex> lk(result_mutex_);
                rins.set_incumbent(best_solution_);
            }
            auto res = rins.run(problem, node.lp_solution);
            if (res.found_solution && !res.solution.empty()) {
                double obj = res.objective_value;
                if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
                try_incumbent(obj, res.solution);
            }
        }
    }

    void process_node(std::size_t idx, std::vector<std::size_t>& warm) {
        BnBNode& node = nodes_[idx];

        if (!node.lp_solved) {
            if (opts.node_presolve && !sol.apply_node_presolve(node, problem)) {
                node.pruned = true;
                nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
                nodes_pruned_infeasible_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (!sol.solve_node_lp(node, problem)) {
                if (node.status == model::ProblemStatus::ITER_LIMIT ||
                    node.status == model::ProblemStatus::TIME_LIMIT ||
                    node.status == model::ProblemStatus::NUMERICAL_ERROR ||
                    node.status == model::ProblemStatus::INTERRUPTED) {
                    record_failure(node.status);
                }
                node.pruned = true;
                nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            if (node.parent_id != std::numeric_limits<std::size_t>::max() &&
                node.parent_id < id_to_index_.size()) {
                std::size_t pidx;
                int bv = -1;
                double p_lb = 0.0, p_frac = 0.0;
                {
                    std::lock_guard<std::mutex> lk(queue_mutex_);
                    pidx = id_to_index_[node.parent_id];
                    if (pidx < nodes_.size() && nodes_[pidx].branching_var >= 0 && nodes_[pidx].lp_solved) {
                        const BnBNode& parent = nodes_[pidx];
                        bv = parent.branching_var;
                        p_lb = parent.lower_bound;
                        p_frac = parent.lp_solution[static_cast<std::size_t>(bv)] -
                                 std::floor(parent.lp_solution[static_cast<std::size_t>(bv)]);
                    }
                }
                if (bv >= 0) sol.update_pseudocosts(bv, p_lb, p_frac, node);
            }
        }

        {
            double inc = incumbent_.load(std::memory_order_acquire);
            if (node.lower_bound >= inc - opts.mip_gap_tolerance * std::abs(inc)) {
                node.pruned = true;
                nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
                nodes_pruned_bound_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }

        if (sol.is_integral(node.lp_solution, problem)) {
            commit_incumbent_if_better(node);
            node.pruned = true;
            return;
        }

        apply_cuts_parallel(node);
        if (!node.pruned && node.lp_solved && sol.is_integral(node.lp_solution, problem)) {
            commit_incumbent_if_better(node);
            node.pruned = true;
            return;
        }

        if (!node.pruned && node.lp_solved) {
            apply_heuristics_parallel(node);
        }

        int branch_var = sol.select_branching_variable(node, problem);
        if (branch_var >= 0) {
            node.branching_var = branch_var;
            node.branched = true;
            push_children(node, branch_var, warm);
        }
    }

    bool check_limits() {
        if (is_finished()) return true;
        if (opts.interrupt_callback && opts.interrupt_callback()) {
            had_interrupt_ = true;
            finished_.store(true, std::memory_order_release);
            queue_cv_.notify_all();
            return true;
        }
        return false;
    }

    void run_workers(std::size_t num_threads, std::vector<std::size_t> seed_warm) {
        execution::WorkStealingThreadPool pool(num_threads);
        active_workers_.store(num_threads, std::memory_order_release);
        idle_workers_.store(0, std::memory_order_release);

        for (std::size_t i = 0; i < num_threads; ++i) {
            pool.submit([this, i, seed_warm] {
                std::vector<std::size_t> warm = (i == 0) ? seed_warm : std::vector<std::size_t>{};
                try {
                    while (!is_finished()) {
                        auto opt_idx = pop_node(warm);
                        if (!opt_idx) {
                            std::unique_lock<std::mutex> lk(queue_mutex_);
                            idle_workers_.fetch_add(1, std::memory_order_relaxed);
                            if (pending_.load(std::memory_order_relaxed) == 0 &&
                                idle_workers_.load(std::memory_order_relaxed) ==
                                    active_workers_.load(std::memory_order_relaxed)) {
                                finished_.store(true, std::memory_order_release);
                                queue_cv_.notify_all();
                            }
                            queue_cv_.wait(lk, [this]{ return is_finished() || has_queued_locked(); });
                            idle_workers_.fetch_sub(1, std::memory_order_relaxed);
                            if (is_finished()) break;
                            continue;
                        }

                        pending_.fetch_sub(1, std::memory_order_release);
                        process_node(*opt_idx, warm);
                        nodes_explored_.fetch_add(1, std::memory_order_relaxed);

                        if (check_limits()) break;

                        if (nodes_explored_.load() % 200 == 0) {
                            {
                                std::lock_guard<std::mutex> lk(queue_mutex_);
                                sol.cut_pool_.age_cuts();
                                sol.cut_pool_.remove_ineffective_cuts();
                            }
                            if (nodes_explored_.load() % 500 == 0) {
                                std::lock_guard<std::mutex> lk(bp_mutex_);
                                bound_progression_.push_back(compute_best_bound());
                            }
                        }

                        auto elapsed = std::chrono::duration<double>(
                            std::chrono::high_resolution_clock::now() - sol.solve_start_time_).count();
                        if (elapsed >= opts.time_limit_seconds) {
                            std::lock_guard<std::mutex> lk(result_mutex_);
                            time_limit_hit_ = true;
                            finished_.store(true, std::memory_order_release);
                            queue_cv_.notify_all();
                            break;
                        }

                        if (opts.node_limit >= 0 &&
                            nodes_explored_.load(std::memory_order_acquire) >=
                                static_cast<std::size_t>(opts.node_limit)) {
                            std::lock_guard<std::mutex> lk(result_mutex_);
                            node_limit_hit_ = true;
                            finished_.store(true, std::memory_order_release);
                            queue_cv_.notify_all();
                            break;
                        }

                        if (opts.solution_limit > 0 &&
                            incumbent_improvements_.load(std::memory_order_acquire) >=
                                static_cast<std::size_t>(opts.solution_limit)) {
                            std::lock_guard<std::mutex> lk(result_mutex_);
                            solution_limit_hit_ = true;
                            finished_.store(true, std::memory_order_release);
                            queue_cv_.notify_all();
                            break;
                        }
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> lk(result_mutex_);
                    pool_failure_ = std::current_exception();
                    finished_.store(true, std::memory_order_release);
                    queue_cv_.notify_all();
                }

                active_workers_.fetch_sub(1, std::memory_order_release);
                maybe_signal_done();
            });
        }

        // Diagnostic watchdog (lock-free reads): dump state if the B&B worker
        // loop has not finished within the timeout.
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            if (!is_finished()) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                std::cerr << "BB-STUCK fin=0 p=" << pending_.load()
                          << " idl=" << idle_workers_.load()
                          << " act=" << active_workers_.load()
                          << " heap=" << heap_.size()
                          << " nodes=" << nodes_.size() << "\n";
            } else {
                std::cerr << "BB-DONE fin=1 p=" << pending_.load()
                          << " idl=" << idle_workers_.load()
                          << " act=" << active_workers_.load() << "\n";
            }
        }

        pool.wait_idle();
        std::cerr << "POOL-DONE\n";
    }

    void maybe_signal_done() {
        if (pending_.load(std::memory_order_acquire) == 0 &&
            idle_workers_.load(std::memory_order_acquire) == active_workers_.load(std::memory_order_acquire)) {
            finished_.store(true, std::memory_order_release);
            queue_cv_.notify_all();
        }
    }

    double compute_best_bound() const {
        double best = std::numeric_limits<double>::infinity();
        for (const auto& nd : nodes_) {
            if (!nd.pruned && nd.lp_solved && !nd.branched) {
                best = std::min(best, nd.lower_bound);
            }
        }
        return (best == std::numeric_limits<double>::infinity()) ? incumbent_.load() : best;
    }
};

BranchAndBoundResult BranchAndBoundSolver::solve_parallel(
    const model::Problem& problem,
    const std::chrono::high_resolution_clock::time_point& start_time) {

    BranchAndBoundResult result;
    solve_start_time_ = start_time;

    initialize(problem);

    if (options_.symmetry_breaking) {
        SymmetryDetector detector(tol_);
        SymmetryDetection sym = detector.detect(problem);
        symmetry_rows_ = std::move(sym.ordering_rows);
        symmetry_row_names_ = std::move(sym.row_names);
    }

    ParallelContext ctx(*this, problem, tol_, options_);
    ctx.best_solution_.assign(problem.variables.size(), 0.0);

    BnBNode root;
    root.id = 0;
    root.parent_id = std::numeric_limits<std::size_t>::max();
    root.depth = 0;
    {
        std::lock_guard<std::mutex> lk(ctx.queue_mutex_);
        ctx.nodes_.push_back(std::move(root));
        ctx.id_to_index_.assign(1, 0);
        ctx.next_node_id_ = 1;
        ctx.nodes_created_.store(1);
        // Root is processed by the main thread, not pushed to the worker
        // queue, so do NOT count it in pending_ (only children get counted).
    }

    BnBNode& r = ctx.nodes_[0];
    if (options_.node_presolve && !apply_node_presolve(r, problem)) {
        r.pruned = true;
        ctx.nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
        ctx.nodes_pruned_infeasible_.fetch_add(1, std::memory_order_relaxed);
    }

    if (!r.pruned) {
        if (solve_node_lp(r, problem)) {
            if (is_integral(r.lp_solution, problem)) {
                ctx.commit_incumbent_if_better(r);
                r.pruned = true;
                ctx.nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            if (r.status == model::ProblemStatus::ITER_LIMIT ||
                r.status == model::ProblemStatus::TIME_LIMIT ||
                r.status == model::ProblemStatus::NUMERICAL_ERROR ||
                r.status == model::ProblemStatus::INTERRUPTED) {
                ctx.record_failure(r.status);
            }
            r.pruned = true;
            ctx.nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::vector<std::size_t> root_warm;
    if (!r.pruned && r.lp_solved) {
        int bv = select_branching_variable(r, problem);
        if (bv >= 0) {
            r.branching_var = bv;
            r.branched = true;
            ctx.push_children(r, bv, root_warm);
        } else {
            ctx.commit_incumbent_if_better(r);
            r.pruned = true;
            ctx.nodes_pruned_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    ctx.run_workers(options_.threads, std::move(root_warm));

    if (ctx.pool_failure_) std::rethrow_exception(ctx.pool_failure_);

    if (ctx.had_interrupt_) {
        result.status = model::ProblemStatus::INTERRUPTED;
    } else if (ctx.time_limit_hit_) {
        result.status = model::ProblemStatus::TIME_LIMIT;
    } else if (ctx.node_limit_hit_ || ctx.solution_limit_hit_) {
        result.status = (ctx.incumbent_.load() < std::numeric_limits<double>::infinity())
            ? model::ProblemStatus::SUBOPTIMAL
            : model::ProblemStatus::UNKNOWN;
    } else if (ctx.relaxation_failure_ != model::ProblemStatus::UNKNOWN) {
        result.status = ctx.relaxation_failure_;
        if (ctx.incumbent_.load() < std::numeric_limits<double>::infinity() &&
            result.status != model::ProblemStatus::TIME_LIMIT &&
            result.status != model::ProblemStatus::INTERRUPTED) {
            result.status = model::ProblemStatus::SUBOPTIMAL;
        }
    } else {
        result.status = (ctx.incumbent_.load() < std::numeric_limits<double>::infinity())
            ? model::ProblemStatus::OPTIMAL
            : model::ProblemStatus::INFEASIBLE;
    }

    result.objective_value = ctx.incumbent_.load();
    result.best_bound = ctx.compute_best_bound();
    if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) {
        result.objective_value = -result.objective_value;
        result.best_bound = -result.best_bound;
    }
    {
        double obj = result.objective_value;
        double bnd = result.best_bound;
        double den = std::abs(obj);
        if (den < 1e-10) result.gap = (std::abs(obj - bnd) < 1e-10) ? 0.0 : 1.0;
        else result.gap = std::max(0.0, (obj - bnd) / den);
    }
    result.primal = ctx.best_solution_;
    result.nodes_explored = ctx.nodes_explored_.load();
    result.nodes_pruned = ctx.nodes_pruned_.load();

    result.stats.nodes_created = ctx.nodes_created_.load();
    result.stats.nodes_pruned_by_bound = ctx.nodes_pruned_bound_.load();
    result.stats.nodes_pruned_infeasible = ctx.nodes_pruned_infeasible_.load();
    result.stats.incumbent_improvements = ctx.incumbent_improvements_.load();
    result.stats.heuristic_solutions = ctx.heuristic_solutions_.load();
    result.stats.cuts_generated = ctx.cuts_generated_.load();
    {
        std::lock_guard<std::mutex> lk(ctx.cut_stats_mutex_);
        result.stats.cut_stats = ctx.cut_stats_;
    }
    result.stats.best_bound = result.best_bound;
    {
        std::lock_guard<std::mutex> lk(ctx.bp_mutex_);
        result.stats.bound_progression = ctx.bound_progression_;
    }
    result.stats.cuts_active = cut_pool_.get_active_cuts().size();

    auto end_time = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

} // namespace hypernova::milp