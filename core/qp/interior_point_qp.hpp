#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../numerical/sparse_matrix.hpp"
#include "../numerical/factorization.hpp"
#include <vector>
#include <cstddef>
#include <memory>
#include <functional>

namespace hypernova::qp {

struct InteriorPointQPOptions {
    int max_iterations = 100;
    double time_limit_seconds = 0.0;
    double convergence_tol = 1e-8;
    double complementarity_tol = 1e-8;
    double regularization = 1e-8;
    bool check_convexity = true;
    std::function<bool()> interrupt_callback;
};

struct InteriorPointQPResult {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<double> slack;
    std::size_t iterations = 0;
    double solve_time_ms = 0.0;
};

class InteriorPointQPSolver {
public:
    explicit InteriorPointQPSolver(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                                    const InteriorPointQPOptions& options = InteriorPointQPOptions());
    ~InteriorPointQPSolver() = default;

    InteriorPointQPResult solve(const model::Problem& problem);

    const InteriorPointQPOptions& options() const { return options_; }
    void set_options(const InteriorPointQPOptions& opts) { options_ = opts; }

private:
    numerical::ToleranceConfig tol_;
    InteriorPointQPOptions options_;

    std::vector<double> x_, y_, z_, s_;
    std::vector<double> dx_, dy_, dz_, ds_;
    std::unique_ptr<numerical::SparseFactorization> kkt_factorization_;

    void initialize(const model::Problem& problem);
    bool check_convexity(const model::Problem& problem);
    void compute_affine_step(const model::Problem& problem);
    void compute_centering_step(const model::Problem& problem, double mu, double sigma);
    void update_variables(double alpha_p, double alpha_d);
    bool check_convergence(const model::Problem& problem, double mu);
    double compute_mu() const;
    double compute_alpha(const std::vector<double>& vars, const std::vector<double>& dirs);
    void form_kkt_system(const model::Problem& problem, std::vector<double>& rhs);
    void solve_kkt_system(const model::Problem& problem, const std::vector<double>& rhs, std::vector<double>& sol);
};

} // namespace hypernova::qp