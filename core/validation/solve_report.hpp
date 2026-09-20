#pragma once

#include "../model/problem.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace hypernova::validation {

struct SolveReport {
    std::string problem_name;
    std::string solver_version = "0.1.0";
    std::chrono::system_clock::time_point timestamp;

    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    double best_bound = 0.0;
    double gap = 0.0;

    std::size_t num_variables = 0;
    std::size_t num_constraints = 0;
    std::size_t num_integer_vars = 0;
    std::size_t num_nonzeros = 0;

    std::size_t presolve_passes = 0;
    std::size_t presolve_removed_rows = 0;
    std::size_t presolve_removed_cols = 0;
    double presolve_time_ms = 0.0;

    std::size_t simplex_iterations = 0;
    std::size_t ipm_iterations = 0;
    std::size_t bb_nodes = 0;
    std::size_t cuts_added = 0;

    double solve_time_ms = 0.0;
    double total_time_ms = 0.0;

    double primal_infeasibility = 0.0;
    double dual_infeasibility = 0.0;
    double complementarity = 0.0;
    double integrality_violation = 0.0;

    bool is_feasible = false;
    bool optimality_proven = false;

    std::string engine_used;
    bool gpu_used = false;
    int threads_used = 1;

    nlohmann::json to_json() const;
    static SolveReport from_json(const nlohmann::json& j);
    std::string to_string() const;
};

} // namespace hypernova::validation