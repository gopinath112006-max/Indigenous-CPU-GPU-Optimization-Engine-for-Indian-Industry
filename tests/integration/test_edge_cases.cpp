#include <gtest/gtest.h>
#include <model/problem.hpp>
#include <lp/simplex.hpp>
#include <milp/branch_and_bound.hpp>
#include <qp/active_set.hpp>
#include <presolve/presolve.hpp>
#include <validation/solution_verifier.hpp>

using namespace hypernova::model;
using namespace hypernova::lp;
using namespace hypernova::milp;
using namespace hypernova::qp;
using namespace hypernova::presolve;
using namespace hypernova::validation;

static bool check_primal_feasible(const Problem& prob, const std::vector<double>& x, double tol = 1e-6) {
    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (x[j] < prob.variables[j].lower_bound - tol) return false;
        if (x[j] > prob.variables[j].upper_bound + tol) return false;
    }
    const auto& A = prob.constraint_matrix;
    for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
        double activity = 0.0;
        if (A.order() == StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                activity += A.values()[k] * x[A.col_indices()[k]];
            }
        }
        const auto& con = prob.constraints[i];
        if (con.sense == ConstraintSense::LE && activity > con.rhs + tol) return false;
        if (con.sense == ConstraintSense::GE && activity < con.rhs - tol) return false;
        if (con.sense == ConstraintSense::EQ && std::abs(activity - con.rhs) > tol) return false;
    }
    return true;
}

TEST(EdgeCaseTest, EmptyProblem) {
    ProblemBuilder builder("empty");
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(result.primal.empty());
}

TEST(EdgeCaseTest, SingleVariable) {
    ProblemBuilder builder("single_var");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 5.0, 1e-6);
    EXPECT_NEAR(result.objective_value, 5.0, 1e-4);
}

TEST(EdgeCaseTest, AllVariablesFixed) {
    ProblemBuilder builder("fixed");
    builder.add_variable(3.0, 3.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(7.0, 7.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 15.0, "c1");
    builder.set_objective({{0, 2.0}, {1, 3.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 3.0, 1e-6);
    EXPECT_NEAR(result.primal[1], 7.0, 1e-6);
    EXPECT_NEAR(result.objective_value, 27.0, 1e-4);
}

TEST(EdgeCaseTest, DegenerateDuplicateConstraint) {
    ProblemBuilder builder("degen");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c2_dup");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
    EXPECT_NEAR(result.objective_value, 5.0, 1e-4);
}

TEST(EdgeCaseTest, InfeasibleMILP) {
    ProblemBuilder builder("infeas_milp");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::EQ, 0.5, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundSolver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.status == ProblemStatus::INFEASIBLE ||
                result.status == ProblemStatus::UNKNOWN);
}

TEST(EdgeCaseTest, BinaryKnapsack) {
    ProblemBuilder builder("knapsack");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x3");
    builder.add_constraint({{0, 2.0}, {1, 3.0}, {2, 4.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.set_objective({{0, 10.0}, {1, 20.0}, {2, 30.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
    EXPECT_NEAR(result.objective_value, 30.0, 1e-4);
}

TEST(EdgeCaseTest, ZeroObjective) {
    ProblemBuilder builder("zero_obj");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.set_objective({{0, 0.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 0.0, 1e-4);
}

TEST(EdgeCaseTest, ManyConstraintsSingleVar) {
    ProblemBuilder builder("many_con");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x1");
    for (int i = 1; i <= 10; ++i) {
        builder.add_constraint({{0, 1.0}}, ConstraintSense::LE,
                               static_cast<double>(i * 10),
                               "c" + std::to_string(i));
    }
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 10.0, 1e-6);
    EXPECT_NEAR(result.objective_value, 10.0, 1e-4);
}

TEST(EdgeCaseTest, PresolveEmpty) {
    ProblemBuilder builder("presolve_empty");
    Problem prob = builder.build();

    Presolver presolver;
    auto result = presolver.presolve(prob);

    EXPECT_EQ(result.reduced_problem.variables.size(), 0u);
    EXPECT_EQ(result.reduced_problem.constraints.size(), 0u);
}

TEST(EdgeCaseTest, PresolveSingleVar) {
    ProblemBuilder builder("presolve_single");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    Presolver presolver;
    auto result = presolver.presolve(prob);

    SimplexSolver solver;
    auto sol = solver.solve(result.reduced_problem);

    EXPECT_EQ(sol.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(result.reduced_problem, sol.primal));
}

TEST(EdgeCaseTest, IntegerSingleVar) {
    ProblemBuilder builder("int_single");
    builder.add_variable(0.0, 5.0, VarType::INTEGER, "x1");
    builder.set_objective({{0, 3.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 5.0, 1e-6);
    EXPECT_NEAR(result.objective_value, 15.0, 1e-4);
}

TEST(EdgeCaseTest, QuadraticSingleVar) {
    ProblemBuilder builder("qp_single");
    builder.add_variable(-10.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 2.0, 1e-2);
    EXPECT_NEAR(result.objective_value, -2.0, 1e-2);
}
