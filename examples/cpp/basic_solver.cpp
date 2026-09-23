#include <hypernova/api.hpp>
#include <iostream>

using namespace hypernova;

int main() {
    Problem prob;
    auto x1 = prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x1");
    auto x2 = prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x2");
    
    prob.set_objective({{x1, 2.0}, {x2, 3.0}}, model::ObjectiveSense::MAXIMIZE);
    prob.add_constraint({{x1, 1.0}, {x2, 2.0}}, model::ConstraintSense::LE, 10.0, "c1");
    
    SolverOptions opts;
    opts.time_limit_seconds = 10.0;
    
    Solver solver(opts);
    Solution result = solver.solve(prob);
    
    if (result.status == model::ProblemStatus::OPTIMAL) {
        std::cout << "Optimal Objective: " << result.objective_value << "\n";
        std::cout << "x1: " << result.primal[x1] << "\n";
        std::cout << "x2: " << result.primal[x2] << "\n";
        return 0;
    }
    return 1;
}
