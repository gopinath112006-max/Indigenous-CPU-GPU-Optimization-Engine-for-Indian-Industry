#define HYPERNOVA_C_API_EXPORTS
#include "hypernova_c_api.h"
#include <hypernova/api.hpp>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

struct CProblem {
    hypernova::Problem cpp_prob;
};

struct CSolverOptions {
    hypernova::SolverOptions cpp_opts;
};

struct CSolution {
    hypernova::Solution cpp_sol;
};

struct CIISResult {
    hypernova::validation::IISResult cpp_iis;
};

struct CNumericalDiagnostics {
    hypernova::validation::NumericalDiagnostics cpp_diag;
};

static hypernova::model::VarType to_cpp_vartype(HNVarType t) {
    switch (t) {
        case HN_VAR_INTEGER: return hypernova::model::VarType::INTEGER;
        case HN_VAR_BINARY: return hypernova::model::VarType::BINARY;
        case HN_VAR_SEMI_CONTINUOUS: return hypernova::model::VarType::SEMI_CONTINUOUS;
        case HN_VAR_SEMI_INTEGER: return hypernova::model::VarType::SEMI_INTEGER;
        default: return hypernova::model::VarType::CONTINUOUS;
    }
}

static hypernova::model::ConstraintSense to_cpp_sense(HNConstraintSense s) {
    switch (s) {
        case HN_SENSE_GE: return hypernova::model::ConstraintSense::GE;
        case HN_SENSE_EQ: return hypernova::model::ConstraintSense::EQ;
        default: return hypernova::model::ConstraintSense::LE;
    }
}

static hypernova::model::ObjectiveSense to_cpp_objsense(HNObjectiveSense s) {
    return (s == HN_OBJ_MAXIMIZE) ? hypernova::model::ObjectiveSense::MAXIMIZE
                                  : hypernova::model::ObjectiveSense::MINIMIZE;
}

static HNProblemStatus to_c_status(hypernova::model::ProblemStatus s) {
    switch (s) {
        case hypernova::model::ProblemStatus::OPTIMAL: return HN_STATUS_OPTIMAL;
        case hypernova::model::ProblemStatus::INFEASIBLE: return HN_STATUS_INFEASIBLE;
        case hypernova::model::ProblemStatus::UNBOUNDED: return HN_STATUS_UNBOUNDED;
        case hypernova::model::ProblemStatus::SUBOPTIMAL: return HN_STATUS_SUBOPTIMAL;
        case hypernova::model::ProblemStatus::TIME_LIMIT: return HN_STATUS_TIME_LIMIT;
        case hypernova::model::ProblemStatus::ITER_LIMIT: return HN_STATUS_ITER_LIMIT;
        case hypernova::model::ProblemStatus::NUMERICAL_ERROR: return HN_STATUS_NUMERICAL_ERROR;
        case hypernova::model::ProblemStatus::INTERRUPTED: return HN_STATUS_INTERRUPTED;
        default: return HN_STATUS_UNKNOWN;
    }
}

extern "C" {

CProblem* hypernova_problem_create(const char* name) {
    auto p = new CProblem();
    if (name) p->cpp_prob.name = name;
    return p;
}

void hypernova_problem_destroy(CProblem* problem) {
    delete problem;
}

size_t hypernova_problem_add_variable(CProblem* problem, double lb, double ub, HNVarType type, const char* name) {
    if (!problem) return 0;
    std::string vname = name ? name : "";
    return problem->cpp_prob.add_variable(lb, ub, to_cpp_vartype(type), vname);
}

size_t hypernova_problem_add_constraint(CProblem* problem, size_t num_terms, const size_t* var_indices, const double* coeffs, HNConstraintSense sense, double rhs, const char* name) {
    if (!problem) return 0;
    std::vector<std::pair<std::size_t, double>> terms;
    terms.reserve(num_terms);
    for (size_t i = 0; i < num_terms; ++i) {
        terms.push_back({var_indices[i], coeffs[i]});
    }
    std::string cname = name ? name : "";
    return problem->cpp_prob.add_constraint(terms, to_cpp_sense(sense), rhs, cname);
}

void hypernova_problem_set_objective(CProblem* problem, size_t num_terms, const size_t* var_indices, const double* coeffs, HNObjectiveSense sense) {
    if (!problem) return;
    std::vector<std::pair<std::size_t, double>> terms;
    terms.reserve(num_terms);
    for (size_t i = 0; i < num_terms; ++i) {
        terms.push_back({var_indices[i], coeffs[i]});
    }
    problem->cpp_prob.set_objective(terms, to_cpp_objsense(sense));
}

void hypernova_problem_add_quadratic_term(CProblem* problem, size_t row, size_t col, double coeff) {
    if (!problem) return;
    problem->cpp_prob.add_quadratic_term(row, col, coeff);
}

size_t hypernova_problem_get_num_variables(const CProblem* problem) {
    return problem ? problem->cpp_prob.variables.size() : 0;
}

size_t hypernova_problem_get_num_constraints(const CProblem* problem) {
    return problem ? problem->cpp_prob.constraints.size() : 0;
}

CSolverOptions* hypernova_options_create(void) {
    return new CSolverOptions();
}

void hypernova_options_destroy(CSolverOptions* options) {
    delete options;
}

void hypernova_options_set_time_limit(CSolverOptions* options, double seconds) {
    if (options) options->cpp_opts.time_limit_seconds = seconds;
}

void hypernova_options_set_mip_gap(CSolverOptions* options, double gap) {
    if (options) options->cpp_opts.mip_gap_tolerance = gap;
}

void hypernova_options_set_threads(CSolverOptions* options, int threads) {
    if (options) options->cpp_opts.thread_count = threads;
}

void hypernova_options_set_use_gpu(CSolverOptions* options, int use_gpu) {
    if (options) {
        options->cpp_opts.use_gpu = (use_gpu != 0);
        options->cpp_opts.compute_target = (use_gpu != 0) ? hypernova::ComputeTarget::CPU_GPU_AUTO : hypernova::ComputeTarget::CPU_ONLY;
    }
}

CSolution* hypernova_solve(const CProblem* problem, const CSolverOptions* options) {
    if (!problem) return nullptr;
    auto sol = new CSolution();
    hypernova::SolverOptions opts = options ? options->cpp_opts : hypernova::SolverOptions();
    hypernova::Solver solver(opts);
    sol->cpp_sol = solver.solve(problem->cpp_prob);
    return sol;
}

void hypernova_solution_destroy(CSolution* solution) {
    delete solution;
}

HNProblemStatus hypernova_solution_get_status(const CSolution* solution) {
    return solution ? to_c_status(solution->cpp_sol.status) : HN_STATUS_UNKNOWN;
}

double hypernova_solution_get_objective(const CSolution* solution) {
    return solution ? solution->cpp_sol.objective_value : 0.0;
}

double hypernova_solution_get_best_bound(const CSolution* solution) {
    return solution ? solution->cpp_sol.best_bound : 0.0;
}

double hypernova_solution_get_gap(const CSolution* solution) {
    return solution ? solution->cpp_sol.gap : 0.0;
}

double hypernova_solution_get_solve_time_ms(const CSolution* solution) {
    return solution ? solution->cpp_sol.solve_time_ms : 0.0;
}

size_t hypernova_solution_get_primal(const CSolution* solution, double* out_buffer, size_t max_len) {
    if (!solution || !out_buffer || max_len == 0) return 0;
    size_t copy_len = std::min(max_len, solution->cpp_sol.primal.size());
    std::copy(solution->cpp_sol.primal.begin(), solution->cpp_sol.primal.begin() + copy_len, out_buffer);
    return copy_len;
}

size_t hypernova_solution_get_dual(const CSolution* solution, double* out_buffer, size_t max_len) {
    if (!solution || !out_buffer || max_len == 0) return 0;
    size_t copy_len = std::min(max_len, solution->cpp_sol.dual.size());
    std::copy(solution->cpp_sol.dual.begin(), solution->cpp_sol.dual.begin() + copy_len, out_buffer);
    return copy_len;
}

size_t hypernova_solution_get_reduced_costs(const CSolution* solution, double* out_buffer, size_t max_len) {
    if (!solution || !out_buffer || max_len == 0) return 0;
    size_t copy_len = std::min(max_len, solution->cpp_sol.reduced_costs.size());
    std::copy(solution->cpp_sol.reduced_costs.begin(), solution->cpp_sol.reduced_costs.begin() + copy_len, out_buffer);
    return copy_len;
}

const char* hypernova_solution_get_backend_used(const CSolution* solution) {
    return solution ? solution->cpp_sol.backend_used.c_str() : "unknown";
}

CIISResult* hypernova_compute_iis(const CProblem* problem) {
    if (!problem) return nullptr;
    auto res = new CIISResult();
    hypernova::Solver solver;
    res->cpp_iis = solver.compute_iis(problem->cpp_prob);
    return res;
}

void hypernova_iis_result_destroy(CIISResult* iis) {
    delete iis;
}

int hypernova_iis_result_found(const CIISResult* iis) {
    return (iis && iis->cpp_iis.found) ? 1 : 0;
}

CNumericalDiagnostics* hypernova_diagnose_numerical(const CProblem* problem) {
    if (!problem) return nullptr;
    auto diag = new CNumericalDiagnostics();
    hypernova::Solver solver;
    diag->cpp_diag = solver.diagnose_numerical(problem->cpp_prob);
    return diag;
}

void hypernova_diagnostics_destroy(CNumericalDiagnostics* diag) {
    delete diag;
}

const char* hypernova_diagnostics_get_grade(const CNumericalDiagnostics* diag) {
    return diag ? diag->cpp_diag.quality.c_str() : "unknown";
}

double hypernova_diagnostics_get_kappa(const CNumericalDiagnostics* diag) {
    return diag ? diag->cpp_diag.condition_estimate : 0.0;
}

} // extern "C"
