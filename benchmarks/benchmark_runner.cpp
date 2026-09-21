#include "benchmark_runner.hpp"
#include "netlib_reference.hpp"
#include <validation/solution_verifier.hpp>
#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

namespace hypernova::benchmark {

namespace {

namespace fs = std::filesystem;

struct ReferenceMap {
    std::map<std::string, ReferenceEntry> by_name;
};

ReferenceMap build_reference(const BenchmarkConfig& config) {
    ReferenceMap map;
    std::vector<ReferenceEntry> entries;
    if (!config.reference_csv.empty()) {
        entries = load_reference_csv(config.reference_csv);
    } else if (config.use_builtin_netlib_reference) {
        entries = netlib_reference_table();
    }
    for (const auto& entry : entries) {
        std::string key = entry.name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        map.by_name[key] = entry;
    }
    return map;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string stem_of(const std::string& path) {
    fs::path p(path);
    return p.stem().string();
}

std::vector<std::string> collect_files(const BenchmarkConfig& config) {
    if (!config.files.empty()) {
        std::vector<std::string> files = config.files;
        std::sort(files.begin(), files.end());
        return files;
    }
    if (config.directory.empty()) {
        return {};
    }
    std::error_code ec;
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(config.directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = to_lower(entry.path().extension().string());
        if (ext == ".mps" || ext == ".lp") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool is_mps(const std::string& path) {
    std::string ext = to_lower(fs::path(path).extension().string());
    return ext == ".mps";
}

ReferenceEntry match_reference(const ReferenceMap& map, const std::string& primary_name,
                               const std::string& fallback_name) {
    for (const auto& candidate : {primary_name, fallback_name}) {
        std::string key = to_lower(candidate);
        auto it = map.by_name.find(key);
        if (it != map.by_name.end()) {
            return it->second;
        }
    }
    return ReferenceEntry{};
}

std::string problem_type_of(const model::Problem& problem) {
    if (problem.is_lp()) return "LP";
    if (problem.is_milp()) return "MILP";
    if (problem.is_miqp()) return "MIQP";
    if (problem.is_qp()) return "QP";
    return "unsupported";
}

bool status_matches_reference(model::ProblemStatus actual, const std::string& expected) {
    if (expected == "inf") return actual == model::ProblemStatus::INFEASIBLE;
    if (expected == "unb") return actual == model::ProblemStatus::UNBOUNDED;
    return actual == model::ProblemStatus::OPTIMAL;
}

double relative_objective_error(double actual, double reference) {
    double scale = std::max(1.0, std::fabs(reference));
    return std::fabs(actual - reference) / scale;
}

} // namespace

namespace {

// Solves a parsed problem with the given thread count and fills every field of
// an InstanceResult used by the thread sweep (time, nodes, nodes/sec, status,
// objective). Independent validation is re-run for OPTIMAL results.
InstanceResult sweep_solve(const hypernova::Problem& problem, int threads,
                           const hypernova::SolverOptions& base_options,
                           const std::string& stem) {
    InstanceResult result;
    result.name = stem;
    result.file = stem;
    result.problem_type = problem_type_of(problem);
    result.num_variables = problem.variables.size();
    result.num_constraints = problem.constraints.size();
    result.num_nonzeros = problem.constraint_matrix.nnz();
    result.num_integer_vars = problem.num_integer_vars();
    result.thread_count = threads;

    hypernova::SolverOptions options = base_options;
    options.thread_count = threads;

    hypernova::Solution solution;
    try {
        hypernova::Solver solver(options);
        solution = solver.solve(problem);
    } catch (const std::exception& e) {
        result.status = model::ProblemStatus::NUMERICAL_ERROR;
        result.message = std::string("solve raised: ") + e.what();
        result.pass = false;
        return result;
    }

    result.status = solution.status;
    result.objective = solution.objective_value;
    result.best_bound = solution.best_bound;
    result.gap = solution.gap;
    result.solve_time_seconds = solution.solve_time_ms / 1000.0;
    if (problem.is_milp() || problem.is_miqp()) {
        result.engine_used = problem.is_miqp() ? "branch-and-bound-qp" : "branch-and-bound";
        result.iterations = solution.bb_nodes;
        if (result.solve_time_seconds > 0.0) {
            result.nodes_per_second = static_cast<double>(result.iterations) / result.solve_time_seconds;
        }
    }

    if (result.status == model::ProblemStatus::OPTIMAL && !solution.primal.empty()) {
        validation::SolutionVerifier verifier(options.tolerances);
        model::Solution model_sol;
        model_sol.primal = solution.primal;
        model_sol.dual = solution.dual;
        model_sol.reduced_costs = solution.reduced_costs;
        model_sol.status = solution.status;
        model_sol.objective_value = solution.objective_value;
        auto detail = verifier.verify_detailed(problem, model_sol);
        result.verified = detail.optimal;
        result.pass = detail.optimal;
        if (!detail.optimal) result.message = "verifier: " + detail.message;
    } else {
        result.verified = false;
        result.pass = false;
        if (result.message.empty()) {
            result.message = "status " + SuiteReport::status_name(result.status);
        }
    }
    return result;
}

} // namespace

SuiteReport run_benchmark(const BenchmarkConfig& config) {
    SuiteReport report;
    report.suite_name = config.suite_name;
    report.timestamp = iso_timestamp_now();
    report.reference_tolerance = std::to_string(config.reference_rel_tol);
    
    report.threads = config.thread_count;
    report.gap_tol = config.mip_gap_tolerance;
    report.time_limit = config.time_limit_seconds;
    report.cpu_info = "Detected CPUs: " + std::to_string(std::thread::hardware_concurrency());
    report.gpu_info = "N/A (benchmarks default to CPU deterministic path)";
    // You could map config.engine/presolve if those fields were available on config, but this is a good baseline.

    const ReferenceMap reference = build_reference(config);
    const std::vector<std::string> files = collect_files(config);

    for (const auto& file : files) {
        InstanceResult result;
        result.file = file;
        result.name = stem_of(file);

        if (is_mps(file)) {
            result.problem_type = "MPS";
        } else {
            result.problem_type = "LP";
        }

        // --- Parse & load ---------------------------------------------
        hypernova::Problem problem;
        try {
            if (is_mps(file)) {
                problem = hypernova::Problem::from_mps(file);
            } else {
                problem = hypernova::Problem::from_lp(file);
            }
            result.num_variables = problem.variables.size();
            result.num_constraints = problem.constraints.size();
            result.num_nonzeros = problem.constraint_matrix.nnz();
            result.num_integer_vars = problem.num_integer_vars();
            result.problem_type = problem_type_of(problem);
        } catch (const std::exception& e) {
            result.status = model::ProblemStatus::NUMERICAL_ERROR;
            result.problem_type = "parse-failed";
            result.message = std::string("parse failed: ") + e.what();
            report.instances.push_back(std::move(result));
            continue;
        }

        // --- Solve ----------------------------------------------------
        hypernova::SolverOptions options;
        options.time_limit_seconds = config.time_limit_seconds;
        options.mip_gap_tolerance = config.mip_gap_tolerance;
        options.thread_count = config.thread_count;
        options.engine = config.engine;
        options.use_gpu = false; // deterministic CPU baseline for benchmarks

        hypernova::Solution solution;
        try {
            hypernova::Solver solver(options);
            solution = solver.solve(problem);
        } catch (const std::exception& e) {
            result.status = model::ProblemStatus::NUMERICAL_ERROR;
            result.message = std::string("solve raised: ") + e.what();
            report.instances.push_back(std::move(result));
            continue;
        }

        result.status = solution.status;
        result.objective = solution.objective_value;
        result.best_bound = solution.best_bound;
        result.gap = solution.gap;
        result.solve_time_seconds = solution.solve_time_ms / 1000.0;
        result.thread_count = config.thread_count;
        if (problem.is_milp()) {
            result.engine_used = "branch-and-bound";
            result.iterations = solution.bb_nodes;
            result.nodes_per_second =
                result.solve_time_seconds > 0.0
                    ? static_cast<double>(solution.bb_nodes) / result.solve_time_seconds
                    : 0.0;
        } else if (problem.is_miqp()) {
            result.engine_used = "branch-and-bound-qp";
            result.iterations = solution.bb_nodes;
            result.nodes_per_second =
                result.solve_time_seconds > 0.0
                    ? static_cast<double>(solution.bb_nodes) / result.solve_time_seconds
                    : 0.0;
        } else if (solution.simplex_iterations > 0) {
            result.engine_used = "simplex";
            result.iterations = solution.simplex_iterations;
        } else if (solution.ipm_iterations > 0) {
            result.engine_used = "interior-point";
            result.iterations = solution.ipm_iterations;
        } else if (problem.is_qp()) {
            result.engine_used = "qp";
            result.iterations = 0;
        } else {
            result.engine_used = "auto";
            result.iterations = 0;
        }

        // --- Independent validation layer check ----------------------
        if (solution.primal.empty() &&
            result.status != model::ProblemStatus::INFEASIBLE &&
            result.status != model::ProblemStatus::UNBOUNDED) {
            result.verified = false;
            if (result.message.empty()) {
                result.message = "no primal returned";
            }
        } else if (result.status == model::ProblemStatus::OPTIMAL) {
            validation::SolutionVerifier verifier(options.tolerances);
            model::Solution model_sol;
            model_sol.primal = solution.primal;
            model_sol.dual = solution.dual;
            model_sol.reduced_costs = solution.reduced_costs;
            model_sol.status = solution.status;
            model_sol.objective_value = solution.objective_value;
            auto detail = verifier.verify_detailed(problem, model_sol);
            result.verified = detail.optimal;
            if (!result.verified && result.message.empty()) {
                result.message = "verifier: " + detail.message;
            }
        } else {
            result.verified = false;
            if (result.message.empty()) {
                result.message = "status not optimal; validation skipped";
            }
        }

        // --- Grade against reference ---------------------------------
        ReferenceEntry ref = match_reference(reference, problem.name, result.name);
        if (!ref.name.empty()) {
            result.has_reference = true;
            result.reference_status = ref.status;
            result.reference_objective = ref.objective;
            if (ref.status == "inf" || ref.status == "unb") {
                result.pass = status_matches_reference(result.status, ref.status);
                if (!result.pass) {
                    result.message = "expected " + ref.status + " but got " +
                                     SuiteReport::status_name(result.status);
                }
            } else {
                bool objective_ok = result.status == model::ProblemStatus::OPTIMAL &&
                                    std::isfinite(result.objective) &&
                                    relative_objective_error(result.objective, ref.objective) <=
                                        config.reference_rel_tol;
                bool classified_ok = status_matches_reference(result.status, ref.status);
                result.pass = classified_ok && objective_ok && result.verified;
                if (!result.pass) {
                    result.message = (result.message.empty() ? "" : result.message + "; ") +
                                     "reference objective " + std::to_string(ref.objective) +
                                     (objective_ok ? "" : " not matched");
                }
            }
        } else {
            // No reference: pass means an honest, verifiable classification.
            if (result.status == model::ProblemStatus::INFEASIBLE ||
                result.status == model::ProblemStatus::UNBOUNDED) {
                result.pass = true;
            } else {
                result.pass = result.status == model::ProblemStatus::OPTIMAL && result.verified;
            }
            if (!result.pass && result.message.empty()) {
                result.message = "status " + SuiteReport::status_name(result.status);
            }
        }

        report.instances.push_back(std::move(result));
    }

    // --- Optional B&B thread sweep -------------------------------------
    // Re-solve every MILP/MIQP instance at each thread count in
    // config.thread_sweep and record wall-clock, node count and nodes/sec so
    // the parallel scaling (1/2/4/8...) can be documented honestly. Only
    // B&B-type instances participate; LP/QP runs are unaffected.
    if (!config.thread_sweep.empty()) {
        hypernova::SolverOptions sweep_base;
        sweep_base.time_limit_seconds = config.time_limit_seconds;
        sweep_base.mip_gap_tolerance = config.mip_gap_tolerance;
        sweep_base.thread_count = 1; // overridden per row by sweep_solve
        sweep_base.engine = config.engine;
        sweep_base.use_gpu = false;

        for (const auto& file : files) {
            hypernova::Problem sweep_problem;
            try {
                if (is_mps(file)) sweep_problem = hypernova::Problem::from_mps(file);
                else sweep_problem = hypernova::Problem::from_lp(file);
            } catch (const std::exception&) {
                continue;
            }
            if (!sweep_problem.is_milp() && !sweep_problem.is_miqp()) continue;

            for (int threads : config.thread_sweep) {
                InstanceResult sweep_result =
                    sweep_solve(sweep_problem, threads, sweep_base, stem_of(file));
                sweep_result.file = file;
                report.thread_sweep.push_back(std::move(sweep_result));
            }
        }
    }

    report.summarize();
    return report;
}

} // namespace hypernova::benchmark