#include "solve_report.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace hypernova::validation {

nlohmann::json SolveReport::to_json() const {
    nlohmann::json j;
    j["problem_name"] = problem_name;
    j["solver_version"] = solver_version;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    j["status"] = static_cast<int>(status);
    j["objective_value"] = objective_value;
    j["best_bound"] = best_bound;
    j["gap"] = gap;
    j["num_variables"] = num_variables;
    j["num_constraints"] = num_constraints;
    j["num_integer_vars"] = num_integer_vars;
    j["num_nonzeros"] = num_nonzeros;
    j["presolve_passes"] = presolve_passes;
    j["presolve_removed_rows"] = presolve_removed_rows;
    j["presolve_removed_cols"] = presolve_removed_cols;
    j["presolve_time_ms"] = presolve_time_ms;
    j["simplex_iterations"] = simplex_iterations;
    j["ipm_iterations"] = ipm_iterations;
    j["bb_nodes"] = bb_nodes;
    j["cuts_added"] = cuts_added;
    j["solve_time_ms"] = solve_time_ms;
    j["total_time_ms"] = total_time_ms;
    j["primal_infeasibility"] = primal_infeasibility;
    j["dual_infeasibility"] = dual_infeasibility;
    j["complementarity"] = complementarity;
    j["integrality_violation"] = integrality_violation;
    j["is_feasible"] = is_feasible;
    j["optimality_proven"] = optimality_proven;
    j["engine_used"] = engine_used;
    j["gpu_used"] = gpu_used;
    j["threads_used"] = threads_used;
    return j;
}

SolveReport SolveReport::from_json(const nlohmann::json& j) {
    SolveReport report;
    report.problem_name = j.value("problem_name", "");
    report.solver_version = j.value("solver_version", "0.1.0");
    if (j.contains("timestamp")) {
        report.timestamp = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(j["timestamp"].get<int64_t>()));
    }
    report.status = static_cast<model::ProblemStatus>(j.value("status", 0));
    report.objective_value = j.value("objective_value", 0.0);
    report.best_bound = j.value("best_bound", 0.0);
    report.gap = j.value("gap", 0.0);
    report.num_variables = j.value("num_variables", 0);
    report.num_constraints = j.value("num_constraints", 0);
    report.num_integer_vars = j.value("num_integer_vars", 0);
    report.num_nonzeros = j.value("num_nonzeros", 0);
    report.presolve_passes = j.value("presolve_passes", 0);
    report.presolve_removed_rows = j.value("presolve_removed_rows", 0);
    report.presolve_removed_cols = j.value("presolve_removed_cols", 0);
    report.presolve_time_ms = j.value("presolve_time_ms", 0.0);
    report.simplex_iterations = j.value("simplex_iterations", 0);
    report.ipm_iterations = j.value("ipm_iterations", 0);
    report.bb_nodes = j.value("bb_nodes", 0);
    report.cuts_added = j.value("cuts_added", 0);
    report.solve_time_ms = j.value("solve_time_ms", 0.0);
    report.total_time_ms = j.value("total_time_ms", 0.0);
    report.primal_infeasibility = j.value("primal_infeasibility", 0.0);
    report.dual_infeasibility = j.value("dual_infeasibility", 0.0);
    report.complementarity = j.value("complementarity", 0.0);
    report.integrality_violation = j.value("integrality_violation", 0.0);
    report.is_feasible = j.value("is_feasible", false);
    report.optimality_proven = j.value("optimality_proven", false);
    report.engine_used = j.value("engine_used", "");
    report.gpu_used = j.value("gpu_used", false);
    report.threads_used = j.value("threads_used", 1);
    return report;
}

std::string SolveReport::to_string() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "HyperNova Solve Report\n";
    oss << "========================\n";
    oss << "Problem:         " << problem_name << "\n";
    oss << "Status:          " << static_cast<int>(status) << "\n";
    oss << "Objective:       " << objective_value << "\n";
    oss << "Best Bound:      " << best_bound << "\n";
    oss << "Gap:             " << gap << "\n";
    oss << "Variables:       " << num_variables << "\n";
    oss << "Constraints:     " << num_constraints << "\n";
    oss << "Integer Vars:    " << num_integer_vars << "\n";
    oss << "Nonzeros:        " << num_nonzeros << "\n";
    oss << "Presolve Passes: " << presolve_passes << "\n";
    oss << "Presolve Rows:   " << presolve_removed_rows << "\n";
    oss << "Presolve Cols:   " << presolve_removed_cols << "\n";
    oss << "Simplex Iters:   " << simplex_iterations << "\n";
    oss << "IPM Iters:       " << ipm_iterations << "\n";
    oss << "B&B Nodes:       " << bb_nodes << "\n";
    oss << "Cuts Added:      " << cuts_added << "\n";
    oss << "Solve Time (ms): " << solve_time_ms << "\n";
    oss << "Total Time (ms): " << total_time_ms << "\n";
    oss << "Feasible:        " << (is_feasible ? "Yes" : "No") << "\n";
    oss << "Opt Proven:      " << (optimality_proven ? "Yes" : "No") << "\n";
    oss << "Engine:          " << engine_used << "\n";
    oss << "GPU Used:        " << (gpu_used ? "Yes" : "No") << "\n";
    oss << "Threads:         " << threads_used << "\n";
    return oss.str();
}

} // namespace hypernova::validation