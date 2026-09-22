#include <hypernova/api.hpp>
#include <iostream>

using namespace hypernova;

int main() {
    Problem p;
    auto x = p.add_variable(0.0, 1.0, model::VarType::CONTINUOUS, "x");
    p.set_objective({{x, -4.0}}, model::ObjectiveSense::MINIMIZE);
    p.add_quadratic_term(x, x, 2.0); // 1/2 * 2.0 * x^2 = x^2

    SolverOptions opts;
    opts.engine = EngineType::QP_INTERIOR_POINT;
    Solver s(opts);
    auto res = s.solve(p);
    
    std::cout << "Obj: " << res.objective_value << " x: " << res.primal[0] << "\n";
    return 0;
}
