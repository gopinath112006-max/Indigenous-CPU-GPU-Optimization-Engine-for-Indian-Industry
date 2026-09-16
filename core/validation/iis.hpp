#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../lp/simplex.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace hypernova::validation {

// A variable bound that participates in the infeasible subsystem.
struct IISBound {
    std::size_t variable;
    bool upper;  // true = upper bound, false = lower bound
};

// Result of an IIS (irreducible infeasible subsystem) computation.
//
// An IIS is a set of constraints and variable bounds such that removing any
// single element makes the remaining subsystem feasible. When a model is
// infeasible there may be several IISs; the deletion filter (Chinneck) returns
// the one that happens to be found first.
struct IISResult {
    bool found = false;
    std::vector<std::size_t> constraint_indices;
    std::vector<IISBound> bounds;
    std::size_t lp_solves = 0;
    double time_ms = 0.0;
    std::string message;

    std::string to_string(const model::Problem& p) const;
};

// Computes an IIS for an infeasible model by repeated LP feasibility tests
// (deletion filter). Only the rows and the finite variable bounds of the LP
// relaxation are considered; integrality constraints, SOS constraints and
// quadratic terms are ignored by the feasibility probe.
class IISComputer {
public:
    explicit IISComputer(const numerical::ToleranceConfig& tol =
                             numerical::ToleranceConfig::industrial_defaults(),
                         const lp::SimplexOptions& options = lp::SimplexOptions());
    ~IISComputer() = default;

    IISResult compute(const model::Problem& problem);

private:
    numerical::ToleranceConfig tol_;
    lp::SimplexOptions options_;

    bool is_infeasible(const model::Problem& candidate, std::size_t& solves);
};

} // namespace hypernova::validation