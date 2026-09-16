#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include <vector>
#include <cstddef>

namespace hypernova::milp {

struct HeuristicResult {
    bool found_solution = false;
    std::vector<double> solution;
    double objective_value = 0.0;
};

class Heuristic {
public:
    explicit Heuristic(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults());
    virtual ~Heuristic() = default;

    virtual HeuristicResult run(const model::Problem& problem,
                                 const std::vector<double>& lp_solution) = 0;

protected:
    numerical::ToleranceConfig tol_;
    bool check_feasibility(const model::Problem& problem, const std::vector<double>& solution) const;
    double compute_objective(const model::Problem& problem, const std::vector<double>& solution) const;
};

class RoundingHeuristic : public Heuristic {
public:
    using Heuristic::Heuristic;
    HeuristicResult run(const model::Problem& problem, const std::vector<double>& lp_solution) override;
};

class DivingHeuristic : public Heuristic {
public:
    explicit DivingHeuristic(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                              int max_iterations = 20);
    HeuristicResult run(const model::Problem& problem, const std::vector<double>& lp_solution) override;

private:
    int max_iterations_;
};

class FeasibilityPumpHeuristic : public Heuristic {
public:
    using Heuristic::Heuristic;
    HeuristicResult run(const model::Problem& problem, const std::vector<double>& lp_solution) override;
};

class RINSHeuristic : public Heuristic {
public:
    explicit RINSHeuristic(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults(),
                            int frequency = 10);
    HeuristicResult run(const model::Problem& problem, const std::vector<double>& lp_solution) override;

    void set_incumbent(const std::vector<double>& incumbent) { incumbent_ = incumbent; }

private:
    int frequency_;
    std::vector<double> incumbent_;
};

class HeuristicManager {
public:
    explicit HeuristicManager(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults());
    ~HeuristicManager() = default;

    void add_heuristic(std::unique_ptr<Heuristic> heuristic);
    HeuristicResult run_all(const model::Problem& problem, const std::vector<double>& lp_solution);
    void set_incumbent(const std::vector<double>& incumbent);

private:
    numerical::ToleranceConfig tol_;
    std::vector<std::unique_ptr<Heuristic>> heuristics_;
};

} // namespace hypernova::milp