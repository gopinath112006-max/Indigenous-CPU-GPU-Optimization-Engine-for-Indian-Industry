#include <hypernova/api.hpp>
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>
#include <lp/simplex.hpp>
#include <lp/interior_point.hpp>
#include <milp/branch_and_bound.hpp>
#include <qp/active_set.hpp>
#include <qp/interior_point_qp.hpp>
#include <presolve/presolve.hpp>
#include <validation/solution_verifier.hpp>
#include <validation/iis.hpp>
#include <validation/solve_report.hpp>
#include <validation/diagnostics.hpp>
#include <execution/gpu_backend.hpp>
#include <numerical/scaling.hpp>
#include <chrono>
#include <memory>
#include <iostream>
#include <optional>
#include <memory>
#include <iostream>

namespace hypernova {

namespace {

// Map the public API's strategy enums to the MILP solver options. The public
// surface mirrors the solver internals one-to-one so no option is silently
// ignored (Phase 4: node-selection/branching must affect execution).
milp::NodeSelectionStrategy node_selection_strategy(NodeSelectionStrategy s) {
    switch (s) {
        case NodeSelectionStrategy::BEST_FIRST: return milp::NodeSelectionStrategy::BEST_FIRST;
        case NodeSelectionStrategy::DEPTH_FIRST: return milp::NodeSelectionStrategy::DEPTH_FIRST;
        case NodeSelectionStrategy::BEST_ESTIMATE: return milp::NodeSelectionStrategy::BEST_ESTIMATE;
        case NodeSelectionStrategy::HYBRID: return milp::NodeSelectionStrategy::HYBRID;
    }
    return milp::NodeSelectionStrategy::HYBRID;
}

milp::BranchingStrategy branching_strategy(BranchingStrategy s) {
    switch (s) {
        case BranchingStrategy::MOST_FRACTIONAL: return milp::BranchingStrategy::MOST_FRACTIONAL;
        case BranchingStrategy::PSEUDOCOST: return milp::BranchingStrategy::PSEUDOCOST;
        case BranchingStrategy::STRONG_BRANCHING: return milp::BranchingStrategy::STRONG_BRANCHING;
        case BranchingStrategy::RELIABILITY: return milp::BranchingStrategy::RELIABILITY;
    }
    return milp::BranchingStrategy::RELIABILITY;
}

} // namespace

struct Solver::Impl {
    SolverOptions options;
    bool interrupted_ = false;

    lp::SimplexOptions make_simplex_options(double budget) const {
        lp::SimplexOptions o;
        o.time_limit_seconds = budget;
        o.interrupt_callback = [this]() { return interrupted_; };
        o.pricing = options.simplex_pricing;
        o.ratio_test = options.simplex_ratio_test;
        o.bland_rule = options.simplex_bland_rule;
        o.steepest_edge_shortlist = options.simplex_steepest_edge_shortlist;
        return o;
    }
};

Solver::Solver(const SolverOptions& options) : pimpl_(std::make_unique<Impl>()) {
    pimpl_->options = options;
}

Solver::~Solver() = default;

Solution Solver::solve(const model::Problem& problem) {
    auto global_start = std::chrono::high_resolution_clock::now();
    if (pimpl_->interrupted_) {
        pimpl_->interrupted_ = false;
        Solution solution;
        solution.status = model::ProblemStatus::INTERRUPTED;
        return solution;
    }

    Solution solution;

    auto elapsed_ms = [&]() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - global_start).count();
    };

auto recompute_objective = [&](const std::vector<double>& primal) {
        double obj = problem.obj_offset;
        for (std::size_t j = 0; j < problem.variables.size() && j < primal.size(); ++j) {
            obj += problem.variables[j].objective_coeff * primal[j];
        }
        for (const auto& term : problem.quadratic_terms) {
            if (term.row >= primal.size() || term.col >= primal.size()) continue;
            if (term.row == term.col) {
                obj += 0.5 * term.coeff * primal[term.row] * primal[term.col];
            } else {
                obj += term.coeff * primal[term.row] * primal[term.col];
            }
        }
        return obj;
    };

    double remaining_time = pimpl_->options.time_limit_seconds;
    
    std::shared_ptr<execution::IComputeBackend> compute_backend;
    if (!pimpl_->options.use_gpu || pimpl_->options.compute_target == ComputeTarget::CPU_ONLY) {
        compute_backend = execution::ComputeBackendFactory::create_backend(execution::ComputeBackendType::CPU);
    } else if (pimpl_->options.compute_target == ComputeTarget::CPU_GPU_FORCE) {
        compute_backend = execution::ComputeBackendFactory::create_backend(execution::ComputeBackendType::CUDA);
    } else {
        compute_backend = execution::ComputeBackendFactory::create_backend(execution::ComputeBackendType::AUTO);
    }

    const model::Problem* solve_problem = &problem;
    std::unique_ptr<presolve::Presolver> presolver;
    std::optional<presolve::PresolveResult> presolve_result;

    auto postsolve = [&](const std::vector<double>& primal,
                         const std::vector<double>& dual,
                         const std::vector<double>& reduced_costs) {
        model::Solution s;
        s.primal = primal;
        s.dual = dual;
        s.reduced_costs = reduced_costs;
        return s;
    };

auto solve_dispatch = [&](const model::Problem& target) {
        // Distinguish "unlimited" (limit==0) from "budget already exhausted"
        // (limit>0 but remaining<=0): a 0 limit means unlimited to the solvers,
        // so an exhausted budget must be expressed as a token positive limit to
        // keep TIME_LIMIT semantics instead of running fallbacks forever.
        double dispatch_budget = remaining_time;
        if (pimpl_->options.time_limit_seconds > 0.0 && remaining_time <= 0.0) {
            dispatch_budget = 1e-9;
        }
        bool presolved_solution = presolver && presolve_result;
        if (target.is_lp()) {
            if (pimpl_->options.engine == EngineType::INTERIOR_POINT ||
                (pimpl_->options.engine == EngineType::AUTO &&
                 target.variables.size() > 10000)) {
                lp::InteriorPointOptions ipm_opts;
                ipm_opts.time_limit_seconds = dispatch_budget;
                ipm_opts.interrupt_callback = [this]() { return pimpl_->interrupted_; };
                ipm_opts.compute_backend = compute_backend;
                lp::InteriorPointSolver ipm_solver(pimpl_->options.tolerances, ipm_opts);
                auto result = ipm_solver.solve(target);
                if (presolved_solution &&
                    result.status != model::ProblemStatus::TIME_LIMIT &&
                    result.status != model::ProblemStatus::INTERRUPTED &&
                    result.status != model::ProblemStatus::NUMERICAL_ERROR) {
                    auto full = presolver->postsolve(problem,
                                                     postsolve(result.primal, result.dual, result.reduced_costs),
                                                     *presolve_result);
                    solution.primal = std::move(full.primal);
                    solution.dual = std::move(full.dual);
                    solution.reduced_costs = std::move(full.reduced_costs);
                    solution.objective_value = recompute_objective(solution.primal);
                } else {
                    solution.primal = std::move(result.primal);
                    solution.dual = std::move(result.dual);
                    solution.reduced_costs = std::move(result.reduced_costs);
                    solution.objective_value = result.objective_value;
                }
                solution.status = result.status;
                solution.ipm_iterations = result.iterations;
} else {
lp::SimplexOptions simplex_opts = pimpl_->make_simplex_options(dispatch_budget);
                if (pimpl_->options.engine == EngineType::PRIMAL_SIMPLEX) {
                    simplex_opts.algorithm = lp::SimplexAlgorithm::PRIMAL;
                } else if (pimpl_->options.engine == EngineType::DUAL_SIMPLEX) {
                    simplex_opts.algorithm = lp::SimplexAlgorithm::DUAL;
                }
                lp::SimplexSolver simplex_solver(pimpl_->options.tolerances, simplex_opts);
                auto result = simplex_solver.solve(target);

                // AUTO fallback: on a degenerate simplex failure (cycling one
                // the scatter/dense paths disagree on, or a hard instance),
                // retry with the interior-point engine using the remaining time.
                if (pimpl_->options.engine == EngineType::AUTO &&
                    (result.status == model::ProblemStatus::TIME_LIMIT ||
                     result.status == model::ProblemStatus::ITER_LIMIT ||
                     result.status == model::ProblemStatus::NUMERICAL_ERROR) &&
                    (remaining_time > 0.0 || pimpl_->options.time_limit_seconds <= 0.0)) {
                    if (remaining_time > 0.0) {
                        remaining_time -= elapsed_ms();
                        if (remaining_time < 0.0) remaining_time = 0.0;
                    }
                    // time_limit_seconds == 0 means "unlimited"; never pass an
                    // exhausted (0) budget to the fallback when a limit was set,
                    // or the barrier would run unbounded past the deadline.
                    if (pimpl_->options.time_limit_seconds > 0.0 && remaining_time <= 0.0) {
                        remaining_time = 1e-9;
                    }
                    lp::InteriorPointOptions ipm_opts;
                    ipm_opts.time_limit_seconds = remaining_time;
                    ipm_opts.interrupt_callback = [this]() { return pimpl_->interrupted_; };
                    ipm_opts.compute_backend = compute_backend;
                    lp::InteriorPointSolver ipm_solver(pimpl_->options.tolerances, ipm_opts);
                    auto ipm_result = ipm_solver.solve(target);
                    if (ipm_result.status == model::ProblemStatus::OPTIMAL) {
                        result.status = std::move(ipm_result.status);
                        result.objective_value = std::move(ipm_result.objective_value);
                        result.primal = std::move(ipm_result.primal);
                        result.dual = std::move(ipm_result.dual);
                        result.reduced_costs = std::move(ipm_result.reduced_costs);
                        result.iterations = std::move(ipm_result.iterations);
                        result.solve_time_ms = std::move(ipm_result.solve_time_ms);
                    }
                }

                if (presolved_solution &&
                    result.status != model::ProblemStatus::TIME_LIMIT &&
                    result.status != model::ProblemStatus::INTERRUPTED &&
                    result.status != model::ProblemStatus::NUMERICAL_ERROR) {
                    auto full = presolver->postsolve(problem,
                                                     postsolve(result.primal, result.dual, result.reduced_costs),
                                                     *presolve_result);
                    solution.primal = std::move(full.primal);
                    solution.dual = std::move(full.dual);
                    solution.reduced_costs = std::move(full.reduced_costs);
                    solution.objective_value = recompute_objective(solution.primal);
                } else {
                    solution.primal = std::move(result.primal);
                    solution.dual = std::move(result.dual);
                    solution.reduced_costs = std::move(result.reduced_costs);
                    solution.objective_value = result.objective_value;
                }
                solution.status = result.status;
                solution.basis_status = std::move(result.basis_status);
                solution.simplex_iterations = result.iterations;
            }
} else if (target.is_milp()) {
            milp::BranchAndBoundOptions bb_opts;
            bb_opts.time_limit_seconds = remaining_time;
            bb_opts.interrupt_callback = [this]() { return pimpl_->interrupted_; };
            bb_opts.mip_gap_tolerance = pimpl_->options.mip_gap_tolerance;
            bb_opts.threads = pimpl_->options.thread_count;
            bb_opts.use_gpu = pimpl_->options.use_gpu;
            if (pimpl_->options.node_limit > 0) bb_opts.node_limit = static_cast<int>(pimpl_->options.node_limit);
            if (pimpl_->options.solution_limit > 0) bb_opts.solution_limit = static_cast<int>(pimpl_->options.solution_limit);
            bb_opts.branching.node_strategy = node_selection_strategy(pimpl_->options.node_selection);
            bb_opts.branching.strategy = branching_strategy(pimpl_->options.branching);
            bb_opts.heuristics.feasibility_pump = pimpl_->options.heuristic_feasibility_pump;
            bb_opts.heuristics.rins = pimpl_->options.heuristic_rins;
            bb_opts.heuristics.rins_frequency = pimpl_->options.rins_frequency;
            if (pimpl_->options.engine == EngineType::BRANCH_AND_CUT) {
                bb_opts.cuts.gomory_cuts = true;
                bb_opts.cuts.mir_cuts = true;
                bb_opts.cuts.knapsack_cuts = true;
                bb_opts.cuts.clique_cuts = true;
            }
            milp::BranchAndBoundSolver bb_solver(pimpl_->options.tolerances, bb_opts);
            auto result = bb_solver.solve(target);
            if (presolved_solution &&
                result.status != model::ProblemStatus::TIME_LIMIT &&
                    result.status != model::ProblemStatus::INTERRUPTED &&
                    result.status != model::ProblemStatus::NUMERICAL_ERROR) {
                auto full = presolver->postsolve(problem, postsolve(result.primal, {}, {}), *presolve_result);
                solution.primal = std::move(full.primal);
                solution.objective_value = recompute_objective(solution.primal);
            } else {
                solution.primal = std::move(result.primal);
                solution.objective_value = result.objective_value;
            }
solution.status = result.status;
            solution.best_bound = result.best_bound;
            solution.gap = result.gap;
            solution.bb_nodes = result.nodes_explored;
        } else if (target.is_miqp()) {
            milp::BranchAndBoundOptions bb_opts;
            bb_opts.time_limit_seconds = remaining_time;
            bb_opts.interrupt_callback = [this]() { return pimpl_->interrupted_; };
            bb_opts.mip_gap_tolerance = pimpl_->options.mip_gap_tolerance;
            bb_opts.threads = pimpl_->options.thread_count;
            bb_opts.use_gpu = pimpl_->options.use_gpu;
            if (pimpl_->options.node_limit > 0) bb_opts.node_limit = static_cast<int>(pimpl_->options.node_limit);
            if (pimpl_->options.solution_limit > 0) bb_opts.solution_limit = static_cast<int>(pimpl_->options.solution_limit);
            bb_opts.branching.node_strategy = node_selection_strategy(pimpl_->options.node_selection);
            bb_opts.branching.strategy = branching_strategy(pimpl_->options.branching);
            bb_opts.heuristics.feasibility_pump = pimpl_->options.heuristic_feasibility_pump;
            bb_opts.heuristics.rins = pimpl_->options.heuristic_rins;
            bb_opts.heuristics.rins_frequency = pimpl_->options.rins_frequency;
            bb_opts.qp_relaxation = true;
            bb_opts.qp_relaxation_solver =
                (pimpl_->options.engine == EngineType::QP_INTERIOR_POINT)
                    ? milp::QpRelaxationSolver::INTERIOR_POINT
                    : milp::QpRelaxationSolver::ACTIVE_SET;
            milp::BranchAndBoundSolver bb_solver(pimpl_->options.tolerances, bb_opts);
            auto result = bb_solver.solve(target);
            if (presolved_solution &&
                result.status != model::ProblemStatus::TIME_LIMIT &&
                    result.status != model::ProblemStatus::INTERRUPTED &&
                    result.status != model::ProblemStatus::NUMERICAL_ERROR) {
                auto full = presolver->postsolve(problem, postsolve(result.primal, result.dual, {}), *presolve_result);
                solution.primal = std::move(full.primal);
                solution.objective_value = recompute_objective(solution.primal);
            } else {
                solution.primal = std::move(result.primal);
                solution.dual = std::move(result.dual);
                solution.objective_value = result.objective_value;
            }
            solution.status = result.status;
            solution.best_bound = result.best_bound;
            solution.gap = result.gap;
            solution.bb_nodes = result.nodes_explored;
        } else if (target.is_qp()) {
            if (pimpl_->options.engine == EngineType::QP_INTERIOR_POINT) {
                qp::InteriorPointQPOptions qp_opts;
                qp_opts.time_limit_seconds = remaining_time;
                qp_opts.interrupt_callback = [this]() { return pimpl_->interrupted_; };
                qp::InteriorPointQPSolver qp_solver(pimpl_->options.tolerances, qp_opts);
                auto result = qp_solver.solve(target);
                solution.status = result.status;
                solution.objective_value = result.objective_value;
                solution.primal = std::move(result.primal);
                solution.dual = std::move(result.dual);
            } else {
                qp::ActiveSetOptions qp_opts;
                qp_opts.time_limit_seconds = remaining_time;
                qp_opts.interrupt_callback = [this]() { return pimpl_->interrupted_; };
                qp::ActiveSetQPSolver qp_solver(pimpl_->options.tolerances, qp_opts);
                auto result = qp_solver.solve(target);
                solution.status = result.status;
                solution.objective_value = result.objective_value;
                solution.primal = std::move(result.primal);
                solution.dual = std::move(result.dual);
            }
        } else {
            solution.status = model::ProblemStatus::NUMERICAL_ERROR;
        }
    };

    bool use_presolve = pimpl_->options.presolve != PresolveLevel::OFF &&
                        (problem.is_lp() || problem.is_milp());
    if (use_presolve) {
presolve::PresolveOptions popts;
        popts.remove_singletons = pimpl_->options.presolve == PresolveLevel::AGGRESSIVE;
        popts.coefficient_strengthening = pimpl_->options.presolve == PresolveLevel::AGGRESSIVE;
        popts.dual_reductions = pimpl_->options.presolve == PresolveLevel::AGGRESSIVE;
        presolver = std::make_unique<presolve::Presolver>(popts);
        presolve_result = presolver->presolve(problem);

        if (remaining_time > 0.0) {
            remaining_time -= elapsed_ms();
            if (remaining_time < 0.0) remaining_time = 0.0;
        }

        const auto& reduced = presolve_result->reduced_problem;
        if (reduced.variables.empty()) {
            solution.status = model::ProblemStatus::OPTIMAL;
            solution.primal.resize(problem.variables.size(), 0.0);
            for (const auto& fv : presolve_result->fixed_variables) {
                solution.primal[fv.first] = fv.second.second;
            }
            solution.objective_value = recompute_objective(solution.primal);
            auto end_time = std::chrono::high_resolution_clock::now();
            solution.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - global_start).count();
            return solution;
        }
        solve_problem = &reduced;
    }

    numerical::ScalingMethod scale_method = numerical::ScalingMethod::NONE;
    if (pimpl_->options.scaling == ScalingMethod::GEOMETRIC) {
        scale_method = numerical::ScalingMethod::GEOMETRIC;
    } else if (pimpl_->options.scaling == ScalingMethod::CURTIS_REID) {
        scale_method = numerical::ScalingMethod::CURTIS_REID;
    }

    std::optional<numerical::ScalingResult> scaling_result;
    std::optional<model::Problem> scaled_problem_holder;
    if (scale_method != numerical::ScalingMethod::NONE && solve_problem->constraint_matrix.nnz() > 0) {
        numerical::MatrixScaler scaler(scale_method);
        scaling_result = scaler.compute_scales(solve_problem->constraint_matrix);
        scaled_problem_holder = *solve_problem;
        std::vector<double> rhs(scaled_problem_holder->constraints.size());
        for (std::size_t i = 0; i < rhs.size(); ++i) rhs[i] = scaled_problem_holder->constraints[i].rhs;
        std::vector<double> obj(scaled_problem_holder->variables.size());
        for (std::size_t j = 0; j < obj.size(); ++j) obj[j] = scaled_problem_holder->variables[j].objective_coeff;
        std::vector<double> lb(scaled_problem_holder->variables.size());
        for (std::size_t j = 0; j < lb.size(); ++j) lb[j] = scaled_problem_holder->variables[j].lower_bound;
        std::vector<double> ub(scaled_problem_holder->variables.size());
        for (std::size_t j = 0; j < ub.size(); ++j) ub[j] = scaled_problem_holder->variables[j].upper_bound;

        scaler.apply_scales(scaled_problem_holder->constraint_matrix, rhs, obj, lb, ub, *scaling_result);
        for (std::size_t i = 0; i < rhs.size(); ++i) scaled_problem_holder->constraints[i].rhs = rhs[i];
        for (std::size_t j = 0; j < obj.size(); ++j) {
            scaled_problem_holder->variables[j].objective_coeff = obj[j];
            scaled_problem_holder->variables[j].lower_bound = lb[j];
            scaled_problem_holder->variables[j].upper_bound = ub[j];
        }
        solve_problem = &(*scaled_problem_holder);
    }

    if (compute_backend && compute_backend->type() == execution::ComputeBackendType::AUTO) {
        compute_backend->initialize();
    }

    solve_dispatch(*solve_problem);

    if (scaling_result && !solution.primal.empty()) {
        numerical::MatrixScaler scaler(scale_method);
        scaler.unscale_solution(*scaling_result, solution.primal, solution.dual);
        solution.objective_value = recompute_objective(solution.primal);
    }
    if (std::getenv("HYPERNOVA_API_DBG")) {
        std::cerr << "API dispatch1 status=" << static_cast<int>(solution.status)
                  << " obj=" << solution.objective_value
                  << " elapsed=" << elapsed_ms() << "\n";
    }

    if (remaining_time > 0.0) {
        remaining_time -= elapsed_ms();
        if (remaining_time < 0.0) remaining_time = 0.0;
    }
    if (presolver && presolve_result && remaining_time > 0.0 &&
        (solution.status == model::ProblemStatus::INFEASIBLE ||
         solution.status == model::ProblemStatus::UNBOUNDED ||
         solution.status == model::ProblemStatus::NUMERICAL_ERROR)) {
        presolver.reset();
        presolve_result.reset();
        solve_dispatch(problem);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    solution.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - global_start).count();

    validation::SolutionVerifier verifier(pimpl_->options.tolerances);
    model::Solution model_sol;
    auto refresh_model_sol = [&]() {
        model_sol.primal = solution.primal;
        model_sol.dual = solution.dual;
        model_sol.reduced_costs = solution.reduced_costs;
        model_sol.status = solution.status;
    };
    refresh_model_sol();
    if (std::getenv("HYPERNOVA_API_DBG")) {
        auto vcheck = verifier.verify_detailed(problem, model_sol);
        std::cerr << "API first verify " << (vcheck.feasible && vcheck.optimal ? "PASSED" : "FAILED")
                  << " status=" << static_cast<int>(solution.status)
                  << " remaining=" << remaining_time << "\n";
        std::cerr << "API verify message: " << vcheck.message
                  << " primal=" << vcheck.primal_infeasibility
                  << " dual=" << vcheck.dual_infeasibility
                  << " comp=" << vcheck.complementarity << "\n";
        if (!vcheck.violated_constraints.empty()) {
            std::cerr << "API violated constraints:";
            for (std::size_t i = 0; i < vcheck.violated_constraints.size() && i < 4; ++i) {
                std::cerr << " " << vcheck.violated_constraints[i];
            }
            std::cerr << "\n";
        }
    }
    if (remaining_time > 0.0) {
        remaining_time -= elapsed_ms();
        if (remaining_time < 0.0) remaining_time = 0.0;
    }
    if (!verifier.verify(problem, model_sol) &&
        solution.status == model::ProblemStatus::OPTIMAL &&
        presolver && presolve_result &&
        remaining_time > 0.0) {
        presolver.reset();
        presolve_result.reset();
        solve_dispatch(problem);
        refresh_model_sol();
    }
    if (std::getenv("HYPERNOVA_API_DBG")) {
        std::cerr << "API final status=" << static_cast<int>(solution.status)
                  << " obj=" << solution.objective_value << "\n";
    }
    if (!verifier.verify(problem, model_sol)) {
        if (solution.status == model::ProblemStatus::OPTIMAL) {
            solution.status = model::ProblemStatus::NUMERICAL_ERROR;
        }
    }

    if (!pimpl_->options.use_gpu || pimpl_->options.compute_target == ComputeTarget::CPU_ONLY) {
        solution.backend_used = "cpu (GPU disabled)";
    } else if (compute_backend && compute_backend->was_gpu_kernel_executed()) {
        solution.backend_used = "cuda (PTX SpMV kernel executed)";
    } else if (pimpl_->options.compute_target == ComputeTarget::CPU_GPU_FORCE) {
        auto avail = execution::ComputeBackendFactory::available_backends();
        bool has_cuda = false;
        for (auto b : avail) {
            if (b == execution::ComputeBackendType::CUDA) has_cuda = true;
        }
        solution.backend_used = has_cuda ? "cuda (forced)" : "cpu (cuda forced but unavailable - fallback)";
    } else {
        auto avail = execution::ComputeBackendFactory::available_backends();
        bool has_cuda = false;
        for (auto b : avail) {
            if (b == execution::ComputeBackendType::CUDA) has_cuda = true;
        }
        solution.backend_used = has_cuda ? "cpu (cost model selected CPU for matrix size)" : "cpu (cuda unavailable)";
    }

    return solution;
}

Solution Solver::warm_solve(const model::Problem& problem, const Basis& start_basis) {
    auto start_time = std::chrono::high_resolution_clock::now();
    pimpl_->interrupted_ = false;

    Solution solution;

    std::vector<int> var_status(problem.variables.size(), 0);
    if (start_basis.var_status.size() == problem.variables.size()) {
        var_status = start_basis.var_status;
    }
    std::vector<int> con_status;
    if (start_basis.con_status.size() == problem.constraints.size()) {
        con_status = start_basis.con_status;
    }

    if (problem.is_lp()) {
        lp::SimplexOptions simplex_opts = pimpl_->make_simplex_options(pimpl_->options.time_limit_seconds);
        lp::SimplexSolver simplex_solver(pimpl_->options.tolerances, simplex_opts);
        auto result = simplex_solver.solve_with_basis(problem, var_status, con_status);
        solution.status = result.status;
        solution.objective_value = result.objective_value;
        solution.primal = std::move(result.primal);
        solution.dual = std::move(result.dual);
        solution.reduced_costs = std::move(result.reduced_costs);
        solution.basis_status = std::move(result.basis_status);
        solution.simplex_iterations = result.iterations;
    } else {
        solution = solve(problem);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    solution.solve_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return solution;
}

void Solver::set_options(const SolverOptions& options) {
    pimpl_->options = options;
}

validation::IISResult Solver::compute_iis(const model::Problem& problem) {
    lp::SimplexOptions simplex_opts = pimpl_->make_simplex_options(pimpl_->options.time_limit_seconds);
    validation::IISComputer computer(pimpl_->options.tolerances, simplex_opts);
    return computer.compute(problem);
}

validation::CertificateResult Solver::diagnose(const model::Problem& problem) {
    lp::SimplexOptions simplex_opts = pimpl_->make_simplex_options(pimpl_->options.time_limit_seconds);
    validation::CertificateAnalyzer analyzer(pimpl_->options.tolerances, simplex_opts);
    return analyzer.analyze(problem);
}

validation::NumericalDiagnostics Solver::diagnose_numerical(const model::Problem& problem) {
    lp::SimplexOptions simplex_opts = pimpl_->make_simplex_options(pimpl_->options.time_limit_seconds);
    validation::DiagnosticsAnalyzer analyzer(pimpl_->options.tolerances, simplex_opts);
    return analyzer.analyze(problem);
}

const SolverOptions& Solver::options() const {
    return pimpl_->options;
}

void Solver::interrupt() {
    pimpl_->interrupted_ = true;
}

bool Solver::interrupted() const {
    return pimpl_->interrupted_;
}

Problem Problem::from_mps(const std::string& path) {
    model::MPSParser parser;
    auto result = parser.parse_file(path);
    if (!result.success) {
        throw std::runtime_error("Failed to parse MPS file: " + path);
    }
    Problem p;
    static_cast<model::Problem&>(p) = std::move(result.problem);
    return p;
}

Problem Problem::from_lp(const std::string& path) {
    model::LPParser parser;
    auto result = parser.parse_file(path);
    if (!result.success) {
        for (const auto& e : result.errors) {
            std::cerr << "[LP parse error] " << e << "\n";
        }
        throw std::runtime_error("Failed to parse LP file: " + path);
    }
    Problem p;
    static_cast<model::Problem&>(p) = std::move(result.problem);
    return p;
}

void Problem::write_mps(const std::string& path) const {
    model::write_mps(*this, path);
}

void Problem::write_lp(const std::string& path) const {
    model::write_lp(*this, path);
}

std::size_t Problem::add_variable(double lb, double ub, model::VarType type, const std::string& name) {
    model::Variable var;
    var.index = variables.size();
    var.lower_bound = lb;
    var.upper_bound = ub;
    var.type = type;
    var.name = name.empty() ? "x" + std::to_string(var.index) : name;
    variables.push_back(var);
    return var.index;
}

std::size_t Problem::add_constraint(const std::vector<std::pair<std::size_t, double>>& coeffs,
                                     model::ConstraintSense sense, double rhs, const std::string& name) {
    model::Constraint con;
    con.index = constraints.size();
    con.sense = sense;
    con.rhs = rhs;
    con.name = name.empty() ? "c" + std::to_string(con.index) : name;
    constraints.push_back(con);

    if (constraint_matrix.row_ptr().empty()) {
        constraint_matrix.mutable_row_ptr().push_back(0);
    }

    for (const auto& [var_idx, coeff] : coeffs) {
        constraint_matrix.mutable_values().push_back(coeff);
        constraint_matrix.mutable_col_indices().push_back(var_idx);
        if (var_idx + 1 > constraint_matrix.mutable_cols()) {
            constraint_matrix.mutable_cols() = var_idx + 1;
        }
    }
    constraint_matrix.mutable_row_ptr().push_back(constraint_matrix.values().size());
    constraint_matrix.mutable_rows() = constraints.size();

    return con.index;
}

void Problem::set_objective(const std::vector<std::pair<std::size_t, double>>& coeffs,
                             model::ObjectiveSense sense) {
    obj_sense = sense;
    for (const auto& [var_idx, coeff] : coeffs) {
        if (var_idx < variables.size()) {
            variables[var_idx].objective_coeff = coeff;
        }
    }
}

void Problem::add_quadratic_term(std::size_t row, std::size_t col, double coeff) {
    quadratic_terms.push_back({row, col, coeff});
}

} // namespace hypernova


