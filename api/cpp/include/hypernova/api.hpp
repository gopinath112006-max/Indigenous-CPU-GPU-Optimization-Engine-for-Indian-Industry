#pragma once

#include <model/problem.hpp>
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>
#include <numerical/tolerance.hpp>
#include <lp/simplex.hpp>
#include <validation/solution_verifier.hpp>
#include <validation/iis.hpp>
#include <validation/certificate.hpp>
#include <validation/diagnostics.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace hypernova {

enum class EngineType {
    AUTO,
    PRIMAL_SIMPLEX,
    DUAL_SIMPLEX,
    INTERIOR_POINT,
    BRANCH_AND_BOUND,
    BRANCH_AND_CUT,
    QP_ACTIVE_SET,
    QP_INTERIOR_POINT
};

enum class NodeSelectionStrategy {
    BEST_FIRST,
    DEPTH_FIRST,
    BEST_ESTIMATE,
    HYBRID
};

enum class BranchingStrategy {
    MOST_FRACTIONAL,
    PSEUDOCOST,
    STRONG_BRANCHING,
    RELIABILITY
};

enum class PresolveLevel {
    OFF,
    CONSERVATIVE,
    AGGRESSIVE
};

enum class ScalingMethod {
    NONE,
    GEOMETRIC,
    CURTIS_REID
};

enum class ComputeTarget {
    CPU_ONLY,
    CPU_GPU_AUTO,
    CPU_GPU_FORCE
};

struct SolverOptions {
    double time_limit_seconds = 3600.0;
    double mip_gap_tolerance = 1e-4;
    int thread_count = 0;
    EngineType engine = EngineType::AUTO;
    NodeSelectionStrategy node_selection = NodeSelectionStrategy::HYBRID;
    BranchingStrategy branching = BranchingStrategy::RELIABILITY;
    PresolveLevel presolve = PresolveLevel::AGGRESSIVE;
    ScalingMethod scaling = ScalingMethod::NONE;
    ComputeTarget compute_target = ComputeTarget::CPU_GPU_AUTO;
    bool use_gpu = true;
    bool heuristic_feasibility_pump = false;
    bool heuristic_rins = false;
    int rins_frequency = 10;
    numerical::ToleranceConfig tolerances;
    // Simplex internals. Defaults reproduce the baseline engine behavior
    // (Bland anti-cycling entering + standard ratio test) which the Netlib
    // regression suite is pinned against; toggling these on activates the
    // implemented Devex / steepest-edge pricing and the Harris two-pass
    // ratio test.
    lp::PricingStrategy simplex_pricing = lp::PricingStrategy::DEVEX;
    lp::RatioTest simplex_ratio_test = lp::RatioTest::HARRIS_TWO_PASS;
    bool simplex_bland_rule = false;
    std::size_t simplex_steepest_edge_shortlist = 16;

    std::size_t node_limit = 0;
    std::size_t solution_limit = 0;

    SolverOptions() {
        tolerances = numerical::ToleranceConfig::industrial_defaults();
    }
};

struct Solution {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    double best_bound = 0.0;
    double gap = 0.0;

    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<double> reduced_costs;
    std::vector<int> basis_status;

    std::size_t simplex_iterations = 0;
    std::size_t ipm_iterations = 0;
    std::size_t bb_nodes = 0;
    double solve_time_ms = 0.0;
    std::string backend_used = "cpu";

    bool is_optimal() const { return status == model::ProblemStatus::OPTIMAL; }
    bool is_feasible() const {
        return status == model::ProblemStatus::OPTIMAL ||
               status == model::ProblemStatus::SUBOPTIMAL;
    }
};

struct Basis {
    std::vector<int> var_status;
    std::vector<int> con_status;
};

class Solver {
public:
    explicit Solver(const SolverOptions& options = SolverOptions());
    ~Solver();

    Solution solve(const model::Problem& problem);
    Solution warm_solve(const model::Problem& problem, const Basis& start_basis);

    // Computes an IIS (irreducible infeasible subsystem) for an infeasible model
    // using the deletion filter. Returns IISResult::found == false when the model
    // is feasible (or the LP relaxation has no IIS to report).
    validation::IISResult compute_iis(const model::Problem& problem);

    // Produces a verified Farkas certificate for an infeasible model or a
    // verified unbounded-ray certificate for an unbounded model, reporting
    // status and (for feasible models) a plain feasibility verdict.
    validation::CertificateResult diagnose(const model::Problem& problem);

    // Produces a numerical-quality report: coefficient statistics, a
    // condition-number estimate kappa(A*A^T), probe-solve residuals, and a
    // one-word grade ("good" | "warn" | "poor") with warnings.
    validation::NumericalDiagnostics diagnose_numerical(const model::Problem& problem);

    void set_options(const SolverOptions& options);
    const SolverOptions& options() const;

    void interrupt();
    bool interrupted() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

class Problem : public model::Problem {
public:
    using model::Problem::Problem;

    static Problem from_mps(const std::string& path);
    static Problem from_lp(const std::string& path);

    void write_mps(const std::string& path) const;
    void write_lp(const std::string& path) const;

    std::size_t add_variable(double lb, double ub, model::VarType type, const std::string& name = "");
    std::size_t add_constraint(const std::vector<std::pair<std::size_t, double>>& coeffs,
                                model::ConstraintSense sense, double rhs, const std::string& name = "");
    void set_objective(const std::vector<std::pair<std::size_t, double>>& coeffs,
                        model::ObjectiveSense sense = model::ObjectiveSense::MINIMIZE);
    void add_quadratic_term(std::size_t row, std::size_t col, double coeff);
};

using VariableHandle = std::size_t;
using ConstraintHandle = std::size_t;

} // namespace hypernova