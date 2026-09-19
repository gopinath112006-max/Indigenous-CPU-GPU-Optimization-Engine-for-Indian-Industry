#include <gtest/gtest.h>
#include <model/problem.hpp>
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>
#include <model/problem_json.hpp>

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

TEST(MPSParserTest, IntegerMarkersDeclareIntegerVariables) {
    // Regression: MARKER 'INTORG'/'INTEND' records delimit the integer-variable
    // block. Columns inside must become integer; the marker records themselves
    // must never be turned into variables.
    std::string mps_content = R"(
NAME          MARKER_TEST
ROWS
 N  OBJ
 L  C1
COLUMNS
    MARK0000  'MARKER'                 'INTORG'
    X1        OBJ        1.0       C1        1.0
    X2        OBJ        2.0       C1        1.0
    MARK0001  'MARKER'                 'INTEND'
    X3        OBJ        3.0       C1        1.0
RHS
    RHS1      C1        10.0
BOUNDS
 UP BND       X1        5.0
 UP BND       X2        5.0
 UP BND       X3        5.0
ENDATA
)";

    MPSParser parser;
    auto result = parser.parse_string(mps_content);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.problem.variables.size(), 3);  // MARKER records excluded
    EXPECT_EQ(result.problem.num_integer_vars(), 2);
    EXPECT_EQ(result.problem.num_continuous_vars(), 1);
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
    // parsed, not dropped: `x1 * x1 2` is 2.0*x1^2 (stored Q11 = 4.0) and `x1 * x2 1` is 1.0*x1*x2.
    double q11 = 0.0, q12 = 0.0;
    for (const auto& t : result.problem.quadratic_terms) {
        if (t.row == 0 && t.col == 0) q11 = t.coeff;
        if (t.row == 0 && t.col == 1) q12 = t.coeff;
    }
    EXPECT_DOUBLE_EQ(q11, 4.0);
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
    EXPECT_DOUBLE_EQ(coeff_of(0), 4.0);
    EXPECT_DOUBLE_EQ(coeff_of(1), 6.0);
}

TEST(JSONFidelityTest, RoundTripStructuralEquality) {
    std::string mps_content = R"(
NAME          JSON_TEST
ROWS
 N  OBJ
 L  C1
 G  C2
 E  C3
COLUMNS
    MARK0000  'MARKER'                 'INTORG'
    X1        OBJ        1.0       C1        1.0       C2        2.0
    MARK0001  'MARKER'                 'INTEND'
    X2        OBJ        2.5       C1        3.0       C3        1.5
RHS
    RHS1      C1        10.0      C2        5.0       C3        7.0
BOUNDS
 UP BND       X1        10.0
 BV BND       X2
QUADOBJ
    X1        X1        2.0
    X1        X2        0.5
ENDATA
)";

    MPSParser parser;
    auto result = parser.parse_string(mps_content);
    ASSERT_TRUE(result.success);

    Problem orig_prob = result.problem;
    orig_prob.obj_offset = 12.5;

    // Add SOS constraint to test full schema fidelity
    SOS sos;
    sos.name = "sos_group_1";
    sos.type = SOS::Type::SOS1;
    sos.variable_indices = {0, 1};
    sos.weights = {1.0, 2.0};
    orig_prob.sos_constraints.push_back(sos);

    // Serialize MPS -> Problem -> JSON
    nlohmann::json json_doc = problem_to_json(orig_prob);

    // Verify key JSON fields are populated
    EXPECT_EQ(json_doc["name"], "JSON_TEST");
    EXPECT_EQ(json_doc["variables"].size(), 2);
    EXPECT_EQ(json_doc["constraints"].size(), 3);
    EXPECT_EQ(json_doc["quadratic_terms"].size(), 2);
    EXPECT_EQ(json_doc["sos_constraints"].size(), 1);

    // Deserialize JSON -> Problem
    Problem restored_prob = problem_from_json(json_doc);

    // Verify structural equality
    EXPECT_EQ(restored_prob.name, orig_prob.name);
    EXPECT_EQ(restored_prob.obj_sense, orig_prob.obj_sense);
    EXPECT_DOUBLE_EQ(restored_prob.obj_offset, orig_prob.obj_offset);

    // Variables
    ASSERT_EQ(restored_prob.variables.size(), orig_prob.variables.size());
    for (size_t i = 0; i < orig_prob.variables.size(); ++i) {
        EXPECT_EQ(restored_prob.variables[i].name, orig_prob.variables[i].name);
        EXPECT_EQ(restored_prob.variables[i].type, orig_prob.variables[i].type);
        EXPECT_DOUBLE_EQ(restored_prob.variables[i].lower_bound, orig_prob.variables[i].lower_bound);
        EXPECT_DOUBLE_EQ(restored_prob.variables[i].upper_bound, orig_prob.variables[i].upper_bound);
        EXPECT_DOUBLE_EQ(restored_prob.variables[i].objective_coeff, orig_prob.variables[i].objective_coeff);
    }

    // Constraints
    ASSERT_EQ(restored_prob.constraints.size(), orig_prob.constraints.size());
    for (size_t i = 0; i < orig_prob.constraints.size(); ++i) {
        EXPECT_EQ(restored_prob.constraints[i].name, orig_prob.constraints[i].name);
        EXPECT_EQ(restored_prob.constraints[i].sense, orig_prob.constraints[i].sense);
        EXPECT_DOUBLE_EQ(restored_prob.constraints[i].rhs, orig_prob.constraints[i].rhs);
    }

    // Matrix values
    const auto& A_orig = orig_prob.constraint_matrix;
    const auto& A_rest = restored_prob.constraint_matrix;
    EXPECT_EQ(A_rest.rows(), A_orig.rows());
    EXPECT_EQ(A_rest.cols(), A_orig.cols());
    EXPECT_EQ(A_rest.row_ptr(), A_orig.row_ptr());
    EXPECT_EQ(A_rest.col_indices(), A_orig.col_indices());
    EXPECT_EQ(A_rest.values(), A_orig.values());

    // Quadratic terms
    ASSERT_EQ(restored_prob.quadratic_terms.size(), orig_prob.quadratic_terms.size());
    for (size_t i = 0; i < orig_prob.quadratic_terms.size(); ++i) {
        EXPECT_EQ(restored_prob.quadratic_terms[i].row, orig_prob.quadratic_terms[i].row);
        EXPECT_EQ(restored_prob.quadratic_terms[i].col, orig_prob.quadratic_terms[i].col);
        EXPECT_DOUBLE_EQ(restored_prob.quadratic_terms[i].coeff, orig_prob.quadratic_terms[i].coeff);
    }

    // SOS
    ASSERT_EQ(restored_prob.sos_constraints.size(), orig_prob.sos_constraints.size());
    EXPECT_EQ(restored_prob.sos_constraints[0].name, orig_prob.sos_constraints[0].name);
    EXPECT_EQ(restored_prob.sos_constraints[0].type, orig_prob.sos_constraints[0].type);
    EXPECT_EQ(restored_prob.sos_constraints[0].variable_indices, orig_prob.sos_constraints[0].variable_indices);
    EXPECT_EQ(restored_prob.sos_constraints[0].weights, orig_prob.sos_constraints[0].weights);
}