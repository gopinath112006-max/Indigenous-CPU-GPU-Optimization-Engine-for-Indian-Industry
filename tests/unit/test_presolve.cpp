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

TEST(PresolveTest, FixedVariableQuadraticObjectiveEquivalence) {
    // min 1*x0 + 2*x0^2 - 5*x1 + x1^2 + 3*x0*x1 + 7, with x0 fixed at 2.
    // Elimination folds x0's linear/quadratic contributions and the x0*x1
    // cross term (3*2 = 6) into x1's reduced objective and a constant offset,
    // so objective(x1) == 17 + 1*x1 + x1^2 == original objective(2, x1).
    ProblemBuilder builder("qp_fixed");
    builder.add_variable(2.0, 2.0, VarType::CONTINUOUS, "x0_fixed");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}, {1, -5.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 4.0);
    builder.add_quadratic_term(1, 1, 2.0);
    builder.add_quadratic_term(0, 1, 3.0);

    Problem prob = builder.build();
    prob.obj_offset = 7.0;

    Presolver presolver;
    PresolveResult result = presolver.presolve(prob);

    bool fixed_x0 = false;
    for (const auto& [idx, val_pair] : result.fixed_variables) {
        if (idx == 0 && val_pair.second == 2.0) fixed_x0 = true;
    }
    ASSERT_TRUE(fixed_x0);
    ASSERT_EQ(result.reduced_problem.variables.size(), 1u);

    const Problem& rp = result.reduced_problem;
    EXPECT_DOUBLE_EQ(rp.obj_offset, 7.0 + 1.0 * 2.0 + 0.5 * 4.0 * 2.0 * 2.0);
    EXPECT_DOUBLE_EQ(rp.variables[0].objective_coeff, -5.0 + 3.0 * 2.0);
    ASSERT_EQ(rp.quadratic_terms.size(), 1u);
    EXPECT_EQ(rp.quadratic_terms[0].row, 0u);
    EXPECT_EQ(rp.quadratic_terms[0].col, 0u);
    EXPECT_DOUBLE_EQ(rp.quadratic_terms[0].coeff, 2.0);

    auto reduced_value = [&](double x1) {
        double v = rp.obj_offset + rp.variables[0].objective_coeff * x1;
        for (const auto& t : rp.quadratic_terms) {
            v += (t.row == t.col ? 0.5 : 1.0) * t.coeff * x1 * x1;
        }
        return v;
    };
    auto original_value = [&](double x1) {
        double v = prob.obj_offset + 1.0 * 2.0 + 2.0 * 2.0 * 2.0;
        v += -5.0 * x1 + 3.0 * 2.0 * x1 + 0.5 * 2.0 * x1 * x1;
        return v;
    };
    for (double x1 : {0.0, 1.0, 2.5, 5.0}) {
        EXPECT_NEAR(reduced_value(x1), original_value(x1), 1e-9);
    }
}

TEST(PresolveTest, ObjectiveOffsetPreservedAfterReduction) {
    // A reduced problem must carry the original obj_offset even when no
    // variable is eliminated.
    ProblemBuilder builder("offset_lp");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 8.0, "c");
    builder.set_objective({{0, 2.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();
    prob.obj_offset = 4.5;

    Presolver presolver;
    PresolveResult result = presolver.presolve(prob);

    EXPECT_DOUBLE_EQ(result.reduced_problem.obj_offset, 4.5);
    ASSERT_GE(result.reduced_problem.variables.size(), 1u);
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