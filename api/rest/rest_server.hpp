#pragma once

#include <model/problem.hpp>
#include <model/problem_json.hpp>
#include <hypernova/api.hpp>

#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <future>
#include <chrono>

namespace hypernova::rest {

struct JobStatus {
    std::string job_id;
    std::string name;
    std::string status; // PENDING, RUNNING, OPTIMAL, INFEASIBLE, ERROR
    double objective_value = 0.0;
    double solve_time_ms = 0.0;
    nlohmann::json solution_json;
};

class RESTJobSystem {
public:
    static RESTJobSystem& instance() {
        static RESTJobSystem sys;
        return sys;
    }

    std::string submit_job(const nlohmann::json& problem_json, const SolverOptions& options = SolverOptions()) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string job_id = "job_" + std::to_string(++job_counter_);

        JobStatus status;
        status.job_id = job_id;
        status.name = problem_json.value("name", "unnamed");
        status.status = "PENDING";
        jobs_[job_id] = status;

        // Process asynchronously
        std::thread([this, job_id, problem_json, options]() {
            execute_job(job_id, problem_json, options);
        }).detach();

        return job_id;
    }

    nlohmann::json get_job_status(const std::string& job_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(job_id);
        if (it == jobs_.end()) {
            return {{"error", "Job not found"}, {"job_id", job_id}};
        }

        const auto& j = it->second;
        return {
            {"job_id", j.job_id},
            {"name", j.name},
            {"status", j.status},
            {"objective_value", j.objective_value},
            {"solve_time_ms", j.solve_time_ms},
            {"result", j.solution_json}
        };
    }

    nlohmann::json list_all_jobs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [id, job] : jobs_) {
            arr.push_back({
                {"job_id", job.job_id},
                {"name", job.name},
                {"status", job.status},
                {"objective_value", job.objective_value}
            });
        }
        return {{"jobs", arr}};
    }

private:
    RESTJobSystem() = default;

    void execute_job(const std::string& job_id, const nlohmann::json& problem_json, const SolverOptions& options) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_[job_id].status = "RUNNING";
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        try {
            model::Problem problem = model::problem_from_json(problem_json);
            Solver solver(options);
            Solution sol = solver.solve(problem);
            auto t1 = std::chrono::high_resolution_clock::now();

            double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::lock_guard<std::mutex> lock(mutex_);
            auto& j = jobs_[job_id];
            j.status = (sol.status == model::ProblemStatus::OPTIMAL) ? "OPTIMAL" :
                       (sol.status == model::ProblemStatus::INFEASIBLE) ? "INFEASIBLE" : "COMPLETED";
            j.objective_value = sol.objective_value;
            j.solve_time_ms = elapsed_ms;

            nlohmann::json res;
            res["status_code"] = static_cast<int>(sol.status);
            res["primal"] = sol.primal;
            res["dual"] = sol.dual;
            res["iterations"] = sol.simplex_iterations + sol.ipm_iterations + sol.bb_nodes;
            j.solution_json = res;
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_[job_id].status = "ERROR";
            jobs_[job_id].solution_json = {{"error", ex.what()}};
        }
    }

    mutable std::mutex mutex_;
    std::size_t job_counter_ = 0;
    std::unordered_map<std::string, JobStatus> jobs_;
};

} // namespace hypernova::rest
