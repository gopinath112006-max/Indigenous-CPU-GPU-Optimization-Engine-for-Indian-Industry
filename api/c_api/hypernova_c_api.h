#ifndef HYPERNOVA_C_API_H
#define HYPERNOVA_C_API_H

#include <stddef.h>

#ifdef _WIN32
  #ifdef HYPERNOVA_C_API_EXPORTS
    #define HYPERNOVA_C_API __declspec(dllexport)
  #else
    #define HYPERNOVA_C_API __declspec(dllimport)
  #endif
#else
  #define HYPERNOVA_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct CProblem CProblem;
typedef struct CSolverOptions CSolverOptions;
typedef struct CSolution CSolution;
typedef struct CIISResult CIISResult;
typedef struct CNumericalDiagnostics CNumericalDiagnostics;

/* Variable and constraint types */
typedef enum {
    HN_VAR_CONTINUOUS = 0,
    HN_VAR_INTEGER = 1,
    HN_VAR_BINARY = 2,
    HN_VAR_SEMI_CONTINUOUS = 3,
    HN_VAR_SEMI_INTEGER = 4
} HNVarType;

typedef enum {
    HN_SENSE_LE = 0,
    HN_SENSE_GE = 1,
    HN_SENSE_EQ = 2
} HNConstraintSense;

typedef enum {
    HN_OBJ_MINIMIZE = 0,
    HN_OBJ_MAXIMIZE = 1
} HNObjectiveSense;

typedef enum {
    HN_STATUS_UNKNOWN = 0,
    HN_STATUS_OPTIMAL = 1,
    HN_STATUS_INFEASIBLE = 2,
    HN_STATUS_UNBOUNDED = 3,
    HN_STATUS_SUBOPTIMAL = 4,
    HN_STATUS_TIME_LIMIT = 5,
    HN_STATUS_ITER_LIMIT = 6,
    HN_STATUS_NUMERICAL_ERROR = 7,
    HN_STATUS_INTERRUPTED = 8
} HNProblemStatus;

/* Problem API */
HYPERNOVA_C_API CProblem* hypernova_problem_create(const char* name);
HYPERNOVA_C_API void hypernova_problem_destroy(CProblem* problem);

HYPERNOVA_C_API size_t hypernova_problem_add_variable(CProblem* problem, double lb, double ub, HNVarType type, const char* name);
HYPERNOVA_C_API size_t hypernova_problem_add_constraint(CProblem* problem, size_t num_terms, const size_t* var_indices, const double* coeffs, HNConstraintSense sense, double rhs, const char* name);
HYPERNOVA_C_API void hypernova_problem_set_objective(CProblem* problem, size_t num_terms, const size_t* var_indices, const double* coeffs, HNObjectiveSense sense);
HYPERNOVA_C_API void hypernova_problem_add_quadratic_term(CProblem* problem, size_t row, size_t col, double coeff);

HYPERNOVA_C_API size_t hypernova_problem_get_num_variables(const CProblem* problem);
HYPERNOVA_C_API size_t hypernova_problem_get_num_constraints(const CProblem* problem);

/* Solver Options API */
HYPERNOVA_C_API CSolverOptions* hypernova_options_create(void);
HYPERNOVA_C_API void hypernova_options_destroy(CSolverOptions* options);

HYPERNOVA_C_API void hypernova_options_set_time_limit(CSolverOptions* options, double seconds);
HYPERNOVA_C_API void hypernova_options_set_mip_gap(CSolverOptions* options, double gap);
HYPERNOVA_C_API void hypernova_options_set_threads(CSolverOptions* options, int threads);
HYPERNOVA_C_API void hypernova_options_set_use_gpu(CSolverOptions* options, int use_gpu);

/* Solver Execution API */
HYPERNOVA_C_API CSolution* hypernova_solve(const CProblem* problem, const CSolverOptions* options);
HYPERNOVA_C_API void hypernova_solution_destroy(CSolution* solution);

/* Solution Inspection API */
HYPERNOVA_C_API HNProblemStatus hypernova_solution_get_status(const CSolution* solution);
HYPERNOVA_C_API double hypernova_solution_get_objective(const CSolution* solution);
HYPERNOVA_C_API double hypernova_solution_get_best_bound(const CSolution* solution);
HYPERNOVA_C_API double hypernova_solution_get_gap(const CSolution* solution);
HYPERNOVA_C_API double hypernova_solution_get_solve_time_ms(const CSolution* solution);
HYPERNOVA_C_API size_t hypernova_solution_get_primal(const CSolution* solution, double* out_buffer, size_t max_len);
HYPERNOVA_C_API size_t hypernova_solution_get_dual(const CSolution* solution, double* out_buffer, size_t max_len);
HYPERNOVA_C_API size_t hypernova_solution_get_reduced_costs(const CSolution* solution, double* out_buffer, size_t max_len);
HYPERNOVA_C_API const char* hypernova_solution_get_backend_used(const CSolution* solution);

/* Diagnostics & IIS API */
HYPERNOVA_C_API CIISResult* hypernova_compute_iis(const CProblem* problem);
HYPERNOVA_C_API void hypernova_iis_result_destroy(CIISResult* iis);
HYPERNOVA_C_API int hypernova_iis_result_found(const CIISResult* iis);

HYPERNOVA_C_API CNumericalDiagnostics* hypernova_diagnose_numerical(const CProblem* problem);
HYPERNOVA_C_API void hypernova_diagnostics_destroy(CNumericalDiagnostics* diag);
HYPERNOVA_C_API const char* hypernova_diagnostics_get_grade(const CNumericalDiagnostics* diag);
HYPERNOVA_C_API double hypernova_diagnostics_get_kappa(const CNumericalDiagnostics* diag);

#ifdef __cplusplus
}
#endif

#endif /* HYPERNOVA_C_API_H */
