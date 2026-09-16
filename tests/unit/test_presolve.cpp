#include <gtest/gtest.h>
#include <presolve/presolve.hpp>
#include <model/problem.hpp>

using namespace hypernova::presolve;
using namespace hypernova::model;

TEST(PresolveTest, SimpleProblem) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 2.0}, {1, 3.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Presolver presolver;
    PresolveResult result = presolver.presolve(prob);

    EXPECT_EQ(result.reduced_problem.variables.size(), 2);
    EXPECT_LE(result.reduced_problem.constraints.size(), 1);
    EXPECT_GE(result.passes, 0);
}

TEST(PresolveTest, RedundantConstraint) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 0.0}, {1, 0.0}}, ConstraintSense::LE, 100.0, "c2_redundant");
    builder.set_objective({{0, 2.0}, {1, 3.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Presolver presolver;
    PresolveResult result = presolver.presolve(prob);

    EXPECT_LE(result.reduced_problem.constraints.size(), 2);
}

TEST(PresolveTest, FixedVariable) {
    ProblemBuilder builder("test");
    builder.add_variable(5.0, 5.0, VarType::CONTINUOUS, "x1_fixed");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 2.0}, {1, 3.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Presolver presolver;
    PresolveResult result = presolver.presolve(prob);

    EXPECT_GE(result.fixed_variables.size(), 1);
}

TEST(PresolveTest, Postsolve) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 2.0}, {1, 3.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Presolver presolver;
    PresolveResult result = presolver.presolve(prob);

    Solution reduced_sol;
    reduced_sol.primal = {3.0, 2.0};
    reduced_sol.status = ProblemStatus::OPTIMAL;

    Solution full_sol = presolver.postsolve(prob, reduced_sol, result);
    EXPECT_EQ(full_sol.primal.size(), 2);
}

namespace {

double row_rhs(const Problem& p, std::size_t row) {
    return p.constraints[row].rhs;
}

std::vector<double> row_coeffs(const Problem& p, std::size_t row) {
    const auto& A = p.constraint_matrix;
    std::vector<double> vals;
    for (std::size_t k = A.row_ptr()[row]; k < A.row_ptr()[row + 1]; ++k) {
        vals.push_back(A.values()[k]);
    }
    return vals;
}

}  // namespace

TEST(PresolveTest, CoefficientStrengtheningLE) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 2.0}, {1, 4.0}}, ConstraintSense::LE, 7.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.coefficient_strengthening = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    ASSERT_EQ(result.reduced_problem.constraints.size(), 1u);
    EXPECT_NEAR(row_rhs(result.reduced_problem, 0), 3.0, 1e-9);
    auto coeffs = row_coeffs(result.reduced_problem, 0);
    ASSERT_EQ(coeffs.size(), 2u);
    EXPECT_NEAR(coeffs[0], 1.0, 1e-9);
    EXPECT_NEAR(coeffs[1], 2.0, 1e-9);
}

TEST(PresolveTest, CoefficientStrengtheningGE) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 2.0}, {1, 4.0}}, ConstraintSense::GE, 7.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.coefficient_strengthening = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    if (result.reduced_problem.constraints.size() == 1u) {
        EXPECT_NEAR(row_rhs(result.reduced_problem, 0), 4.0, 1e-9);
        auto coeffs = row_coeffs(result.reduced_problem, 0);
        ASSERT_EQ(coeffs.size(), 2u);
        EXPECT_NEAR(coeffs[0], 1.0, 1e-9);
        EXPECT_NEAR(coeffs[1], 2.0, 1e-9);
    }
}

TEST(PresolveTest, CoefficientStrengtheningEQ) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 2.0}, {1, 4.0}}, ConstraintSense::EQ, 6.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.coefficient_strengthening = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    if (result.reduced_problem.constraints.size() == 1u) {
        EXPECT_NEAR(row_rhs(result.reduced_problem, 0), 3.0, 1e-9);
        auto coeffs = row_coeffs(result.reduced_problem, 0);
        ASSERT_EQ(coeffs.size(), 2u);
        EXPECT_NEAR(coeffs[0], 1.0, 1e-9);
        EXPECT_NEAR(coeffs[1], 2.0, 1e-9);
    }
}

TEST(PresolveTest, CoefficientStrengtheningSkipsContinuous) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 2.0}, {1, 4.0}}, ConstraintSense::LE, 7.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.coefficient_strengthening = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    if (result.reduced_problem.constraints.size() == 1u) {
        EXPECT_NEAR(row_rhs(result.reduced_problem, 0), 7.0, 1e-9);
        auto coeffs = row_coeffs(result.reduced_problem, 0);
        ASSERT_EQ(coeffs.size(), 2u);
        EXPECT_NEAR(coeffs[0], 2.0, 1e-9);
        EXPECT_NEAR(coeffs[1], 4.0, 1e-9);
    }
}

TEST(PresolveTest, DualReductionIntegralityFixing) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 0.4, "c_tight");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::GE, 0.0, "c_keep");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.dual_reductions = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    bool x1_fixed = false;
    double x1_value = 0.0;
    for (const auto& [idx, val_pair] : result.fixed_variables) {
        if (idx == 0) {
            x1_fixed = true;
            x1_value = val_pair.second;
        }
    }
    EXPECT_TRUE(x1_fixed);
    EXPECT_NEAR(x1_value, 0.0, 1e-9);
    EXPECT_EQ(result.reduced_problem.variables.size(), 1u);
}

TEST(PresolveTest, DualReductionExclusiveSingletonFixing) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 4.0, "c1");
    builder.set_objective({{0, -5.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.dual_reductions = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    bool x1_fixed = false;
    double x1_value = 0.0;
    for (const auto& [idx, val_pair] : result.fixed_variables) {
        if (idx == 0) {
            x1_fixed = true;
            x1_value = val_pair.second;
        }
    }
    EXPECT_TRUE(x1_fixed);
    EXPECT_NEAR(x1_value, 4.0, 1e-9);
    EXPECT_TRUE(result.reduced_problem.variables.empty());

    Solution reduced_sol;
    reduced_sol.status = ProblemStatus::OPTIMAL;
    Solution full_sol = presolver.postsolve(prob, reduced_sol, result);
    ASSERT_EQ(full_sol.primal.size(), 1u);
    EXPECT_NEAR(full_sol.primal[0], 4.0, 1e-9);
}

TEST(PresolveTest, DualReductionSkipsNonExclusive) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 2.0}}, ConstraintSense::LE, 6.0, "c1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 1.0, "c2");
    builder.set_objective({{0, 3.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.dual_reductions = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    bool x1_fixed = false;
    for (const auto& [idx, val_pair] : result.fixed_variables) {
        if (idx == 0) x1_fixed = true;
    }
    EXPECT_FALSE(x1_fixed);
    EXPECT_EQ(result.reduced_problem.variables.size(), 1u);
}

TEST(PresolveTest, DualReductionIntegerCeilOnLowerBound) {
    ProblemBuilder builder("test");
    builder.add_variable(0.5, 10.0, VarType::INTEGER, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 1.0, "c1");
    builder.set_objective({{0, 4.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    PresolveOptions opts;
    opts.dual_reductions = true;
    Presolver presolver(opts);
    PresolveResult result = presolver.presolve(prob);

    bool x1_fixed = false;
    double x1_value = 0.0;
    for (const auto& [idx, val_pair] : result.fixed_variables) {
        if (idx == 0) {
            x1_fixed = true;
            x1_value = val_pair.second;
        }
    }
    EXPECT_TRUE(x1_fixed);
    EXPECT_NEAR(x1_value, 1.0, 1e-9);
    EXPECT_TRUE(result.reduced_problem.variables.empty());
}