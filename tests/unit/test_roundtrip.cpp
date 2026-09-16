#include <gtest/gtest.h>
#include <model/problem.hpp>
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>
#include <presolve/presolve.hpp>
#include <lp/simplex.hpp>
#include <validation/solution_verifier.hpp>

using namespace hypernova::model;
using namespace hypernova::presolve;
using namespace hypernova::lp;
using namespace hypernova::validation;
using Solution = hypernova::model::Solution;

static Problem build_test_lp() {
    ProblemBuilder builder("rt_test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 2.0}, {1, 1.0}}, ConstraintSense::LE, 14.0, "c2");
    builder.set_objective({{0, 3.0}, {1, 5.0}}, ObjectiveSense::MAXIMIZE);
    return builder.build();
}

TEST(RoundTripTest, MPSWriteAndParse) {
    Problem original = build_test_lp();
    write_mps(original, "test_rt.mps");

    MPSParser parser;
    auto result = parser.parse_file("test_rt.mps");
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.problem.variables.size(), original.variables.size());
    EXPECT_EQ(result.problem.constraints.size(), original.constraints.size());

    for (std::size_t j = 0; j < original.variables.size(); ++j) {
        EXPECT_NEAR(result.problem.variables[j].lower_bound,
                    original.variables[j].lower_bound, 1e-9);
        EXPECT_NEAR(result.problem.variables[j].upper_bound,
                    original.variables[j].upper_bound, 1e-9);
    }
}

TEST(RoundTripTest, LPWriteAndParse) {
    Problem original = build_test_lp();
    write_lp(original, "test_rt.lp");

    LPParser parser;
    auto result = parser.parse_file("test_rt.lp");
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.problem.variables.size(), original.variables.size());
    EXPECT_EQ(result.problem.constraints.size(), original.constraints.size());

    for (std::size_t j = 0; j < original.variables.size(); ++j) {
        EXPECT_NEAR(result.problem.variables[j].lower_bound,
                    original.variables[j].lower_bound, 1e-9);
        EXPECT_NEAR(result.problem.variables[j].upper_bound,
                    original.variables[j].upper_bound, 1e-9);
    }
}

TEST(RoundTripTest, MPSPreservesMaximizeObjective) {
    Problem original = build_test_lp();

    SimplexSolver solver;
    auto sol = solver.solve(original);
    ASSERT_EQ(sol.status, ProblemStatus::OPTIMAL);
    double obj_before = 0.0;
    for (std::size_t j = 0; j < original.variables.size(); ++j) {
        obj_before += original.variables[j].objective_coeff * sol.primal[j];
    }

    write_mps(original, "test_rt_obj_max.mps");

    MPSParser parser;
    auto result = parser.parse_file("test_rt_obj_max.mps");
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.problem.obj_sense, ObjectiveSense::MAXIMIZE);
    for (std::size_t j = 0; j < original.variables.size(); ++j) {
        EXPECT_NEAR(result.problem.variables[j].objective_coeff,
                    original.variables[j].objective_coeff, 1e-9);
    }

    SimplexSolver solver2;
    auto sol2 = solver2.solve(result.problem);
    ASSERT_EQ(sol2.status, ProblemStatus::OPTIMAL);
    double obj_after = 0.0;
    for (std::size_t j = 0; j < result.problem.variables.size(); ++j) {
        obj_after += result.problem.variables[j].objective_coeff * sol2.primal[j];
    }

    EXPECT_NEAR(obj_before, 50.0, 1e-4);
    EXPECT_NEAR(obj_after, 50.0, 1e-4);
}

TEST(RoundTripTest, MPSQuadraticRoundTrip) {
    ProblemBuilder builder("rt_qp");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 4.0, "c1");
    builder.set_objective({{0, -4.0}, {1, -5.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem original = builder.build();

    write_mps(original, "test_rt_qp.mps");

    MPSParser parser;
    auto result = parser.parse_file("test_rt_qp.mps");
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.problem.quadratic_terms.size(), original.quadratic_terms.size());
}

TEST(RoundTripTest, MPSBinaryRoundTrip) {
    ProblemBuilder builder("rt_bin");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 1.0, "c1");
    builder.set_objective({{0, 5.0}, {1, 4.0}}, ObjectiveSense::MAXIMIZE);
    Problem original = builder.build();

    write_mps(original, "test_rt_bin.mps");

    MPSParser parser;
    auto result = parser.parse_file("test_rt_bin.mps");
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.problem.num_binary_vars(), 2u);
}

TEST(RoundTripTest, PresolvePostsolveLP) {
    ProblemBuilder builder("rt_presolve");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 2.0}, {1, 1.0}}, ConstraintSense::LE, 14.0, "c2");
    builder.set_objective({{0, 3.0}, {1, 5.0}}, ObjectiveSense::MAXIMIZE);
    Problem original = builder.build();

    Presolver presolver;
    auto presolve_result = presolver.presolve(original);

    SimplexSolver solver;
    auto sol = solver.solve(presolve_result.reduced_problem);
    ASSERT_EQ(sol.status, ProblemStatus::OPTIMAL);

    Solution reduced_sol;
    reduced_sol.primal = sol.primal;
    reduced_sol.dual = sol.dual;
    reduced_sol.status = sol.status;

    auto postsolved = presolver.postsolve(original, reduced_sol, presolve_result);

    SolutionVerifier verifier;
    Solution check_sol;
    check_sol.primal = postsolved.primal;
    check_sol.status = ProblemStatus::OPTIMAL;

    EXPECT_TRUE(verifier.verify_feasible(original, check_sol));
}

TEST(RoundTripTest, PresolvePreservesObjective) {
    ProblemBuilder builder("rt_obj");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 2.0}, {1, 1.0}}, ConstraintSense::LE, 14.0, "c2");
    builder.set_objective({{0, 3.0}, {1, 5.0}}, ObjectiveSense::MAXIMIZE);
    Problem original = builder.build();

    Presolver presolver;
    auto presolve_result = presolver.presolve(original);

    SimplexSolver solver;
    auto sol = solver.solve(presolve_result.reduced_problem);
    ASSERT_EQ(sol.status, ProblemStatus::OPTIMAL);

    double obj = 0.0;
    for (std::size_t j = 0; j < original.variables.size(); ++j) {
        obj += original.variables[j].objective_coeff * sol.primal[j];
    }

    EXPECT_NEAR(obj, 50.0, 1e-2);
}
