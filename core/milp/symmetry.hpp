#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace hypernova::milp {

// Symmetry information detected by grouping variables whose COLUMNS and bound/
// objective data are identical: swapping two variables inside one group leaves
// the formulation (and its feasible region) unchanged, so the group is a set of
// fully symmetrically interchangeable integer variables.
struct SymmetryDetection {
    // Each entry lists variable indices (ascending) of one symmetric group.
    std::vector<std::vector<std::size_t>> variable_groups;
    std::size_t num_symmetric_variables = 0;
    // GE-0 ordering rows x_g[0] >= x_g[1] >= ... >= x_g[n-1] that break the
    // symmetry of a group (one row per adjacent pair).
    std::vector<std::vector<std::pair<std::size_t, double>>> ordering_rows;
    std::vector<std::string> row_names;
};

// Detects identical-column symmetry among integer/binary variables. Only
// groups of 2+ variables are reported.
class SymmetryDetector {
public:
    explicit SymmetryDetector(const numerical::ToleranceConfig& tol =
                                  numerical::ToleranceConfig::industrial_defaults())
        : tol_(tol) {}

    SymmetryDetection detect(const model::Problem& problem) const;

private:
    numerical::ToleranceConfig tol_;
};

} // namespace hypernova::milp