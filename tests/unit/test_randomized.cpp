#include <gtest/gtest.h>
#include <random>
#include <cmath>
#include <vector>
#include <algorithm>
#include "../core/model/problem.hpp"
#include "../core/milp/branch_and_bound.hpp"

using namespace hypernova;
using namespace hypernova::model;

TEST(RandomizedTest, DenseMILPMiniatures) {
    std::mt19937 gen(42);
    std::uniform_real_distribution<> obj_dist(-10.0, 10.0);
    std::uniform_real_distribution<> coeff_dist(-5.0, 5.0);
    std::uniform_real_distribution<> rhs_dist(-20.0, 20.0);
    
    int num_tests = 50;
    
    for (int t = 0; t < num_tests; ++t) {
        ProblemBuilder builder("rand_" + std::to_string(t));
        int n_vars = 5;
        int n_cons = 3;
        
        std::vector<std::size_t> vars;
        for (int i = 0; i < n_vars; ++i) {
            std::string name = "x" + std::to_string(i);
            VarType type = (i % 2 == 0) ? VarType::INTEGER : VarType::CONTINUOUS;
            vars.push_back(builder.add_variable(0.0, 5.0, type, name));
        }
        
        std::vector<std::pair<std::size_t, double>> obj;
        for (int i = 0; i < n_vars; ++i) {
            obj.push_back({vars[i], std::round(obj_dist(gen))});
        }
        builder.set_objective(obj, ObjectiveSense::MAXIMIZE);
        
        for (int i = 0; i < n_cons; ++i) {
            std::vector<std::pair<std::size_t, double>> expr;
            for (int j = 0; j < n_vars; ++j) {
                if (gen() % 2 == 0) {
                    expr.push_back({vars[j], std::round(coeff_dist(gen))});
                }
            }
            builder.add_constraint(expr, ConstraintSense::LE, std::round(rhs_dist(gen)) + 10.0, "c" + std::to_string(i));
        }
        
        model::Problem prob = builder.build();
        
        milp::BranchAndBoundOptions opts;
        opts.cuts.gomory_cuts = true;
        opts.heuristics.rounding = true;
        milp::BranchAndBoundSolver solver(numerical::ToleranceConfig::industrial_defaults(), opts);
        
        auto res = solver.solve(prob);
        EXPECT_TRUE(res.status == ProblemStatus::OPTIMAL || 
                    res.status == ProblemStatus::INFEASIBLE || 
                    res.status == ProblemStatus::UNBOUNDED);
                    
        if (res.status == ProblemStatus::OPTIMAL) {
            for (std::size_t j = 0; j < prob.variables.size(); ++j) {
                if (prob.variables[j].type != VarType::CONTINUOUS) {
                    double val = res.primal[j];
                    EXPECT_NEAR(val, std::round(val), 1e-4);
                }
            }
        }
    }
}
