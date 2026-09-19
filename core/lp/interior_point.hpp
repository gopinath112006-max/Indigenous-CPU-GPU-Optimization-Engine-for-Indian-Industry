#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../numerical/sparse_matrix.hpp"
#include "../numerical/factorization.hpp"
#include <vector>
#include <cstddef>
#include <memory>
#include <functional>

namespace hypernova::execution { class IComputeBackend; }

namespace hypernova::lp {

struct InteriorPointOptions {
    int max_iterations = 100;
    double time_limit_seconds = 0.0;
    double convergence_tol = 1e-8;
    double complementarity_tol = 1e-8;
    double regularization = 1e-8;
    bool use_augmented_system = true;
    bool crossover = true;
    int crossover_max_iter = 100;
    std::function<bool()> interrupt_callback;
    std::shared_ptr<execution::IComputeBackend> compute_backend = nullptr;
};

struct InteriorPointResult {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<double> slack;
    std::vector<double> reduced_costs;
    std::size_t iterations = 0;
    double solve_time_ms = 0.0;
};

class InteriorPointSolver {
public:
    explicit InteriorPointSolver(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                                  const InteriorPointOptions& options = InteriorPointOptions());
    ~InteriorPointSolver() = default;

    InteriorPointResult solve(const model::Problem& problem);

    const InteriorPointOptions& options() const { return options_; }
    void set_options(const InteriorPointOptions& opts) { options_ = opts; }

private:
    numerical::ToleranceConfig tol_;
    InteriorPointOptions options_;
};

} // namespace hypernova::lp