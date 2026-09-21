#pragma once

#include <model/problem.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cstddef>

namespace hypernova::benchmark {

struct InstanceResult {
    std::string name;
    std::string file;
    std::string problem_type; // "LP", "MILP", "QP", "parse-failed", "unsupported"

    std::size_t num_variables = 0;
    std::size_t num_constraints = 0;
    std::size_t num_nonzeros = 0;
    std::size_t num_integer_vars = 0;

    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective = 0.0;
    double best_bound = 0.0;
    double gap = 0.0;
    double solve_time_seconds = 0.0;
    int thread_count = 0;
    double nodes_per_second = 0.0; // B&B nodes / solve time (0.0 when not applicable)

    std::string engine_used;
    std::size_t iterations = 0; // simplex/IPM/QP iterations, or B&B nodes

    bool verified = false; // validation layer independently re-checked the result

    bool has_reference = false;
    std::string reference_status; // "min", "max", "inf", "unb"
    double reference_objective = 0.0;

    bool pass = false;
    std::string message;

    nlohmann::json to_json() const;
    std::string to_csv_row() const;
};

struct SuiteReport {
    std::string suite_name;
    std::string solver_version = "0.1.0";
    std::string timestamp;
    std::string reference_tolerance = "1e-6";
    
    std::string cpu_info;
    std::string gpu_info;
    
    // Options used
    int threads = 1;
    std::string presolve;
    std::string scaling;
    double gap_tol = 1e-4;
    double time_limit = 0.0;
    std::vector<InstanceResult> instances;
    std::vector<InstanceResult> thread_sweep; // extra B&B runs across --sweep thread counts

    std::size_t total = 0;
    std::size_t num_optimal = 0;
    std::size_t num_infeasible = 0;
    std::size_t num_unbounded = 0;
    std::size_t num_other = 0;
    std::size_t num_verified = 0;
    std::size_t num_pass = 0;
    bool all_pass = false;

    double geomean_time = 0.0;
    double shifted_geomean_time = 0.0;

    void summarize();
    nlohmann::json to_json() const;
    std::string to_summary_string() const;
    std::string to_csv() const;

    static std::string status_name(model::ProblemStatus status);
};

// Industry-standard aggregate: exp(mean(log(t + shift))) - shift. Times in seconds.
double shifted_geometric_mean(const std::vector<double>& times_seconds, double shift);

std::string iso_timestamp_now();

} // namespace hypernova::benchmark