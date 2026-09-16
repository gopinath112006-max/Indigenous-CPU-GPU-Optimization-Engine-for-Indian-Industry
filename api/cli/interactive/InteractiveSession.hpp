#pragma once

#include "PromptEngine.hpp"
#include "ProblemBuilder.hpp"
#include "SessionManager.hpp"
#include "ExportManager.hpp"
#include <hypernova/api.hpp>

namespace hypernova::cli {

struct InteractiveConfig {
    std::string resume_session;
    std::string save_session;
    std::string output_format;
    std::string output_file;
    std::string engine = "auto";
    double time_limit = 3600.0;
    double mip_gap = 1e-4;
    int threads = 0;
    bool advanced = false;
};

class InteractiveSession {
public:
    explicit InteractiveSession(const InteractiveConfig& config = {});

    int run(int argc, char** argv);

private:
    int collect_metadata();
    int collect_variables();
    int collect_constraints();
    int collect_quadratic_terms();
    int collect_sos_constraints();
    int collect_solver_options();
    int solve_and_display();
    int post_solve_actions();

    void display_problem_summary();
    void display_solution(const model::Problem& problem,
                          double objective_value,
                          const std::vector<double>& primal);

    model::Problem build_problem();

    InteractiveConfig config_;
    SessionState session_;
    PromptEngine prompt_;
};

} // namespace hypernova::cli
