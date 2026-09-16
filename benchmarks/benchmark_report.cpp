#include "benchmark_report.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <chrono>

namespace hypernova::benchmark {

namespace {

template <typename T>
std::string fmt(const T& value, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

std::string csv_escape(const std::string& value) {
    if (value.find(',') == std::string::npos && value.find('"') == std::string::npos &&
        value.find('\n') == std::string::npos) {
        return value;
    }
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') out += "\"\"";
        else out += ch;
    }
    out += "\"";
    return out;
}

} // namespace

std::string iso_timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::string SuiteReport::status_name(model::ProblemStatus status) {
    switch (status) {
        case model::ProblemStatus::OPTIMAL: return "OPTIMAL";
        case model::ProblemStatus::INFEASIBLE: return "INFEASIBLE";
        case model::ProblemStatus::UNBOUNDED: return "UNBOUNDED";
        case model::ProblemStatus::SUBOPTIMAL: return "SUBOPTIMAL";
        case model::ProblemStatus::TIME_LIMIT: return "TIME_LIMIT";
        case model::ProblemStatus::ITER_LIMIT: return "ITER_LIMIT";
        case model::ProblemStatus::NUMERICAL_ERROR: return "NUMERICAL_ERROR";
        case model::ProblemStatus::INTERRUPTED: return "INTERRUPTED";
        default: return "UNKNOWN";
    }
}

double shifted_geometric_mean(const std::vector<double>& times_seconds, double shift) {
    std::vector<double> shifted;
    shifted.reserve(times_seconds.size());
    for (double t : times_seconds) {
        if (std::isfinite(t) && t + shift > 0.0) {
            shifted.push_back(std::log(t + shift));
        }
    }
    if (shifted.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double v : shifted) {
        sum += v;
    }
    return std::exp(sum / static_cast<double>(shifted.size())) - shift;
}

nlohmann::json InstanceResult::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["file"] = file;
    j["problem_type"] = problem_type;
    j["num_variables"] = num_variables;
    j["num_constraints"] = num_constraints;
    j["num_nonzeros"] = num_nonzeros;
    j["num_integer_vars"] = num_integer_vars;
    j["status"] = SuiteReport::status_name(status);
    j["objective"] = objective;
    j["best_bound"] = best_bound;
    j["gap"] = gap;
    j["solve_time_seconds"] = solve_time_seconds;
    j["thread_count"] = thread_count;
    j["nodes_per_second"] = nodes_per_second;
    j["engine_used"] = engine_used;
    j["iterations"] = iterations;
    j["verified"] = verified;
    j["reference_status"] = reference_status;
    j["reference_objective"] = reference_objective;
    j["pass"] = pass;
    j["message"] = message;
    return j;
}

std::string InstanceResult::to_csv_row() const {
    std::ostringstream oss;
    oss << csv_escape(name) << "," << csv_escape(file) << "," << csv_escape(problem_type) << ","
        << num_variables << "," << num_constraints << "," << num_nonzeros << "," << num_integer_vars << ","
        << SuiteReport::status_name(status) << "," << fmt(objective, 10) << "," << fmt(best_bound, 10) << ","
        << fmt(gap, 8) << "," << fmt(solve_time_seconds, 6) << "," << thread_count << ","
        << fmt(nodes_per_second, 1) << "," << csv_escape(engine_used) << ","
        << iterations << "," << (verified ? "yes" : "no") << "," << csv_escape(reference_status) << ","
        << (has_reference ? fmt(reference_objective, 10) : "") << "," << (pass ? "PASS" : "FAIL") << ","
        << csv_escape(message);
    return oss.str();
}

void SuiteReport::summarize() {
    total = instances.size();
    num_optimal = 0;
    num_infeasible = 0;
    num_unbounded = 0;
    num_other = 0;
    num_verified = 0;
    num_pass = 0;

    std::vector<double> optimal_times;
    optimal_times.reserve(total);

    for (const auto& instance : instances) {
        switch (instance.status) {
            case model::ProblemStatus::OPTIMAL:
                ++num_optimal;
                optimal_times.push_back(instance.solve_time_seconds);
                break;
            case model::ProblemStatus::INFEASIBLE:
                ++num_infeasible;
                break;
            case model::ProblemStatus::UNBOUNDED:
                ++num_unbounded;
                break;
            default:
                ++num_other;
                break;
        }
        if (instance.verified) ++num_verified;
        if (instance.pass) ++num_pass;
    }

    geomean_time = shifted_geometric_mean(optimal_times, 0.0);
    shifted_geomean_time = shifted_geometric_mean(optimal_times, 10.0);
    all_pass = total > 0 && num_pass == total;
}

nlohmann::json SuiteReport::to_json() const {
    nlohmann::json j;
    j["suite_name"] = suite_name;
    j["solver_version"] = solver_version;
    j["timestamp"] = timestamp;
    j["reference_tolerance"] = reference_tolerance;
    j["total"] = total;
    j["num_optimal"] = num_optimal;
    j["num_infeasible"] = num_infeasible;
    j["num_unbounded"] = num_unbounded;
    j["num_other"] = num_other;
    j["num_verified"] = num_verified;
    j["num_pass"] = num_pass;
    j["all_pass"] = all_pass;
    j["geomean_time_seconds"] = geomean_time;
    j["shifted_geomean_time_seconds"] = shifted_geomean_time;
    j["instances"] = nlohmann::json::array();
    for (const auto& instance : instances) {
        j["instances"].push_back(instance.to_json());
    }
    j["thread_sweep"] = nlohmann::json::array();
    for (const auto& instance : thread_sweep) {
        j["thread_sweep"].push_back(instance.to_json());
    }
    return j;
}

std::string SuiteReport::to_summary_string() const {
    std::ostringstream oss;
    oss << "HyperNova Benchmark Suite Report\n";
    oss << "====================\n";
    oss << "Suite:           " << suite_name << "\n";
    oss << "Solver version:  " << solver_version << "\n";
    oss << "Timestamp:       " << timestamp << "\n";
    oss << "Reference tol:   " << reference_tolerance << " (relative)\n\n";
    oss << "Instances:       " << total << "\n";
    oss << "Optimal:         " << num_optimal << "\n";
    oss << "Infeasible:      " << num_infeasible << "\n";
    oss << "Unbounded:       " << num_unbounded << "\n";
    oss << "Other:           " << num_other << "\n";
    oss << "Verified:        " << num_verified << "\n";
    oss << "Passing:         " << num_pass << " / " << total << "\n";
    oss << "All pass:        " << (all_pass ? "YES" : "NO") << "\n";
    oss << "Geomean time:    " << fmt(geomean_time, 4) << " s\n";
    oss << "Shifted geomean: " << fmt(shifted_geomean_time, 4) << " s (shift 10 s)\n";

    if (!thread_sweep.empty()) {
        oss << "\nThread sweep (B&B only):\n";
        oss << "  instance                 threads   status    nodes   time_s   nodes/s   speedup\n";
        for (const auto& s : thread_sweep) {
            double speedup = 0.0;
            const InstanceResult* base = nullptr;
            for (const auto& base_row : instances) {
                if (base_row.name == s.name) { base = &base_row; break; }
            }
            for (const auto& s0 : thread_sweep) {
                if (s0.name == s.name && s0.thread_count == 1) { base = &s0; break; }
            }
            if (base && base->solve_time_seconds > 0.0) {
                speedup = base->solve_time_seconds / s.solve_time_seconds;
            }
            std::string inst = s.name;
            if (inst.size() > 24) inst = inst.substr(0, 21) + "...";
            oss << "  " << std::setw(24) << std::left << inst << std::right << " "
                << std::setw(7) << s.thread_count << "  "
                << std::setw(10) << std::left << status_name(s.status) << std::right << " "
                << std::setw(6) << s.iterations << "  "
                << std::setw(8) << std::fixed << std::setprecision(3) << s.solve_time_seconds << "  "
                << std::setw(8) << std::setprecision(0) << s.nodes_per_second << "  "
                << (speedup > 0.0 ? fmt(speedup, 2) : "-") << "\n";
        }
    }
    return oss.str();
}

std::string SuiteReport::to_csv() const {
    std::ostringstream oss;
    oss << "name,file,problem_type,vars,cons,nnz,integer_vars,status,objective,best_bound,gap,"
           "time_s,threads,nodes_per_s,engine,iterations,verified,reference_status,reference_objective,result,message\n";
    for (const auto& instance : instances) {
        oss << instance.to_csv_row() << "\n";
    }
    if (!thread_sweep.empty()) {
        oss << "\n# thread sweep rows\n";
        for (const auto& instance : thread_sweep) {
            oss << instance.to_csv_row() << "\n";
        }
    }
    return oss.str();
}

} // namespace hypernova::benchmark