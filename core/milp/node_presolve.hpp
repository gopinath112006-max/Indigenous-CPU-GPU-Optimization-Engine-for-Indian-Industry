#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace hypernova::milp {

// A single directional bound change imposed at a B&B node (branching decision
// or presolve tightening). Directional bounds avoid the ambiguity of a plain
// {variable, value} pair, which could otherwise be misread as both a lower and
// an upper bound.
struct BoundChange {
    std::size_t var;
    double value;
    bool is_lower;  // true  => x_var >= value ; false => x_var <= value
};

// Results of running activity/duty bound-tightening propagation at a B&B node.
struct NodePresolveResult {
    bool feasible = true;   // false => the node is infeasible by propagation alone
    // Tightening measured against the ORIGINAL variable bounds (not the node
    // bounds already applied): caller overlays these on top of node bounds.
    std::vector<std::pair<std::size_t, double>> tightened_lb; // forced lower bounds
    std::vector<std::pair<std::size_t, double>> tightened_ub; // forced upper bounds
    unsigned passes = 0;
    std::string message;
};

// Cheap, sound bound-propagation presolve for an LP relaxation at a node:
//   * detects rows that are infeasible under the current bounds (prunes the
//     node without a simplex solve),
//   * tightens variable bounds implied by the constraint activities,
//   * floors/ceils the tightened bounds of integer variables, and
//   * fixes variables whose bounds are forced together.
// Iterates until fixpoint or a pass cap. Works on problem.quadratic_terms == empty.
class NodePresolver {
public:
    explicit NodePresolver(const numerical::ToleranceConfig& tol =
                               numerical::ToleranceConfig::industrial_defaults())
        : tol_(tol) {}

    // node_bounds: directional (variable index, bound value, direction) changes
    // already imposed at the node (branching decisions). All returned
    // tightenings are for ORIGINAL variable indices.
    NodePresolveResult tighten(const model::Problem& problem,
                               const std::vector<BoundChange>& node_bounds) const;

private:
    numerical::ToleranceConfig tol_;
};

} // namespace hypernova::milp