#include <gtest/gtest.h>
#include <model/problem.hpp>
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>
#include <lp/simplex.hpp>
#include <lp/interior_point.hpp>
#include <milp/branch_and_bound.hpp>
#include <qp/active_set.hpp>
#include <qp/interior_point_qp.hpp>
#include <validation/solution_verifier.hpp>

using namespace hypernova::model;
using namespace hypernova::lp;
using namespace hypernova::milp;
using namespace hypernova::qp;
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

TEST(IntegrationTest, SimplexFromBuilder) {
    ProblemBuilder builder("simplex_e2e");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 2.0}, {1, 1.0}}, ConstraintSense::LE, 14.0, "c2");
    builder.set_objective({{0, 3.0}, {1, 5.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
    EXPECT_NEAR(result.objective_value, 50.0, 1e-4);
}

TEST(IntegrationTest, IPMFromBuilder) {
    ProblemBuilder builder("ipm_e2e");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 2.0}, {1, 1.0}}, ConstraintSense::LE, 14.0, "c2");
    builder.set_objective({{0, 3.0}, {1, 5.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    InteriorPointSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
    EXPECT_NEAR(result.objective_value, 50.0, 1e-2);
}

TEST(IntegrationTest, MILPFromBuilder) {
    ProblemBuilder builder("milp_e2e");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 7.0, "c1");
    builder.set_objective({{0, 5.0}, {1, 4.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
    EXPECT_NEAR(result.objective_value, 35.0, 1e-4);
}

TEST(IntegrationTest, QPFromBuilder) {
    ProblemBuilder builder("qp_e2e");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 4.0, "c1");
    builder.set_objective({{0, -4.0}, {1, -5.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
}

TEST(IntegrationTest, MPSParserToSimplex) {
    std::string mps = R"(
NAME          MPS_E2E
ROWS
 N  OBJ
 L  C1
 L  C2
COLUMNS
    X1        OBJ        3.0       C1        1.0
    X1        C2         2.0
    X2        OBJ        5.0       C1        1.0
    X2        C2         1.0
RHS
    RHS1      C1        10.0      C2        14.0
OBJSENSE
 MAX
ENDATA
)";

    MPSParser parser;
    auto parse_result = parser.parse_string(mps);
    ASSERT_TRUE(parse_result.success);

    SimplexSolver solver;
    auto sol = solver.solve(parse_result.problem);

    EXPECT_EQ(sol.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(parse_result.problem, sol.primal));
    EXPECT_NEAR(sol.objective_value, 50.0, 1e-4);
}

TEST(IntegrationTest, LPParserToSimplex) {
    std::string lp = R"(
Maximize
 OBJ: 3 x1 + 5 x2
Subject To
 C1: 1 x1 + 1 x2 <= 10
 C2: 2 x1 + 1 x2 <= 14
Bounds
 0 <= x1
 0 <= x2
End
)";

    LPParser parser;
    auto parse_result = parser.parse_string(lp);
    ASSERT_TRUE(parse_result.success);

    SimplexSolver solver;
    auto sol = solver.solve(parse_result.problem);

    EXPECT_EQ(sol.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(parse_result.problem, sol.primal));
    EXPECT_NEAR(sol.objective_value, 50.0, 1e-4);
}

TEST(IntegrationTest, MPSParserToMILP) {
    std::string mps = R"(
NAME          MILP_E2E
ROWS
 N  OBJ
 L  C1
COLUMNS
    X1        OBJ        5.0       C1        1.0
    X2        OBJ        4.0       C1        1.0
RHS
    RHS1      C1        7.0
BOUNDS
 BV BND       X1
 BV BND       X2
OBJSENSE
 MAX
ENDATA
)";

    MPSParser parser;
    auto parse_result = parser.parse_string(mps);
    ASSERT_TRUE(parse_result.success);

    BranchAndBoundSolver solver;
    auto sol = solver.solve(parse_result.problem);

    EXPECT_EQ(sol.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(parse_result.problem, sol.primal));
    EXPECT_NEAR(sol.objective_value, 9.0, 1e-4);
}

TEST(IntegrationTest, QPWithEqualityConstraints) {
    ProblemBuilder builder("qp_eq_e2e");
    builder.add_variable(-10.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(-10.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::EQ, 2.0, "c1");
    builder.set_objective({{0, -2.0}, {1, -3.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
}

TEST(IntegrationTest, LargerLP) {
    ProblemBuilder builder("larger_lp");
    for (int i = 0; i < 20; ++i) {
        builder.add_variable(0.0, 100.0, VarType::CONTINUOUS,
                             "x" + std::to_string(i));
    }
    for (int i = 0; i < 10; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < 20; ++j) {
            double c = static_cast<double>((i + 1) * (j + 1));
            coeffs.push_back({static_cast<std::size_t>(j), c});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE,
                               100.0 * (i + 1), "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int j = 0; j < 20; ++j) {
        obj.push_back({static_cast<std::size_t>(j), static_cast<double>(j + 1)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(prob, result.primal));
}

TEST(IntegrationTest, InfeasibleLP) {
    ProblemBuilder builder("infeas_e2e");
    builder.add_variable(0.0, 1.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 5.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INFEASIBLE);
}

TEST(IntegrationTest, UnboundedLP) {
    ProblemBuilder builder("unbounded_e2e");
    builder.add_variable(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(),
                         VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::UNBOUNDED);
}

TEST(IntegrationTest, LPParserToQP) {
    std::string lp = R"(
Minimize
 OBJ: -4 x1 - 5 x2 + 1 x1 * x1 + 1 x2 * x2
Subject To
 C1: 1 x1 + 1 x2 <= 4
Bounds
 0 <= x1
 0 <= x2
Quadratic
 x1 * x1 1
 x2 * x2 1
End
)";

    LPParser parser;
    auto parse_result = parser.parse_string(lp);
    ASSERT_TRUE(parse_result.success);

    ActiveSetQPSolver solver;
    auto sol = solver.solve(parse_result.problem);

    EXPECT_EQ(sol.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(check_primal_feasible(parse_result.problem, sol.primal));
}
