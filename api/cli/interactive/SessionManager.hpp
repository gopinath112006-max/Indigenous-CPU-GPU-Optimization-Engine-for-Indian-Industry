#pragma once

#include "ProblemBuilder.hpp"
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace hypernova::cli {

struct SolverOptionsSpec {
    std::string engine = "auto";
    double time_limit_seconds = 3600.0;
    double mip_gap = 1e-4;
    int threads = 0;
    bool use_gpu = true;
};

struct SessionState {
    std::string problem_name;
    model::ObjectiveSense obj_sense = model::ObjectiveSense::MINIMIZE;
    std::vector<VariableSpec> variables;
    std::vector<ConstraintSpec> constraints;
    std::vector<QuadraticTermSpec> quadratic_terms;
    std::vector<SOSConstraintSpec> sos_constraints;
    SolverOptionsSpec solver_options;
    std::string created_at;
    std::string updated_at;
    int version = 1;
};

class SessionManager {
public:
    static bool save_session(const SessionState& state, const std::string& filepath);

    static std::optional<SessionState> load_session(const std::string& filepath);

    static std::string generate_session_path(const std::string& problem_name,
                                              const std::string& directory = ".");

    static std::string timestamp_now();

    static std::string sanitize_filename(const std::string& name);
};

} // namespace hypernova::cli
