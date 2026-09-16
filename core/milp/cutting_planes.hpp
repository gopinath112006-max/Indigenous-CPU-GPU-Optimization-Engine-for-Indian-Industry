#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include <mutex>
#include <vector>
#include <cstddef>

namespace hypernova::milp {

struct Cut {
    std::vector<std::pair<std::size_t, double>> coefficients;
    model::ConstraintSense sense = model::ConstraintSense::LE;
    double rhs = 0.0;
    std::string name;
    double efficacy = 0.0;
    int age = 0;
};

class CutGenerator {
public:
    explicit CutGenerator(const numerical::ToleranceConfig& tol = numerical::ToleranceConfig::industrial_defaults());
    ~CutGenerator() = default;

    std::vector<Cut> generate_gomory_cuts(const model::Problem& problem,
                                           const std::vector<double>& lp_solution);
    std::vector<Cut> generate_mir_cuts(const model::Problem& problem,
                                        const std::vector<double>& lp_solution);
    std::vector<Cut> generate_knapsack_covers(const model::Problem& problem,
                                               const std::vector<double>& lp_solution);
    std::vector<Cut> generate_clique_cuts(const model::Problem& problem,
                                           const std::vector<double>& lp_solution);
    std::vector<Cut> generate_flow_covers(const model::Problem& problem,
                                           const std::vector<double>& lp_solution);

    void add_cut(const Cut& cut);
    void remove_old_cuts(int max_age);
    void clear();

    const std::vector<Cut>& cuts() const { return cuts_; }

private:
    numerical::ToleranceConfig tol_;
    std::vector<Cut> cuts_;
    int next_cut_id_ = 0;

    std::vector<Cut> generate_gomory_from_row(const model::Problem& problem,
                                               const std::vector<double>& lp_solution,
                                               std::size_t row_idx);
    std::vector<Cut> generate_mir_from_row(const model::Problem& problem,
                                            const std::vector<double>& lp_solution,
                                            std::size_t row_idx);
};

class CutPool {
public:
    CutPool(int max_size = 1000, double efficacy_threshold = 0.01);
    ~CutPool() = default;

    void add_cut(const Cut& cut);
    std::vector<Cut> get_active_cuts() const;
    void age_cuts();
    void remove_ineffective_cuts();
    void remove_ineffective_cuts_nolock();  // caller must hold mutex_

private:
    int max_size_;
    double efficacy_threshold_;
    std::vector<Cut> cuts_;
    mutable std::mutex mutex_;  // parallel B&B: adds/reads may come from workers
};

} // namespace hypernova::milp