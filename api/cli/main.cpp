#include <hypernova/api.hpp>
#include <execution/gpu_backend.hpp>
#include "benchmark_runner.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include "interactive/InteractiveSession.hpp"

using namespace hypernova;

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " <command> [options]\n\n";
    std::cout << " Commands:\n";
    std::cout << "  solve <model.mps|model.lp>    Solve an optimization problem\n";
    std::cout << "  iis <model.mps|model.lp>      Find an irreducible infeasible subsystem\n";
    std::cout << "  diagnose <model.mps|model.lp> Verify certificates for infeasible/unbounded models\n";
    std::cout << "  quality <model.mps|model.lp>   Numerical diagnostics report (use --json for JSON)\n";
    std::cout << "  capabilities                  Show solver capabilities and compute backends\n";
    std::cout << "  interactive                   Build problem interactively\n";
    std::cout << "  convert <input> --to <mps|lp> Convert between file formats\n";
    std::cout << "  inspect <model.mps|model.lp>  Inspect problem structure\n";
    std::cout << "  benchmark --suite <name>      Run benchmark suite\n";
    std::cout << "  help                          Show this help\n\n";
    std::cout << "Solve options:\n";
    std::cout << "  --time-limit <seconds>        Time limit (default: 3600)\n";
    std::cout << "  --gap <tolerance>             MIP gap tolerance (default: 1e-4)\n";
    std::cout << "  --threads <count>             Number of threads (default: auto)\n";
    std::cout << "  --gpu <auto|on|off>           GPU usage (default: auto)\n";
    std::cout << "  --engine <auto|simplex|ipm|bnb> Solver engine (default: auto)\n";
    std::cout << "  --pricing <dantzig|devex|steepest-edge> Entering pricing (requires --nobland)\n";
    std::cout << "  --ratio-test <standard|harris> Ratio test rule\n";
    std::cout << "  --nobland                     Fall back from DEVEX pricing to Bland\n";
    std::cout << "                                anti-cycling entering (default: DEVEX)\n";
    std::cout << "  --node-selection <best-first|depth-first|best-estimate|hybrid>\n";
    std::cout << "                                B&B node-ordering strategy (default: hybrid)\n";
    std::cout << "  --branching <most-fractional|pseudocost|strong|reliability>\n";
    std::cout << "                                B&B branching strategy (default: reliability)\n";
    std::cout << "  --out <file>                  Write solution file (JSON .sol)\n";
    std::cout << "  --report <file>               Write solve report (JSON)\n\n";
    std::cout << "Interactive options:\n";
    std::cout << "  --resume <session.json>       Resume from saved session\n";
    std::cout << "  --save-session <file>         Save session after completion\n";
    std::cout << "  --output-format <mps|lp|json> Export problem after build\n";
    std::cout << "  --output-file <file>          Output file for export\n";
    std::cout << "  --engine <engine>             Solver engine\n";
    std::cout << "  --time-limit <seconds>        Time limit (default: 3600)\n";
    std::cout << "  --gap <tolerance>             MIP gap tolerance (default: 1e-4)\n";
    std::cout << "  --threads <count>             Thread count (default: auto)\n";
    std::cout << "  --advanced                    Enable QP/SOS prompts\n";
}

namespace {

bool ends_with_ci(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

Problem load_problem(const std::string& path) {
    if (ends_with_ci(path, ".mps")) return Problem::from_mps(path);
    return Problem::from_lp(path);
}

std::string status_label(model::ProblemStatus s) {
    switch (s) {
        case model::ProblemStatus::OPTIMAL:        return "OPTIMAL";
        case model::ProblemStatus::INFEASIBLE:     return "INFEASIBLE";
        case model::ProblemStatus::UNBOUNDED:      return "UNBOUNDED";
        case model::ProblemStatus::SUBOPTIMAL:     return "SUBOPTIMAL";
        case model::ProblemStatus::TIME_LIMIT:     return "TIME_LIMIT";
        case model::ProblemStatus::ITER_LIMIT:     return "ITER_LIMIT";
        case model::ProblemStatus::NUMERICAL_ERROR:return "NUMERICAL_ERROR";
        case model::ProblemStatus::INTERRUPTED:    return "INTERRUPTED";
        default:                                   return "UNKNOWN";
    }
}

model::Solution to_model_solution(const Solution& s) {
    model::Solution m;
    m.status = s.status;
    m.objective_value = s.objective_value;
    m.best_bound = s.best_bound;
    m.gap = s.gap;
    m.primal = s.primal;
    m.dual = s.dual;
    m.reduced_costs = s.reduced_costs;
    m.basis_status = s.basis_status;
    m.simplex_iterations = s.simplex_iterations;
    m.ipm_iterations = s.ipm_iterations;
    m.bb_nodes = s.bb_nodes;
    m.solve_time_ms = s.solve_time_ms;
    return m;
}

void write_solution_file(const std::string& path, const Problem& problem, const Solution& solution) {
    nlohmann::json j;
    j["problem"] = problem.name;
    j["status"] = status_label(solution.status);
    j["objective_value"] = solution.objective_value;
    j["best_bound"] = solution.best_bound;
    j["gap"] = solution.gap;
    j["solve_time_ms"] = solution.solve_time_ms;
    auto& vars = j["variables"] = nlohmann::json::array();
    for (std::size_t i = 0; i < solution.primal.size() && i < problem.variables.size(); ++i) {
        vars.push_back({
            {"name", problem.variables[i].name},
            {"index", i},
            {"value", solution.primal[i]}
        });
    }
    if (!solution.dual.empty()) {
        auto& cons = j["constraints"] = nlohmann::json::array();
        for (std::size_t i = 0; i < solution.dual.size() && i < problem.constraints.size(); ++i) {
            cons.push_back({
                {"name", problem.constraints[i].name},
                {"index", i},
                {"dual", solution.dual[i]}
            });
        }
    }
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("cannot open output file: " + path);
    }
    out << j.dump(2) << "\n";
}

nlohmann::json solve_report_json(const Problem& problem, const Solution& solution,
                                 double wall_ms,
                                 const validation::VerificationResult& verification,
                                 const SolverOptions& options) {
    nlohmann::json j;
    j["problem_name"] = problem.name;
    j["solver_version"] = "0.1.0";
    j["status"] = status_label(solution.status);
    j["objective_value"] = solution.objective_value;
    j["best_bound"] = solution.best_bound;
    j["gap"] = solution.gap;
    j["num_variables"] = problem.variables.size();
    j["num_constraints"] = problem.constraints.size();
    j["num_integer_vars"] = problem.num_integer_vars();
    j["num_quadratic_terms"] = problem.quadratic_terms.size();
    j["num_nonzeros"] = problem.constraint_matrix.nnz();
    j["simplex_iterations"] = solution.simplex_iterations;
    j["ipm_iterations"] = solution.ipm_iterations;
    j["bb_nodes"] = solution.bb_nodes;
    j["solve_time_ms"] = solution.solve_time_ms;
    j["wall_time_ms"] = wall_ms;
    j["backend_used"] = solution.backend_used;
    j["solver_options"] = {
        {"use_gpu", options.use_gpu},
        {"compute_target", static_cast<int>(options.compute_target)},
        {"presolve", static_cast<int>(options.presolve)},
        {"scaling", static_cast<int>(options.scaling)},
        {"threads", options.thread_count},
        {"time_limit_seconds", options.time_limit_seconds},
        {"mip_gap_tolerance", options.mip_gap_tolerance},
        {"node_limit", options.node_limit},
        {"solution_limit", options.solution_limit},
        {"heuristic_rins", options.heuristic_rins},
        {"heuristic_feasibility_pump", options.heuristic_feasibility_pump}
    };
    j["verification"] = {
        {"feasible", verification.feasible},
        {"optimal", verification.optimal},
        {"primal_infeasibility", verification.primal_infeasibility},
        {"dual_infeasibility", verification.dual_infeasibility},
        {"complementarity", verification.complementarity},
        {"integrality_violation", verification.integrality_violation},
        {"message", verification.message}
    };
    return j;
}

std::string compute_backend_name(execution::ComputeBackendType type) {
    switch (type) {
        case execution::ComputeBackendType::CUDA: return "cuda";
        case execution::ComputeBackendType::HIP:  return "hip";
        case execution::ComputeBackendType::SYCL: return "sycl";
        default:                                  return "cpu";
    }
}

} // namespace

int cmd_solve(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "solve: missing model file\n";
        return 1;
    }

    std::string model_file = argv[2];
    SolverOptions options;
    std::string out_file;
    std::string report_file;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--time-limit" && i + 1 < argc) {
            options.time_limit_seconds = std::stod(argv[++i]);
        } else if (arg == "--gap" && i + 1 < argc) {
            options.mip_gap_tolerance = std::stod(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            options.thread_count = std::stoi(argv[++i]);
        } else if (arg == "--gpu" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "off") options.use_gpu = false;
            else if (val == "on") options.use_gpu = true;
        } else if (arg == "--compute-target" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "cpu") options.compute_target = ComputeTarget::CPU_ONLY;
            else if (val == "auto") options.compute_target = ComputeTarget::CPU_GPU_AUTO;
            else if (val == "force") options.compute_target = ComputeTarget::CPU_GPU_FORCE;
        } else if (arg == "--presolve" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "off") options.presolve = PresolveLevel::OFF;
            else if (val == "conservative") options.presolve = PresolveLevel::CONSERVATIVE;
            else if (val == "aggressive") options.presolve = PresolveLevel::AGGRESSIVE;
        } else if (arg == "--scaling" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "none") options.scaling = ScalingMethod::NONE;
            else if (val == "geometric") options.scaling = ScalingMethod::GEOMETRIC;
            else if (val == "curtis-reid") options.scaling = ScalingMethod::CURTIS_REID;
        } else if (arg == "--rins") {
            options.heuristic_rins = true;
        } else if (arg == "--feasibility-pump") {
            options.heuristic_feasibility_pump = true;
        } else if (arg == "--rins-freq" && i + 1 < argc) {
            options.rins_frequency = std::stoi(argv[++i]);
        } else if (arg == "--primal-tol" && i + 1 < argc) {
            options.tolerances.feasibility_tol(std::stod(argv[++i]));
        } else if (arg == "--dual-tol" && i + 1 < argc) {
            options.tolerances.optimality_tol(std::stod(argv[++i]));
        } else if (arg == "--node-limit" && i + 1 < argc) {
            options.node_limit = std::stoull(argv[++i]);
        } else if (arg == "--solution-limit" && i + 1 < argc) {
            options.solution_limit = std::stoull(argv[++i]);
        } else if (arg == "--engine" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "simplex") options.engine = EngineType::PRIMAL_SIMPLEX;
            else if (val == "dual-simplex") options.engine = EngineType::DUAL_SIMPLEX;
            else if (val == "ipm") options.engine = EngineType::INTERIOR_POINT;
            else if (val == "bnb") options.engine = EngineType::BRANCH_AND_BOUND;
        } else if (arg == "--pricing" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "dantzig") options.simplex_pricing = lp::PricingStrategy::DANTZIG;
            else if (val == "devex") options.simplex_pricing = lp::PricingStrategy::DEVEX;
            else if (val == "steepest-edge") options.simplex_pricing = lp::PricingStrategy::STEEPEST_EDGE;
        } else if (arg == "--ratio-test" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "harris") options.simplex_ratio_test = lp::RatioTest::HARRIS_TWO_PASS;
            else if (val == "standard") options.simplex_ratio_test = lp::RatioTest::STANDARD;
        } else if (arg == "--nobland") {
            options.simplex_bland_rule = false;
        } else if (arg == "--node-selection" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "best-first") options.node_selection = NodeSelectionStrategy::BEST_FIRST;
            else if (val == "depth-first") options.node_selection = NodeSelectionStrategy::DEPTH_FIRST;
            else if (val == "best-estimate") options.node_selection = NodeSelectionStrategy::BEST_ESTIMATE;
            else if (val == "hybrid") options.node_selection = NodeSelectionStrategy::HYBRID;
        } else if (arg == "--branching" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "most-fractional") options.branching = BranchingStrategy::MOST_FRACTIONAL;
            else if (val == "pseudocost") options.branching = BranchingStrategy::PSEUDOCOST;
            else if (val == "strong") options.branching = BranchingStrategy::STRONG_BRANCHING;
            else if (val == "reliability") options.branching = BranchingStrategy::RELIABILITY;
        } else if (arg == "--out" && i + 1 < argc) {
            out_file = argv[++i];
        } else if (arg == "--report" && i + 1 < argc) {
            report_file = argv[++i];
        }
    }

    try {
        Problem problem = load_problem(model_file);

        std::cout << "Problem: " << problem.name << "\n";
        std::cout << "Variables: " << problem.variables.size() << "\n";
        std::cout << "Constraints: " << problem.constraints.size() << "\n";
        std::cout << "Integer vars: " << problem.num_integer_vars() << "\n";

        Solver solver(options);
        auto start = std::chrono::high_resolution_clock::now();
        Solution solution = solver.solve(problem);
        auto end = std::chrono::high_resolution_clock::now();

        double elapsed = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "\nStatus: " << status_label(solution.status) << "\n";
        std::cout << "Objective: " << std::fixed << std::setprecision(6) << solution.objective_value << "\n";
        std::cout << "Gap: " << solution.gap << "\n";
        if (solution.bb_nodes > 0) {
            std::cout << "B&B nodes: " << solution.bb_nodes << "\n";
        }
        if (std::isfinite(solution.best_bound) &&
            std::abs(solution.best_bound - solution.objective_value) > 1e-9) {
            std::cout << "Best bound: " << std::fixed << std::setprecision(6) << solution.best_bound << "\n";
        }
        std::cout << "Time: " << elapsed << " ms\n";

        validation::SolutionVerifier verifier;
        validation::VerificationResult verification;
        if (solution.is_feasible()) {
            verification = verifier.verify_detailed(problem, to_model_solution(solution));
            bool verified_ok = verification.optimal && verification.feasible;
            std::cout << "Verification: "
                      << (verified_ok ? "PASS (primal+dual+complementarity OK)" : "PARTIAL")
                      << "\n";
            std::cout << "  primal infeasibility:  " << std::scientific << verification.primal_infeasibility << "\n";
            std::cout << "  dual infeasibility:    " << verification.dual_infeasibility << "\n";
            std::cout << "  complementarity:       " << verification.complementarity << "\n";
            if (problem.num_integer_vars() > 0) {
                std::cout << "  integrality violation: " << verification.integrality_violation << "\n";
            }
        } else {
            std::cout << "Verification: n/a (" << status_label(solution.status) << ")\n";
        }

        if (!solution.primal.empty()) {
            std::cout << "\nSolution (first 10 vars):\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(10, solution.primal.size()); ++i) {
                std::cout << "  x[" << i << "] = " << solution.primal[i] << "\n";
            }
        }

        if (!out_file.empty()) {
            write_solution_file(out_file, problem, solution);
            std::cout << "Solution written to " << out_file << "\n";
        }
        if (!report_file.empty()) {
            auto report = solve_report_json(problem, solution, elapsed, verification, options);
            std::ofstream ros(report_file);
            if (!ros.is_open()) {
                std::cerr << "Warning: cannot open report file: " << report_file << "\n";
            } else {
                ros << report.dump(2) << "\n";
                std::cout << "Report written to " << report_file << "\n";
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_iis(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "iis: missing model file\n";
        return 1;
    }

    std::string model_file = argv[2];

    try {
        Problem problem = load_problem(model_file);

        std::cout << "Problem: " << problem.name << "\n";
        std::cout << "Variables: " << problem.variables.size() << "\n";
        std::cout << "Constraints: " << problem.constraints.size() << "\n";

        Solver solver;
        validation::IISResult result = solver.compute_iis(problem);
        std::cout << result.to_string(problem);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_diagnose(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "diagnose: missing model file\n";
        return 1;
    }

    std::string model_file = argv[2];

    try {
        Problem problem = load_problem(model_file);

        std::cout << "Problem: " << problem.name << "\n";
        std::cout << "Variables: " << problem.variables.size() << "\n";
        std::cout << "Constraints: " << problem.constraints.size() << "\n\n";

        Solver solver;
        validation::CertificateResult res = solver.diagnose(problem);

        switch (res.status) {
            case model::ProblemStatus::OPTIMAL:    std::cout << "Status: OPTIMAL\n"; break;
            case model::ProblemStatus::INFEASIBLE: std::cout << "Status: INFEASIBLE\n"; break;
            case model::ProblemStatus::UNBOUNDED:  std::cout << "Status: UNBOUNDED\n"; break;
            case model::ProblemStatus::SUBOPTIMAL: std::cout << "Status: SUBOPTIMAL\n"; break;
            case model::ProblemStatus::TIME_LIMIT: std::cout << "Status: TIME_LIMIT\n"; break;
            case model::ProblemStatus::ITER_LIMIT: std::cout << "Status: ITER_LIMIT\n"; break;
            default:                               std::cout << "Status: UNKNOWN\n"; break;
        }
        if (!res.message.empty()) {
            std::cout << "Message: " << res.message << "\n";
        }

        if (res.farkas.valid) {
            std::cout << "Farkas certificate: VERIFIED\n";
            std::cout << "  multipliers:\n";
            for (std::size_t i = 0; i < res.farkas.multipliers.size() && i < problem.constraints.size(); ++i) {
                std::string name = problem.constraints[i].name.empty()
                                       ? std::to_string(i + 1)
                                       : problem.constraints[i].name;
                std::cout << "    " << name << "  " << std::scientific << std::setprecision(6)
                          << res.farkas.multipliers[i] << "\n";
            }
            std::cout << "  farkas_value = " << std::scientific << std::setprecision(6)
                      << res.farkas.farkas_value << "\n";
        }

        if (res.ray.valid) {
            std::cout << "Unbounded ray: VERIFIED\n";
            std::cout << "  direction:\n";
            for (std::size_t j = 0; j < res.ray.direction.size() && j < problem.variables.size(); ++j) {
                std::string name = problem.variables[j].name.empty()
                                       ? std::to_string(j + 1)
                                       : problem.variables[j].name;
                std::cout << "    " << name << "  " << std::scientific << std::setprecision(6)
                          << res.ray.direction[j] << "\n";
            }
            std::cout << "  objective_change = " << std::scientific << std::setprecision(6)
                      << res.ray.objective_direction << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_quality(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "quality: missing model file\n";
        return 1;
    }

    std::string model_file = argv[2];
    bool json_out = false;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json") json_out = true;
    }

    try {
        Problem problem = load_problem(model_file);

        std::cout << "Problem: " << problem.name << "\n";
        std::cout << "Variables: " << problem.variables.size() << "\n";
        std::cout << "Constraints: " << problem.constraints.size() << "\n\n";

        Solver solver;
        validation::NumericalDiagnostics d = solver.diagnose_numerical(problem);
        if (json_out) {
            std::cout << d.to_json();
            return 0;
        }
        std::cout << d.to_string();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_convert(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "convert: usage: convert <input> --to <mps|lp> [--out <output>]\n";
        return 1;
    }

    std::string input = argv[2];
    std::string output_format;
    std::string output_file;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--to" && i + 1 < argc) {
            output_format = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    if (output_format != "mps" && output_format != "lp") {
        std::cerr << "convert: invalid format: " << output_format << "\n";
        return 1;
    }

    try {
        Problem problem = load_problem(input);

        if (output_file.empty()) {
            output_file = input.substr(0, input.find_last_of('.')) + "." + output_format;
        }

        if (output_format == "mps") {
            problem.write_mps(output_file);
        } else {
            problem.write_lp(output_file);
        }

        std::cout << "Converted to " << output_file << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_inspect(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "inspect: missing model file\n";
        return 1;
    }

    std::string model_file = argv[2];

    try {
        Problem problem = load_problem(model_file);

        std::cout << "Problem: " << problem.name << "\n";
        std::cout << "Variables: " << problem.variables.size() << "\n";
        std::cout << "  Continuous: " << problem.num_continuous_vars() << "\n";
        std::cout << "  Integer: " << problem.num_integer_vars() << "\n";
        std::cout << "  Binary: " << problem.num_binary_vars() << "\n";
        std::cout << "Constraints: " << problem.constraints.size() << "\n";
        std::cout << "Nonzeros: " << problem.constraint_matrix.nnz() << "\n";
        std::cout << "Objective sense: " << (problem.obj_sense == model::ObjectiveSense::MINIMIZE ? "Minimize" : "Maximize") << "\n";
        std::cout << "Is LP: " << (problem.is_lp() ? "yes" : "no") << "\n";
        std::cout << "Is MILP: " << (problem.is_milp() ? "yes" : "no") << "\n";
        std::cout << "Is QP: " << (problem.is_qp() ? "yes" : "no") << "\n";
        std::cout << "Is MIQP: " << (problem.is_miqp() ? "yes" : "no") << "\n";
        std::cout << "Quadratic terms: " << problem.quadratic_terms.size() << "\n";
        std::cout << "SOS constraints: " << problem.sos_constraints.size() << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_interactive(int argc, char** argv) {
    hypernova::cli::InteractiveConfig config;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--resume" && i + 1 < argc) {
            config.resume_session = argv[++i];
        } else if (arg == "--save-session" && i + 1 < argc) {
            config.save_session = argv[++i];
        } else if (arg == "--output-format" && i + 1 < argc) {
            config.output_format = argv[++i];
        } else if (arg == "--output-file" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "--engine" && i + 1 < argc) {
            config.engine = argv[++i];
        } else if (arg == "--time-limit" && i + 1 < argc) {
            config.time_limit = std::stod(argv[++i]);
        } else if (arg == "--gap" && i + 1 < argc) {
            config.mip_gap = std::stod(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::stoi(argv[++i]);
        } else if (arg == "--advanced") {
            config.advanced = true;
        }
    }

    hypernova::cli::InteractiveSession session(config);
    return session.run(argc, argv);
}

int cmd_benchmark(int argc, char** argv) {
    hypernova::benchmark::BenchmarkConfig config;
    std::string out_json;
    std::string out_csv;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + arg);
            }
            return argv[++i];
        };
        if (arg == "--suite") {
            config.suite_name = next();
        } else if (arg == "--dir") {
            config.directory = next();
        } else if (arg == "--file") {
            config.files.push_back(next());
        } else if (arg == "--expect") {
            config.reference_csv = next();
        } else if (arg == "--no-reference") {
            config.use_builtin_netlib_reference = false;
        } else if (arg == "--engine") {
            std::string val = next();
            if (val == "simplex") config.engine = EngineType::PRIMAL_SIMPLEX;
            else if (val == "dual-simplex") config.engine = EngineType::DUAL_SIMPLEX;
            else if (val == "ipm") config.engine = EngineType::INTERIOR_POINT;
            else if (val == "bnb") config.engine = EngineType::BRANCH_AND_BOUND;
            else {
                std::cerr << "unknown engine: " << val << "\n";
                return 1;
            }
        } else if (arg == "--time-limit") {
            config.time_limit_seconds = std::stod(next());
        } else if (arg == "--gap") {
            config.mip_gap_tolerance = std::stod(next());
        } else if (arg == "--threads") {
            config.thread_count = std::stoi(next());
        } else if (arg == "--tol") {
            config.reference_rel_tol = std::stod(next());
        } else if (arg == "--out") {
            out_json = next();
        } else if (arg == "--csv") {
            out_csv = next();
        } else {
            std::cerr << "benchmark: unknown option: " << arg << "\n";
            return 1;
        }
    }

    if (config.directory.empty() && config.files.empty()) {
        std::cerr << "benchmark: provide --dir <path> or --file <path>\n";
        std::cout << "usage: hypernova benchmark --dir <path> [--suite name] [--expect refs.csv]\n"
                  << "                        [--engine auto|simplex|ipm|bnb] [--time-limit sec]\n"
                  << "                        [--out report.json] [--csv report.csv]\n";
        return 1;
    }

    try {
        auto report = hypernova::benchmark::run_benchmark(config);
        std::cout << report.to_summary_string();
        if (!out_json.empty()) {
            std::ofstream oss(out_json);
            if (oss.is_open()) oss << report.to_json().dump(2) << "\n";
        }
        if (!out_csv.empty()) {
            std::ofstream oss(out_csv);
            if (oss.is_open()) oss << report.to_csv();
        }
        return report.all_pass ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "benchmark error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_capabilities(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::cout << "HyperNova 0.1.0\n";
    std::cout << "Problem classes : LP, MILP, QP (convex), MIQP (convex)\n";
    std::cout << "Engines         : auto, primal-simplex, dual-simplex, interior-point,\n";
    std::cout << "                  branch-and-bound, branch-and-cut, qp-active-set,\n";
    std::cout << "                  qp-interior-point\n";
    std::cout << "Pricing         : dantzig, devex, steepest-edge (simplex)\n";
    std::cout << "Ratio test      : standard, harris (two-pass)\n";
    std::cout << "Node selection  : best-first, depth-first, best-estimate, hybrid\n";
    std::cout << "Branching       : most-fractional, pseudocost, strong, reliability\n";
    std::cout << "Presolve        : off, conservative, aggressive\n";
    std::cout << "Scaling         : none, geometric, curtis-reid\n";
    std::cout << "Parallel B&B    : threads >= 2 (work-stealing pool)\n";
    std::cout << "Diagnostics     : iis, diagnose (Farkas/unbounded-ray certificates),\n";
    std::cout << "                  quality (kappa + coefficient stats + probe residuals)\n";
    std::cout << "I/O             : MPS (free/fixed), CPLEX-style LP, JSON solution,\n";
    std::cout << "                  JSON report, interactive model builder\n";

    auto backends = execution::ComputeBackendFactory::available_backends();
    std::cout << "Compute backends:";
    if (backends.empty()) {
        std::cout << " none";
    }
    for (auto b : backends) {
        std::cout << " " << compute_backend_name(b);
    }
    std::cout << "\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "solve") {
        return cmd_solve(argc, argv);
    } else if (command == "iis") {
        return cmd_iis(argc, argv);
    } else if (command == "diagnose") {
        return cmd_diagnose(argc, argv);
    } else if (command == "quality") {
        return cmd_quality(argc, argv);
    } else if (command == "capabilities") {
        return cmd_capabilities(argc, argv);
    } else if (command == "interactive") {
        return cmd_interactive(argc, argv);
    } else if (command == "convert") {
        return cmd_convert(argc, argv);
    } else if (command == "inspect") {
        return cmd_inspect(argc, argv);
    } else if (command == "benchmark") {
        return cmd_benchmark(argc, argv);
    } else if (command == "help" || command == "--help" || command == "-h") {
        print_usage(argv[0]);
        return 0;
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        return 1;
    }
}