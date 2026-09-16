#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../numerical/scaling.hpp"
#include <vector>
#include <cstddef>
#include <optional>

namespace hypernova::presolve {

struct PresolveResult {
    struct DroppedSingletonRow {
        std::size_t row;
        std::size_t col;
        double coeff;
        double rhs;
        model::ConstraintSense sense;
        double implied_bound;
    };

    model::Problem reduced_problem;
    std::vector<std::size_t> removed_rows;
    std::vector<std::size_t> removed_cols;
    std::vector<std::pair<std::size_t, std::pair<double, double>>> tightened_bounds;
    std::vector<std::pair<std::size_t, std::pair<std::size_t, double>>> fixed_variables;
    std::vector<DroppedSingletonRow> dropped_singletons;
    std::vector<std::size_t> row_mapping;
    std::vector<std::size_t> col_mapping;
    double time_ms = 0.0;
    int passes = 0;
};

struct PresolveOptions {
    int max_passes = 10;
    bool remove_redundant_rows = true;
    bool remove_redundant_cols = true;
    bool tighten_bounds = true;
    bool fix_variables = true;
    bool remove_singletons = true;
    bool coefficient_strengthening = false;
    bool dual_reductions = false;
    bool scaling = true;
    numerical::ScalingMethod scaling_method = numerical::ScalingMethod::GEOMETRIC;
    double singleton_tol = 1e-9;
    double redundancy_tol = 1e-9;
};

class Presolver {
public:
    explicit Presolver(const PresolveOptions& options = PresolveOptions());
    ~Presolver() = default;

    PresolveResult presolve(const model::Problem& problem);

    model::Solution postsolve(const model::Problem& original,
                               const model::Solution& reduced_solution,
                               const PresolveResult& presolve_result) const;

private:
    PresolveOptions options_;
    numerical::ToleranceConfig tol_;

    void find_redundant_rows(const model::Problem& prob,
                              std::vector<bool>& row_redundant,
                              std::vector<bool>& col_redundant) const;
    void find_singletons(const model::Problem& prob,
                          std::vector<bool>& row_singleton,
                          std::vector<bool>& col_singleton) const;
    void tighten_bounds(const model::Problem& prob,
                         std::vector<double>& lb,
                         std::vector<double>& ub,
                         std::vector<std::pair<std::size_t, std::pair<double, double>>>& tightened) const;
    void fix_variables(const model::Problem& prob,
                        const std::vector<double>& lb,
                        const std::vector<double>& ub,
                        std::vector<std::pair<std::size_t, std::pair<std::size_t, double>>>& fixed) const;
    void apply_coefficient_strengthening(model::Problem& prob) const;

    model::Problem build_reduced_problem(const model::Problem& prob,
                                          const std::vector<bool>& row_keep,
                                          const std::vector<bool>& col_keep,
                                          const std::vector<double>& new_lb,
                                          const std::vector<double>& new_ub) const;
};

} // namespace hypernova::presolve