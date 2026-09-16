#include "problem.hpp"

namespace hypernova::model {

Basis Basis::from_solution(const Solution& sol, const Problem& prob) {
    Basis basis;
    basis.var_status.resize(prob.variables.size(), 0);
    basis.con_status.resize(prob.constraints.size(), 0);

    if (!sol.basis_status.empty() && sol.basis_status.size() == prob.variables.size() + prob.constraints.size()) {
        for (std::size_t i = 0; i < prob.variables.size(); ++i) {
            basis.var_status[i] = sol.basis_status[i];
        }
        for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
            basis.con_status[i] = sol.basis_status[prob.variables.size() + i];
        }
    } else if (!sol.primal.empty()) {
        for (std::size_t i = 0; i < prob.variables.size(); ++i) {
            double val = sol.primal[i];
            double lb = prob.variables[i].lower_bound;
            double ub = prob.variables[i].upper_bound;
            if (val <= lb + 1e-9) basis.var_status[i] = -1;
            else if (val >= ub - 1e-9) basis.var_status[i] = 1;
            else basis.var_status[i] = 0;
        }
        for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
            basis.con_status[i] = 0;
        }
    }

    return basis;
}

} // namespace hypernova::model