#include <gtest/gtest.h>
#include <model/problem.hpp>
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>

using namespace hypernova::model;

TEST(ProblemTest, Builder) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 5.0, VarType::INTEGER, "x2");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x3");

    builder.add_constraint({{0, 1.0}, {1, 2.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 3.0}, {2, 1.0}}, ConstraintSense::GE, 5.0, "c2");

    builder.set_objective({{0, 2.0}, {1, 3.0}, {2, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    EXPECT_EQ(prob.name, "test");
    EXPECT_EQ(prob.variables.size(), 3);
    EXPECT_EQ(prob.constraints.size(), 2);
    EXPECT_EQ(prob.num_continuous_vars(), 1);
    EXPECT_EQ(prob.num_integer_vars(), 2);
    EXPECT_EQ(prob.num_binary_vars(), 1);
    EXPECT_TRUE(prob.is_milp());
}

TEST(ProblemTest, Getters) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 5.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 2.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 2.0}, {1, 3.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    auto lb = prob.get_lb();
    auto ub = prob.get_ub();
    auto obj = prob.get_obj();
    auto rhs = prob.get_rhs();
    auto senses = prob.get_senses();

    EXPECT_EQ(lb.size(), 2);
    EXPECT_EQ(ub.size(), 2);
    EXPECT_EQ(obj.size(), 2);
    EXPECT_EQ(rhs.size(), 1);
    EXPECT_EQ(senses.size(), 1);
}

TEST(ProblemTest, BasisFromSolution) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 2.0}, {1, 3.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {5.0, 5.0};
    sol.basis_status = {0, 0, 0, 0};

    Basis basis = Basis::from_solution(sol, prob);
    EXPECT_EQ(basis.var_status.size(), 2);
    EXPECT_EQ(basis.con_status.size(), 1);
}

TEST(MPSParserTest, SimpleLP) {
    std::string mps_content = R"(
NAME          TEST
ROWS
 N  OBJ
 L  C1
 G  C2
COLUMNS
    X1        OBJ        1.0       C1        1.0
    X1        C2         2.0
    X2        OBJ        2.0       C1        1.0
RHS
    RHS1      C1        10.0      C2        5.0
BOUNDS
 UP BND       X1        10.0
OBJSENSE
 MAX
ENDATA
)";

    MPSParser parser;
    auto result = parser.parse_string(mps_content);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.problem.name, "TEST");
    EXPECT_EQ(result.problem.variables.size(), 2);
    EXPECT_EQ(result.problem.constraints.size(), 2);
    EXPECT_EQ(result.problem.obj_sense, ObjectiveSense::MAXIMIZE);
}

TEST(MPSParserTest, MILPWithBinary) {
    std::string mps_content = R"(
NAME          MILP_TEST
ROWS
 N  OBJ
 L  C1
COLUMNS
    X1        OBJ        1.0       C1        1.0
    X2        OBJ        2.0       C1        1.0
RHS
    RHS1      C1        10.0
BOUNDS
 BV BND       X2
ENDATA
)";

    MPSParser parser;
    auto result = parser.parse_string(mps_content);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.problem.num_binary_vars(), 1);
    EXPECT_TRUE(result.problem.is_milp());
}

TEST(MPSParserTest, QPWithQuadraticObjective) {
    std::string mps_content = R"(
NAME          QP_TEST
ROWS
 N  OBJ
 L  C1
COLUMNS
    X1        OBJ        1.0       C1        1.0
    X2        OBJ        2.0       C1        1.0
RHS
    RHS1      C1        10.0
QUADOBJ
    X1        X1        2.0
    X1        X2        1.0
ENDATA
)";

    MPSParser parser;
    auto result = parser.parse_string(mps_content);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.problem.quadratic_terms.size(), 2);
    EXPECT_TRUE(result.problem.is_qp());
}

TEST(LPParserTest, SimpleLP) {
    std::string lp_content = R"(
Maximize
 OBJ: 1 x1 + 2 x2
Subject To
 C1: 1 x1 + 1 x2 <= 10
 C2: 2 x1 + 0 x2 >= 5
Bounds
 0 <= x1 <= 10
 0 <= x2
End
)";

    LPParser parser;
    auto result = parser.parse_string(lp_content);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.problem.variables.size(), 2);
    EXPECT_EQ(result.problem.constraints.size(), 2);
    EXPECT_EQ(result.problem.obj_sense, ObjectiveSense::MAXIMIZE);
}

TEST(LPParserTest, MILPWithGeneral) {
    std::string lp_content = R"(
Minimize
 OBJ: 1 x1 + 2 x2
Subject To
 C1: 1 x1 + 1 x2 <= 10
Bounds
 0 <= x1 <= 10
 0 <= x2
General
 x1
End
)";

    LPParser parser;
    auto result = parser.parse_string(lp_content);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.problem.num_integer_vars(), 1);
    EXPECT_TRUE(result.problem.is_milp());
}

TEST(LPParserTest, QP) {
    std::string lp_content = R"(
Minimize
 OBJ: 1 x1 + 2 x2
Subject To
 C1: 1 x1 + 1 x2 <= 10
Bounds
 0 <= x1 <= 10
 0 <= x2
Quadratic
 x1 * x1 2
 x1 * x2 1
End
)";

    LPParser parser;
    auto result = parser.parse_string(lp_content);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.problem.quadratic_terms.size(), 2);
    EXPECT_TRUE(result.problem.is_qp());

    // Trailing coefficients ("x * y value", the write_lp format) must be
    // parsed, not dropped: `x1 * x1 2` is 2.0*x1^2 and `x1 * x2 1` is 1.0*x1*x2.
    double q11 = 0.0, q12 = 0.0;
    for (const auto& t : result.problem.quadratic_terms) {
        if (t.row == 0 && t.col == 0) q11 = t.coeff;
        if (t.row == 0 && t.col == 1) q12 = t.coeff;
    }
    EXPECT_DOUBLE_EQ(q11, 2.0);
    EXPECT_DOUBLE_EQ(q12, 1.0);
}

TEST(LPParserTest, QPLeadingCoefficient) {
    // Leading coefficients ("value x * y") are equally accepted.
    std::string lp_content = R"(
Minimize
 OBJ: 1 x1 + 2 x2
Subject To
 C1: 1 x1 + 1 x2 <= 10
Bounds
 0 <= x1 <= 10
 0 <= x2
Quadratic
 2 x1 * x1
 3 x2 * x2
End
)";

    LPParser parser;
    auto result = parser.parse_string(lp_content);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.problem.quadratic_terms.size(), 2);
    EXPECT_TRUE(result.problem.is_qp());

    auto coeff_of = [&](std::size_t j) -> double {
        for (const auto& t : result.problem.quadratic_terms) {
            if (t.row == j && t.col == j) return t.coeff;
        }
        return 0.0;
    };
    EXPECT_DOUBLE_EQ(coeff_of(0), 2.0);
    EXPECT_DOUBLE_EQ(coeff_of(1), 3.0);
}