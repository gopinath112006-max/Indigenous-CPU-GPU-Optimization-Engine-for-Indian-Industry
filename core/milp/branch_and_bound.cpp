/**
 * @file branch_and_bound.cpp
 * @brief Branch-and-Bound and Branch-and-Cut Engine for MILP/MIQP.
 * 
 * Mathematical techniques used:
 * - LP/QP relaxation-based bounding
 * - Fractional and Pseudocost branching strategies
 * - Cutting planes (Gomory, MIR, Clique, Cover)
 * - Work-stealing parallel tree search
 */
#include "branch_and_bound.hpp"
#include "../presolve/presolve.hpp"
#include "heuristics.hpp"
#include "node_presolve.hpp"
#include "node_selection.hpp"
#include "symmetry.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <queue>
#include <cmath>
#include <random>
#include <future>

namespace hypernova::milp {

BranchAndBoundSolver::BranchAndBoundSolver(const numerical::ToleranceConfig& tol, const BranchAndBoundOptions& options)
    : tol_(tol), options_(options) {}

BranchAndBoundResult BranchAndBoundSolver::solve(const model::Problem& problem) {
    auto start_time = std::chrono::high_resolution_clock::now();
    solve_start_time_ = start_time;

    BranchAndBoundResult result;

    if (problem.num_integer_vars() == 0) {
        lp::SimplexSolver simplex(tol_);
        auto lp_result = simplex.solve(problem);
        result.status = lp_result.status;
        result.objective_value = lp_result.objective_value;
        result.primal = lp_result.primal;
        result.dual = lp_result.dual;
        result.reduced_costs = lp_result.reduced_costs;
        result.simplex_iterations = lp_result.iterations;
        result.solve_time_ms = lp_result.solve_time_ms;
        return result;
    }

    try {
        if (options_.threads < 2) {
            return solve_serial(problem, start_time);
        }
        return solve_parallel(problem, start_time);
    } catch (const std::exception& e) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
    }

    return result;
}

BranchAndBoundResult BranchAndBoundSolver::solve_serial(const model::Problem& problem,
                                                         const std::chrono::high_resolution_clock::time_point& start_time) {
    BranchAndBoundResult result;
    bool had_interrupt = false;
    bool solution_limit_hit = false;
    bool node_limit_hit = false;
    initialize(problem);

    if (options_.symmetry_breaking) {
            SymmetryDetector detector(tol_);
            SymmetryDetection sym = detector.detect(problem);
            symmetry_rows_ = sym.ordering_rows;
            symmetry_row_names_ = sym.row_names;
        }
        process_root_node(problem);

        if (options_.interrupt_callback && options_.interrupt_callback())
            had_interrupt = true;

        // ordering. HYBRID cycles through DIVE -> BEST_FIRST -> BEST_ESTIMATE.
        auto node_selector = create_selector(options_.branching.node_strategy);
        node_selector->set_incumbent(best_objective_);

        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (!nodes_[i].pruned) {
                node_selector->add_node(&nodes_[i]);
            }
        }

        while (!node_selector->empty() &&
               (options_.node_limit < 0 ||
                nodes_explored_ < static_cast<std::size_t>(options_.node_limit))) {

            if (options_.interrupt_callback && options_.interrupt_callback()) {
                had_interrupt = true;
                break;
            }

            auto elapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start_time).count();
            if (elapsed >= options_.time_limit_seconds) {
                result.status = model::ProblemStatus::TIME_LIMIT;
                break;
            }

            std::vector<BnBNode*> candidates;
            BnBNode* node_ptr = node_selector->select_node(candidates);
            if (!node_ptr) break;
            BnBNode& node = *node_ptr;

            if (node.pruned ||
                (node.lp_solved &&
                 node.lower_bound >= best_objective_ - options_.mip_gap_tolerance * std::abs(best_objective_))) {
                node.pruned = true;
                ++nodes_pruned_;
                ++stats_.nodes_pruned_by_bound;
                continue;
            }

            double old_best = best_objective_;
            process_node(node, problem);
            if (best_objective_ < old_best) {
                node_selector->set_incumbent(best_objective_);
            }
            ++nodes_explored_;

            if (std::getenv("HYPERNOVA_BB_DBG")) {
                std::cerr << "BB node=" << node.id << " lb=" << node.lower_bound
                          << " lp_solved=" << node.lp_solved << " pruned=" << node.pruned
                          << " best=" << best_objective_ << " integ="
                          << (node.lp_solved && is_integral(node.lp_solution, problem) ? 1 : 0)
                          << " x0=" << (node.lp_solution.empty() ? -999.0 : node.lp_solution[0]) << "\n";
            }

            if (!node.pruned) {
                int branch_var = select_branching_variable(node, problem);
                if (branch_var >= 0) {
                    node.branching_var = branch_var;
                    node.branched = true;
                    auto child_indices = branch(node, branch_var, problem);
                    for (std::size_t idx : child_indices) {
                        if (!nodes_[idx].pruned) {
                            node_selector->add_node(&nodes_[idx]);
                        }
                    }
                    // Update node selection incumbent tracking
                    if (node_selector->empty()) break;
                }
            }

            // Rounding/diving/feasibility-pump run every node; RINS is gated
            // internally by its own frequency.
            // Heuristic improvement applies only to nodes with a solved LP that
            // survived the node processing.
            if (!node.pruned && node.lp_solved) {
                apply_heuristics(node, problem);
            }

            // Age the cut pool periodically and drop stale cuts
            if (nodes_explored_ % 50 == 0) {
                cut_pool_.age_cuts();
                cut_pool_.remove_ineffective_cuts();
            }

            // Record bound progression
            if (nodes_explored_ % 100 == 0) {
                stats_.bound_progression.push_back(compute_best_bound());
                if (std::getenv("HYPERNOVA_MILP_DBG")) {
                    std::cerr << "MILP [Node " << nodes_explored_ << "] "
                              << "Pruned: " << nodes_pruned_ << " "
                              << "BestObj: " << best_objective_ << " "
                              << "BestBnd: " << compute_best_bound() << " "
                              << "Gap: " << compute_gap() << " "
                              << "Cuts: " << cut_pool_.get_active_cuts().size() << "\n";
                }
            }

            // Global solution limit: stop once the requested count of integer
            // solutions has been found (honest status handled below).
            if (options_.solution_limit > 0 &&
                stats_.incumbent_improvements >= static_cast<std::size_t>(options_.solution_limit)) {
                solution_limit_hit = true;
                break;
            }
        }

        // Detect an exit imposed by the node limit while the tree is not yet
        // exhausted: the incumbent is a solution, but optimality is unproven.
        if (options_.node_limit >= 0 &&
            nodes_explored_ >= static_cast<std::size_t>(options_.node_limit) &&
            !node_selector->empty()) {
            node_limit_hit = true;
        }

        if (std::getenv("HYPERNOVA_BB_DBG")) {
            std::size_t live = 0;
            for (const auto& n : nodes_) {
                if (!n.pruned && n.lp_solved && !n.branched) ++live;
            }
            std::cerr << "BB exit explored=" << nodes_explored_ << " frontier=" << live
                      << " best=" << best_objective_ << "\n";
        }

        if (had_interrupt) {
            result.status = model::ProblemStatus::INTERRUPTED;
        } else if (solution_limit_hit || node_limit_hit) {
            // User-requested stop before the tree was proven empty: the
            // incumbent (if any) is a valid solution but optimality is not
            // certified. Never claim a false OPTIMAL here.
            result.status = best_objective_ < std::numeric_limits<double>::infinity() ?
                model::ProblemStatus::SUBOPTIMAL : model::ProblemStatus::UNKNOWN;
        } else if (relaxation_failure_ != model::ProblemStatus::UNKNOWN) {
            // A node relaxation failed numerically; optimality cannot be
            // certified even with an incumbent. Surface the failure (prefer
            // the strongest failure status), demoting an incumbent to the
            // solution with SUBOPTIMAL reporting.
            if (result.status == model::ProblemStatus::UNKNOWN) {
                result.status = relaxation_failure_;
            }
            if (best_objective_ < std::numeric_limits<double>::infinity() &&
                result.status != model::ProblemStatus::TIME_LIMIT &&
                result.status != model::ProblemStatus::INTERRUPTED) {
                result.status = model::ProblemStatus::SUBOPTIMAL;
            }
        } else if (result.status == model::ProblemStatus::UNKNOWN) {
            result.status = best_objective_ < std::numeric_limits<double>::infinity() ?
                model::ProblemStatus::OPTIMAL : model::ProblemStatus::INFEASIBLE;
        }
        result.objective_value = best_objective_;
        result.best_bound = compute_best_bound();
        if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) {
            result.objective_value = -result.objective_value;
            result.best_bound = -result.best_bound;
        }
        result.gap = compute_gap();
        result.primal = best_solution_;
        result.nodes_explored = nodes_explored_;
        result.nodes_pruned = nodes_pruned_;
        result.stats = stats_;
        result.stats.nodes_created = nodes_created_;
        result.stats.cuts_active = cut_pool_.get_active_cuts().size();
        result.stats.best_bound = result.best_bound;

        auto end_time = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return result;
}

void BranchAndBoundSolver::initialize(const model::Problem& problem) {
    std::size_t nvars = problem.variables.size();

    nodes_.clear();
    id_to_index_.clear();
    next_node_id_ = 0;
    best_objective_ = std::numeric_limits<double>::infinity();
    best_bound_ = std::numeric_limits<double>::infinity();
    best_solution_.assign(nvars, 0.0);
    nodes_explored_ = 0;
    nodes_pruned_ = 0;
    nodes_created_ = 0;
    stats_ = BnBStatistics{};
    relaxation_failure_ = model::ProblemStatus::UNKNOWN;

    pseudocost_up_.assign(nvars, 1.0);
    pseudocost_down_.assign(nvars, 1.0);
    pseudocount_up_.assign(nvars, 0);
    pseudocount_down_.assign(nvars, 0);
}

void BranchAndBoundSolver::process_root_node(const model::Problem& problem) {
    BnBNode root;
    root.id = next_node_id_++;
    root.parent_id = std::numeric_limits<std::size_t>::max();
    root.depth = 0;
    nodes_.clear();                        // id_to_index_ is stale (reinitialize)
    id_to_index_.assign(next_node_id_, std::numeric_limits<std::size_t>::max());
    id_to_index_[root.id] = 0;
    nodes_.push_back(std::move(root));
    nodes_created_ = 1;
    stats_.nodes_created = 1;

    BnBNode& r = nodes_[0];
    if (options_.node_presolve && !apply_node_presolve(r, problem)) {
        r.pruned = true;
        ++nodes_pruned_;
        ++stats_.nodes_pruned_infeasible;
        return; // root pruned by bound propagation; nothing to solve
    }

    if (solve_node_lp(r, problem)) {
        if (is_integral(r.lp_solution, problem)) {
            update_best_solution(r, problem);
            r.pruned = true;
            ++nodes_pruned_;
        } else {
            std::vector<double> greedy_sol = r.lp_solution;
            for (std::size_t j = 0; j < problem.variables.size(); ++j) {
                if (problem.variables[j].type != model::VarType::CONTINUOUS) {
                    greedy_sol[j] = std::round(greedy_sol[j]);
                    greedy_sol[j] = std::clamp(greedy_sol[j], problem.variables[j].lower_bound, problem.variables[j].upper_bound);
                }
            }
            RoundingHeuristic rh(tol_);
            if (rh.check_feasibility(problem, greedy_sol)) {
                double obj = rh.compute_objective(problem, greedy_sol);
                if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
                if (obj < best_objective_ - tol_.feasibility_tol()) {
                    best_objective_ = obj;
                    best_solution_ = greedy_sol;
                    ++stats_.incumbent_improvements;
                }
            }
            apply_heuristics(r, problem);
        }
    } else if (r.status == model::ProblemStatus::ITER_LIMIT ||
               r.status == model::ProblemStatus::TIME_LIMIT ||
               r.status == model::ProblemStatus::NUMERICAL_ERROR ||
               r.status == model::ProblemStatus::INTERRUPTED) {
        // Relaxation failure: the root is not certified infeasible. Record the
        // failure so the solve surfaces it instead of a false OPTIMAL.
        if (relaxation_failure_ == model::ProblemStatus::UNKNOWN) {
            relaxation_failure_ = r.status;
        }
        ++nodes_pruned_;
    } else {
        ++nodes_pruned_;
        ++stats_.nodes_pruned_infeasible;
    }
}

void BranchAndBoundSolver::process_node(BnBNode& node, const model::Problem& problem) {
    if (!node.lp_solved) {
        if (options_.node_presolve && !apply_node_presolve(node, problem)) {
            node.pruned = true;
            ++nodes_pruned_;
            ++stats_.nodes_pruned_infeasible;
            return;
        }
        if (!solve_node_lp(node, problem)) {
            if (node.status == model::ProblemStatus::ITER_LIMIT ||
                node.status == model::ProblemStatus::TIME_LIMIT ||
                node.status == model::ProblemStatus::NUMERICAL_ERROR ||
                node.status == model::ProblemStatus::INTERRUPTED) {
                // Not certified infeasible: record the failure, do not count it
                // as an infeasible prune (its bound is unusable).
                if (relaxation_failure_ == model::ProblemStatus::UNKNOWN) {
                    relaxation_failure_ = node.status;
                }
                node.pruned = true;
                ++nodes_pruned_;
                return;
            }
            node.pruned = true;
            ++nodes_pruned_;
            ++stats_.nodes_pruned_infeasible;
            return;
        }

        // Pseudocost bookkeeping: the first SOLVED LP solution for a child of a
        // fractional node measures the objective degradation of that branch.
        if (node.parent_id != std::numeric_limits<std::size_t>::max() &&
            node.parent_id < id_to_index_.size()) {
            const std::size_t pidx = id_to_index_[node.parent_id];
            if (pidx < nodes_.size() && nodes_[pidx].branching_var >= 0 &&
                nodes_[pidx].lp_solved) {
                const BnBNode& parent = nodes_[pidx];
                const int bv = parent.branching_var;
                const double pv = parent.lp_solution[static_cast<std::size_t>(bv)];
                const double pfrac = pv - std::floor(pv);
                update_pseudocosts(bv, parent.lower_bound, pfrac, node);
            }
        }
    }

    if (node.lower_bound >= best_objective_ - options_.mip_gap_tolerance * std::abs(best_objective_)) {
        node.pruned = true;
        ++nodes_pruned_;
        ++stats_.nodes_pruned_by_bound;
        return;
    }

    if (is_integral(node.lp_solution, problem)) {
        update_best_solution(node, problem);
        node.pruned = true;
        return;
    }

    apply_cuts(node, problem);

    // Cuts may have driven the re-solved LP to integrality; record it.
    if (node.lp_solved && is_integral(node.lp_solution, problem)) {
        update_best_solution(node, problem);
        node.pruned = true;
        return;
    }

    if (!node.pruned && node.lp_solved) {
        apply_heuristics(node, problem);
    }
}

bool BranchAndBoundSolver::solve_node_lp(BnBNode& node, const model::Problem& problem) {
    model::ProblemBuilder builder;

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        const auto& var = problem.variables[j];
        double lb = var.lower_bound;
        double ub = var.upper_bound;
        for (const auto& bc : node.bounds) {
            if (bc.var == j) {
                if (bc.is_lower) {
                    if (bc.value > lb) lb = bc.value;
                } else {
                    if (bc.value < ub) ub = bc.value;
                }
            }
        }
        builder.add_variable(lb, ub, var.type, var.name);
    }

    for (const auto& con : problem.constraints) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        const auto& A = problem.constraint_matrix;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[con.index]; k < A.row_ptr()[con.index + 1]; ++k) {
                coeffs.push_back({A.col_indices()[k], A.values()[k]});
            }
        }
        builder.add_constraint(coeffs, con.sense, con.rhs, con.name);
    }

    for (const auto& cut : cut_pool_.get_active_cuts()) {
        builder.add_constraint(cut.coefficients, cut.sense, cut.rhs, cut.name);
    }

    for (std::size_t k = 0; k < symmetry_rows_.size(); ++k) {
        builder.add_constraint(symmetry_rows_[k], model::ConstraintSense::GE, 0.0,
                               symmetry_row_names_[k]);
    }

    std::vector<std::pair<std::size_t, double>> obj_coeffs;
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (problem.variables[j].objective_coeff != 0.0) {
            obj_coeffs.push_back({j, problem.variables[j].objective_coeff});
        }
    }
    builder.set_objective(obj_coeffs, problem.obj_sense);

    for (const auto& term : problem.quadratic_terms) {
        builder.add_quadratic_term(term.row, term.col, term.coeff);
    }

    model::Problem node_problem = builder.build();

    model::ProblemStatus relax_status = model::ProblemStatus::UNKNOWN;
    std::vector<double> relax_primal;
    std::vector<double> relax_reduced_costs;
    double relax_objective = 0.0;

    if (options_.qp_relaxation) {
        if (options_.qp_relaxation_solver == QpRelaxationSolver::INTERIOR_POINT) {
            qp::InteriorPointQPOptions qp_opts;
            qp_opts.time_limit_seconds = options_.time_limit_seconds;
            qp_opts.interrupt_callback = options_.interrupt_callback;
            qp::InteriorPointQPSolver qp_solver(tol_, qp_opts);
            auto qp_result = qp_solver.solve(node_problem);
            relax_status = qp_result.status;
            relax_primal = std::move(qp_result.primal);
            relax_objective = qp_result.objective_value;
        } else {
            qp::ActiveSetOptions qp_opts;
            qp_opts.time_limit_seconds = options_.time_limit_seconds;
            qp_opts.interrupt_callback = options_.interrupt_callback;
            qp::ActiveSetQPSolver qp_solver(tol_, qp_opts);
            auto qp_result = qp_solver.solve(node_problem);
            relax_status = qp_result.status;
            relax_primal = std::move(qp_result.primal);
            relax_objective = qp_result.objective_value;
        }
    } else {
        lp::SimplexOptions simplex_opts;
        // Enforce the global B&B deadline on every node relaxation: a single
        // hard LP would otherwise run unbounded and ignore the time limit.
        if (options_.time_limit_seconds > 0.0) {
            double elapsed =
                std::chrono::duration<double>(
                    std::chrono::high_resolution_clock::now() - solve_start_time_).count();
            double remaining = options_.time_limit_seconds - elapsed;
            simplex_opts.time_limit_seconds = remaining > 0.0 ? remaining : 1e-9;
        }
        simplex_opts.interrupt_callback = [this]() {
            if (options_.interrupt_callback && options_.interrupt_callback()) return true;
            if (options_.time_limit_seconds > 0.0) {
                double elapsed =
                    std::chrono::duration<double>(
                        std::chrono::high_resolution_clock::now() - solve_start_time_).count();
                return elapsed >= options_.time_limit_seconds;
            }
            return false;
        };
        lp::SimplexSolver simplex(tol_, simplex_opts);
        lp::SimplexResult lp_result;
        if (node.lp_solved && !node.basis_var_status.empty()) {
            lp_result = simplex.solve_with_basis(node_problem, node.basis_var_status, node.basis_con_status);
        } else {
            lp_result = simplex.solve(node_problem);
        }
        // The interrupt callback aborts with INTERRUPTED; if the global deadline
        // is what tripped it, report the honest TIME_LIMIT status.
        if (lp_result.status == model::ProblemStatus::INTERRUPTED &&
            options_.time_limit_seconds > 0.0) {
            double elapsed =
                std::chrono::duration<double>(
                    std::chrono::high_resolution_clock::now() - solve_start_time_).count();
            if (elapsed >= options_.time_limit_seconds) {
                lp_result.status = model::ProblemStatus::TIME_LIMIT;
            }
        }
        relax_status = lp_result.status;
        relax_primal = std::move(lp_result.primal);
        relax_reduced_costs = std::move(lp_result.reduced_costs);
        relax_objective = lp_result.objective_value;
        node.basis_var_status = std::move(lp_result.basis_status);
    }

    if (relax_status == model::ProblemStatus::OPTIMAL ||
        relax_status == model::ProblemStatus::SUBOPTIMAL) {
        node.lp_solution = std::move(relax_primal);
        node.lower_bound = relax_objective;
        if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) {
            node.lower_bound = -node.lower_bound;
        }
        node.lp_solved = true;
        node.status = model::ProblemStatus::OPTIMAL;

        // Apply LP-relaxation-aware reduced-cost fixing (Dual Reductions)
        if (best_objective_ < std::numeric_limits<double>::infinity() && 
            problem.quadratic_terms.empty() && 
            !relax_reduced_costs.empty()) {
            
            double gap = best_objective_ - node.lower_bound;
            if (gap >= 0.0) {
                for (std::size_t j = 0; j < problem.variables.size(); ++j) {
                    // Only original variables, skip slack/aux variables if they were somehow included,
                    // but relax_reduced_costs aligns with node_problem variables.
                    // We only tighten bounds for the original problem variables (j < problem.variables.size()).
                    if (j >= relax_reduced_costs.size()) continue;

                    double rc_j = relax_reduced_costs[j];
                    double eff_rc = (problem.obj_sense == model::ObjectiveSense::MINIMIZE) ? rc_j : -rc_j;
                    
                    if (std::abs(eff_rc) > tol_.feasibility_tol()) {
                        double delta_max = gap / std::abs(eff_rc);
                        double lp_val = node.lp_solution[j];
                        
                        double lb = problem.variables[j].lower_bound;
                        double ub = problem.variables[j].upper_bound;
                        for (const auto& bc : node.bounds) {
                            if (bc.var == j) {
                                if (bc.is_lower) { if (bc.value > lb) lb = bc.value; }
                                else { if (bc.value < ub) ub = bc.value; }
                            }
                        }
                        
                        if (eff_rc > 0.0) {
                            double new_ub = lp_val + delta_max;
                            if (problem.variables[j].type != model::VarType::CONTINUOUS) {
                                new_ub = std::floor(new_ub + tol_.feasibility_tol());
                            }
                            if (new_ub < ub - tol_.feasibility_tol()) {
                                node.bounds.push_back({j, new_ub, false});
                            }
                        } else {
                            double new_lb = lp_val - delta_max;
                            if (problem.variables[j].type != model::VarType::CONTINUOUS) {
                                new_lb = std::ceil(new_lb - tol_.feasibility_tol());
                            }
                            if (new_lb > lb + tol_.feasibility_tol()) {
                                node.bounds.push_back({j, new_lb, true});
                            }
                        }
                    }
                }
            }
        }

        return true;
    }
    // INFEASIBLE / UNBOUNDED: the node is certified infeasible (or the
    // relaxation is unbounded), so it is safely pruned here.
    node.status = relax_status;
    node.pruned = true;
    return false;
}

bool BranchAndBoundSolver::apply_node_presolve(BnBNode& node, const model::Problem& problem) {
    if (problem.num_integer_vars() == 0) return true;
    NodePresolver presolver(tol_);
    NodePresolveResult pr = presolver.tighten(problem, node.bounds);
    if (!pr.feasible) return false;
    for (const auto& [j, lb] : pr.tightened_lb) node.bounds.push_back({j, lb, true});
    for (const auto& [j, ub] : pr.tightened_ub) node.bounds.push_back({j, ub, false});
    return true;
}

int BranchAndBoundSolver::select_branching_variable(const BnBNode& node, const model::Problem& problem) {
    // Parallel mode reads pseudocost state from several workers concurrently;
    // reads are shared-locked. Uncontended in serial mode.
    std::shared_lock<std::shared_mutex> lock(pseudocost_mutex_);

    std::size_t nvars = problem.variables.size();
    if (node.lp_solution.size() != nvars) return -1;

    struct Candidate {
        std::size_t var;
        double frac;
        double pseudo_score;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(nvars);

    for (std::size_t j = 0; j < nvars; ++j) {
        if (problem.variables[j].type == model::VarType::CONTINUOUS) continue;

        double val = node.lp_solution[j];
        double frac = val - std::floor(val);
        if (frac < tol_.feasibility_tol() || frac > 1.0 - tol_.feasibility_tol()) continue;

        double up_score = pseudocost_up_[j] * (1.0 - frac);
        double down_score = pseudocost_down_[j] * frac;
        double pseudo_score = options_.branching.pseudocost_weight * std::max(up_score, down_score) +
                              (1.0 - options_.branching.pseudocost_weight) * std::min(up_score, down_score);
        candidates.push_back({j, frac, pseudo_score});
    }
    if (candidates.empty()) return -1;

    const auto strategy = options_.branching.strategy;
    if (strategy == BranchingStrategy::MOST_FRACTIONAL) {
        int best_var = -1;
        double best_score = -1.0;
        for (const auto& cd : candidates) {
            double score = std::min(cd.frac, 1.0 - cd.frac);
            if (score > best_score) {
                best_score = score;
                best_var = static_cast<int>(cd.var);
            }
        }
        return best_var;
    }
    if (strategy == BranchingStrategy::PSEUDOCOST) {
        int best_var = -1;
        double best_score = -1.0;
        for (const auto& cd : candidates) {
            if (cd.pseudo_score > best_score) {
                best_score = cd.pseudo_score;
                best_var = static_cast<int>(cd.var);
            }
        }
        return best_var;
    }

    // STRONG_BRANCHING / RELIABILITY: strong branching costs two LP solves per
    // candidate, so only evaluate the most promising candidates (by pseudocost
    // score); the rest fall back to their pseudocost score. Without this cap a
    // root node can trigger hundreds of LP solves.
    const int cap = options_.branching.strong_branch_candidates;
    if (cap > 0 && candidates.size() > static_cast<std::size_t>(cap)) {
        std::partial_sort(candidates.begin(), candidates.begin() + cap, candidates.end(),
                          [](const Candidate& a, const Candidate& b) {
                              return a.pseudo_score > b.pseudo_score;
                          });
        candidates.resize(static_cast<std::size_t>(cap));
    }

    int best_var = -1;
    double best_score = -1.0;
    for (const auto& cd : candidates) {
        double strong_score = cd.pseudo_score;
        if (strategy == BranchingStrategy::STRONG_BRANCHING ||
            pseudocount_up_[cd.var] < 5 || pseudocount_down_[cd.var] < 5) {
            auto bounds = strong_branching(node, static_cast<int>(cd.var), problem);
            strong_score = std::min(bounds[0], bounds[1]);
        }
        double score = (strategy == BranchingStrategy::STRONG_BRANCHING)
                           ? strong_score
                           : 0.5 * cd.pseudo_score + 0.5 * strong_score;
        if (score > best_score) {
            best_score = score;
            best_var = static_cast<int>(cd.var);
        }
    }

    return best_var;
}

std::pair<double, double> BranchAndBoundSolver::estimate_child_bounds(const BnBNode& node, int var, const model::Problem& /*problem*/) {
    double val = node.lp_solution[var];
    double frac = val - std::floor(val);

    double down_change = frac;
    double up_change = 1.0 - frac;

    double down_bound = node.lower_bound + pseudocost_down_[var] * down_change;
    double up_bound = node.lower_bound + pseudocost_up_[var] * up_change;

    return {down_bound, up_bound};
}

std::vector<std::size_t> BranchAndBoundSolver::branch(const BnBNode& node, int var, const model::Problem& /*problem*/) {
    double val = node.lp_solution[var];
    double floor_val = std::floor(val);
    double ceil_val = std::ceil(val);

    BnBNode down_child;
    down_child.id = next_node_id_++;
    down_child.parent_id = node.id;
    down_child.depth = node.depth + 1;
    down_child.bounds = node.bounds;
    down_child.bounds.push_back({static_cast<std::size_t>(var), floor_val, false});
    down_child.upper_bound = floor_val;

    BnBNode up_child;
    up_child.id = next_node_id_++;
    up_child.parent_id = node.id;
    up_child.depth = node.depth + 1;
    up_child.bounds = node.bounds;
    up_child.bounds.push_back({static_cast<std::size_t>(var), ceil_val, true});
    up_child.lower_bound = ceil_val;

    if (!node.basis_var_status.empty()) {
        down_child.basis_var_status = node.basis_var_status;
        down_child.basis_con_status = node.basis_con_status;
        up_child.basis_var_status = node.basis_var_status;
        up_child.basis_con_status = node.basis_con_status;
    }

    const std::size_t down_idx = nodes_.size();
    const std::size_t up_idx = nodes_.size() + 1;
    id_to_index_.resize(std::max(id_to_index_.size(), up_child.id + 1),
                        std::numeric_limits<std::size_t>::max());
    id_to_index_[down_child.id] = down_idx;
    id_to_index_[up_child.id] = up_idx;

    nodes_.push_back(std::move(down_child));
    nodes_.push_back(std::move(up_child));
    nodes_created_ += 2;
    stats_.nodes_created += 2;

    return {down_idx, up_idx};
}

void BranchAndBoundSolver::update_pseudocosts(int var, double parent_lb, double parent_frac,
                                              const BnBNode& child) {
    // child is either the down (x <= floor) or up (x >= ceil) branch of var.
    if (!child.lp_solved) return;

    // Parallel workers update branching statistics for disjoint child nodes;
    // the shared vectors are written exclusively.
    std::unique_lock<std::shared_mutex> lock(pseudocost_mutex_);

    bool is_down = false;
    for (const auto& bc : child.bounds) {
        if (bc.var == static_cast<std::size_t>(var)) {
            is_down = !bc.is_lower;
            break;
        }
    }

    const double degradation = std::max(0.0, child.lower_bound - parent_lb);
    const double step = is_down ? std::max(parent_frac, 1e-6)
                                : std::max(1.0 - parent_frac, 1e-6);
    const double per_unit = degradation / step;
    if (!std::isfinite(per_unit) || per_unit < 0.0) return;

    int& count = is_down ? pseudocount_down_[var] : pseudocount_up_[var];
    double& cost = is_down ? pseudocost_down_[var] : pseudocost_up_[var];
    cost = (cost * count + per_unit) / (count + 1);
    ++count;
}

void BranchAndBoundSolver::apply_heuristics(const BnBNode& node, const model::Problem& problem) {
    if (options_.qp_relaxation) return;

    auto try_incumbent = [&](double obj_min, const std::vector<double>& sol) {
        if (!is_integral(sol, problem)) return;  // fractional points are not incumbents
        if (obj_min < best_objective_ - tol_.feasibility_tol()) {
            best_objective_ = obj_min;
            best_solution_ = sol;
            ++stats_.heuristic_solutions;
        }
    };

    if (options_.heuristics.rounding) {
        RoundingHeuristic rounding(tol_);
        auto res = rounding.run(problem, node.lp_solution);
        if (res.found_solution) {
            double obj = res.objective_value;
            if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
            try_incumbent(obj, res.solution);
        }
    }

    if (options_.heuristics.diving) {
        DivingHeuristic diving(tol_, options_.heuristics.diving_iterations);
        auto res = diving.run(problem, node.lp_solution);
        if (res.found_solution) {
            double obj = res.objective_value;
            if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
            try_incumbent(obj, res.solution);
        }
    }

    if (options_.heuristics.feasibility_pump) {
        FeasibilityPumpHeuristic fp(tol_);
        auto res = fp.run(problem, node.lp_solution);
        if (res.found_solution && !res.solution.empty()) {
            double obj = res.objective_value;
            if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
            try_incumbent(obj, res.solution);
        }
    }

    if (options_.heuristics.rins &&
        best_objective_ < std::numeric_limits<double>::infinity() &&
        !best_solution_.empty() &&
        (nodes_explored_ % options_.heuristics.rins_frequency == 0)) {
        RINSHeuristic rins(tol_, options_.heuristics.rins_frequency);
        rins.set_incumbent(best_solution_);
        auto res = rins.run(problem, node.lp_solution);
        if (res.found_solution && !res.solution.empty()) {
            double obj = res.objective_value;
            if (problem.obj_sense == model::ObjectiveSense::MAXIMIZE) obj = -obj;
            try_incumbent(obj, res.solution);
        }
    }
}

void BranchAndBoundSolver::apply_cuts(BnBNode& node, const model::Problem& problem) {
    if (options_.qp_relaxation) return;
    if (!options_.cuts.gomory_cuts && !options_.cuts.mir_cuts &&
        !options_.cuts.knapsack_cuts && !options_.cuts.clique_cuts &&
        !options_.cuts.flow_cover_cuts) {
        return;
    }

    CutGenerator generator(tol_);

    std::vector<Cut> candidates;
    auto tally = [&](const std::vector<Cut>& c, std::size_t CutSeparationStatistics::*per) {
        (this->stats_.cut_stats.*per) += c.size();
        candidates.insert(candidates.end(), c.begin(), c.end());
    };
    if (options_.cuts.gomory_cuts) {
        tally(generator.generate_gomory_cuts(problem, node.lp_solution),
              &CutSeparationStatistics::gomory_candidates);
    }
    if (options_.cuts.mir_cuts) {
        tally(generator.generate_mir_cuts(problem, node.lp_solution),
              &CutSeparationStatistics::mir_candidates);
    }
    if (options_.cuts.knapsack_cuts) {
        tally(generator.generate_knapsack_covers(problem, node.lp_solution),
              &CutSeparationStatistics::knapsack_candidates);
    }
    if (options_.cuts.clique_cuts) {
        tally(generator.generate_clique_cuts(problem, node.lp_solution),
              &CutSeparationStatistics::clique_candidates);
    }
    if (options_.cuts.flow_cover_cuts) {
        tally(generator.generate_flow_covers(problem, node.lp_solution),
              &CutSeparationStatistics::flow_candidates);
    }

    int added_count = 0;
    if (std::getenv("HYPERNOVA_BB_DBG")) {
        std::cerr << "BB apply_cuts node=" << node.id << " lp_frac="
                  << !is_integral(node.lp_solution, problem) << " candidates="
                  << candidates.size() << " threshold=" << options_.cuts.cut_efficacy_threshold << "\n";
    }
    for (auto& cut : candidates) {
        if (std::getenv("HYPERNOVA_BB_DBG")) {
            std::cerr << "BB cut candidate eff=" << cut.efficacy << " " << cut.name << ": ";
            for (const auto& [j, a] : cut.coefficients) std::cerr << j << ":" << a << " ";
            std::cerr << static_cast<int>(cut.sense) << " " << cut.rhs << "\n";
        }
        if (cut.efficacy < options_.cuts.cut_efficacy_threshold) continue;
        cut_pool_.add_cut(cut);
        ++added_count;
        ++stats_.cuts_generated;
        // Attribute the added cut to its family by name prefix.
        if (cut.name.rfind("gomory", 0) == 0) ++stats_.cut_stats.gomory_added;
        else if (cut.name.rfind("mir_", 0) == 0) ++stats_.cut_stats.mir_added;
        else if (cut.name.rfind("knapsack_cover", 0) == 0) ++stats_.cut_stats.knapsack_added;
        else if (cut.name.rfind("clique", 0) == 0) ++stats_.cut_stats.clique_added;
        else if (cut.name.rfind("flow_cover", 0) == 0) ++stats_.cut_stats.flow_added;
        if (added_count >= options_.cuts.max_cuts_per_node) break;
    }

    if (added_count > 0) {
        if (!solve_node_lp(node, problem)) {
            ++stats_.nodes_pruned_infeasible;
        }
    }
}

bool BranchAndBoundSolver::is_integral(const std::vector<double>& solution, const model::Problem& problem) const {
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (problem.variables[j].type != model::VarType::CONTINUOUS) {
            double val = solution[j];
            double nearest = std::round(val);
            if (std::abs(val - nearest) > tol_.integrality_tol()) {
                return false;
            }
        }
    }
    return true;
}

double BranchAndBoundSolver::compute_best_bound() const {
    double bound = std::numeric_limits<double>::infinity();
    for (const auto& node : nodes_) {
        // A branched node is already expanded into its children, so its lower
        // bound is not part of the remaining frontier.
        if (node.pruned || node.branched) continue;
        double lb = node.lower_bound;
        if (!std::isfinite(lb)) {
            // An unsolved frontier node inherits the bound of its nearest solved
            // ancestor: branching only tightens the feasible region, so that
            // ancestor's LP bound remains a valid lower bound for its subtree.
            const std::size_t self_id = node.id;
            std::size_t pid = node.parent_id;
            std::size_t guard = 0;
            while (pid < id_to_index_.size() && guard++ < nodes_.size()) {
                std::size_t pidx = id_to_index_[pid];
                if (pidx >= nodes_.size()) break;
                const BnBNode& parent = nodes_[pidx];
                if (parent.id == self_id) break;
                if (std::isfinite(parent.lower_bound)) { lb = parent.lower_bound; break; }
                if (parent.id == parent.parent_id || parent.parent_id == pid) break;
                pid = parent.parent_id;
            }
        }
        if (std::isfinite(lb)) bound = std::min(bound, lb);
    }
    if (bound == std::numeric_limits<double>::infinity()) {
        bound = best_objective_;
    }
    return bound;
}

double BranchAndBoundSolver::compute_gap() const {
    if (best_objective_ == std::numeric_limits<double>::infinity()) {
        return std::numeric_limits<double>::infinity();
    }
    double bound = compute_best_bound();
    if (bound == std::numeric_limits<double>::infinity()) return 0.0;

    double denominator = std::abs(best_objective_);
    if (denominator < 1e-10) {
        return (std::abs(best_objective_ - bound) < 1e-10) ? 0.0 : 1.0;
    }
    return std::max(0.0, (best_objective_ - bound) / denominator);
}

void BranchAndBoundSolver::update_best_solution(const BnBNode& node, const model::Problem& /*problem*/) {
    double obj = node.lower_bound;
    if (obj < best_objective_ - tol_.feasibility_tol()) {
        if (std::getenv("HYPERNOVA_BB_DBG"))
            std::cerr << "BB update best " << best_objective_ << " -> " << obj << " (node " << node.id << ")\n";
        best_objective_ = obj;
        best_solution_ = node.lp_solution;
        ++stats_.incumbent_improvements;
    }
}

std::vector<double> BranchAndBoundSolver::pseudocost_branching(int var) const {
    return {pseudocost_up_[var], pseudocost_down_[var]};
}

std::vector<double> BranchAndBoundSolver::strong_branching(const BnBNode& node, int var, const model::Problem& problem) {
    double val = node.lp_solution[var];
    double floor_val = std::floor(val);
    double ceil_val = std::ceil(val);

    BnBNode down_child;
    down_child.id = next_node_id_;
    down_child.parent_id = node.id;
    down_child.depth = node.depth + 1;
    down_child.bounds = node.bounds;
    down_child.bounds.push_back({static_cast<std::size_t>(var), floor_val, false});

    BnBNode up_child;
    up_child.id = next_node_id_ + 1;
    up_child.parent_id = node.id;
    up_child.depth = node.depth + 1;
    up_child.bounds = node.bounds;
    up_child.bounds.push_back({static_cast<std::size_t>(var), ceil_val, true});

    double down_degrad = 0.0;
    double up_degrad = 0.0;

    if (options_.threads >= 2) {
        auto down_future = std::async(std::launch::async, [&]() {
            bool success = solve_node_lp(down_child, problem);
            return success ? std::max(0.0, down_child.lower_bound - node.lower_bound) : std::numeric_limits<double>::infinity();
        });
        auto up_future = std::async(std::launch::async, [&]() {
            bool success = solve_node_lp(up_child, problem);
            return success ? std::max(0.0, up_child.lower_bound - node.lower_bound) : std::numeric_limits<double>::infinity();
        });
        down_degrad = down_future.get();
        up_degrad = up_future.get();
    } else {
        if (solve_node_lp(down_child, problem)) {
            down_degrad = std::max(0.0, down_child.lower_bound - node.lower_bound);
        } else {
            down_degrad = std::numeric_limits<double>::infinity();
        }

        if (solve_node_lp(up_child, problem)) {
            up_degrad = std::max(0.0, up_child.lower_bound - node.lower_bound);
        } else {
            up_degrad = std::numeric_limits<double>::infinity();
        }
    }

    return {down_degrad, up_degrad};
}

double BranchAndBoundSolver::reliability_score(int var, const model::Problem& /*problem*/) const {
    return 0.5 * pseudocost_up_[var] + 0.5 * pseudocost_down_[var];
}

} // namespace hypernova::milp