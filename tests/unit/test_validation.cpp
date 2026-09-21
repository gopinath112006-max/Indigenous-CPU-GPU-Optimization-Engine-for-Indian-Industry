#include <gtest/gtest.h>
#include <validation/solution_verifier.hpp>
#include <validation/solve_report.hpp>
#include <validation/iis.hpp>
#include <validation/certificate.hpp>
#include <validation/diagnostics.hpp>
#include <model/problem.hpp>
#include <algorithm>
#include <limits>

using namespace hypernova::validation;
using namespace hypernova::model;

TEST(SolutionVerifierTest, FeasibleSolution) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {5.0, 5.0};
    sol.dual = {1.0};

    SolutionVerifier verifier;
    EXPECT_TRUE(verifier.verify_feasible(prob, sol));
}

TEST(SolutionVerifierTest, InfeasiblePrimal) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {10.0};

    SolutionVerifier verifier;
    EXPECT_FALSE(verifier.verify(prob, sol));
}

TEST(SolutionVerifierTest, InfeasibleDual) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {5.0};
    sol.dual = {100.0};

    SolutionVerifier verifier;
    auto result = verifier.verify_detailed(prob, sol);
    EXPECT_FALSE(result.optimal);
}

TEST(SolutionVerifierTest, IntegralityCheck) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {5.5};

    SolutionVerifier verifier;
    auto result = verifier.verify_detailed(prob, sol);
    EXPECT_FALSE(result.feasible);
    EXPECT_GT(result.integrality_violation, 0.0);
}

TEST(SolutionVerifierTest, IntegerFeasible) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {5.0};

    SolutionVerifier verifier;
    EXPECT_TRUE(verifier.verify_feasible(prob, sol));
}


TEST(SolutionVerifierTest, BoundViolation) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {10.0};

    SolutionVerifier verifier;
    auto result = verifier.verify_detailed(prob, sol);
    EXPECT_FALSE(result.feasible);
    EXPECT_GT(result.primal_infeasibility, 0.0);
}

TEST(SolutionVerifierTest, NaNInSolution) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {std::numeric_limits<double>::quiet_NaN()};

    SolutionVerifier verifier;
    auto result = verifier.verify_detailed(prob, sol);
    EXPECT_FALSE(result.feasible);
}

TEST(SolutionVerifierTest, ObjectiveDiscrepancy) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {5.0};
    sol.objective_value = 100.0; // Incorrect, should be 5.0

    SolutionVerifier verifier;
    auto result = verifier.verify_detailed(prob, sol);
    EXPECT_FALSE(result.optimal);
    EXPECT_GT(result.objective_discrepancy, 0.0);
}

TEST(SolutionVerifierTest, ObjectiveMatches) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 2.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    Solution sol;
    sol.status = ProblemStatus::OPTIMAL;
    sol.primal = {5.0};
    sol.objective_value = 10.0; // Correct

    SolutionVerifier verifier;
    auto result = verifier.verify_detailed(prob, sol);
    EXPECT_EQ(result.objective_discrepancy, 0.0);
}

TEST(SolveReportTest, JSONSerialization) {
    SolveReport report;
    report.problem_name = "test";
    report.status = ProblemStatus::OPTIMAL;
    report.objective_value = 100.0;
    report.num_variables = 10;
    report.num_constraints = 5;
    report.solve_time_ms = 123.45;

    nlohmann::json j = report.to_json();
    EXPECT_EQ(j["problem_name"], "test");
    EXPECT_EQ(j["status"], static_cast<int>(ProblemStatus::OPTIMAL));
    EXPECT_DOUBLE_EQ(j["objective_value"], 100.0);

    SolveReport report2 = SolveReport::from_json(j);
    EXPECT_EQ(report2.problem_name, "test");
    EXPECT_EQ(report2.status, ProblemStatus::OPTIMAL);
    EXPECT_DOUBLE_EQ(report2.objective_value, 100.0);
}

TEST(SolveReportTest, StringOutput) {
    SolveReport report;
    report.problem_name = "test";
    report.status = ProblemStatus::OPTIMAL;
    report.objective_value = 100.0;

    std::string str = report.to_string();
    EXPECT_NE(str.find("test"), std::string::npos);
    EXPECT_NE(str.find("100"), std::string::npos);
}

TEST(IISComputerTest, FeasibleModelHasNoIIS) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    IISComputer computer;
    IISResult result = computer.compute(prob);

    EXPECT_FALSE(result.found);
    EXPECT_TRUE(result.constraint_indices.empty());
    EXPECT_TRUE(result.bounds.empty());
}

TEST(IISComputerTest, TwoConstraintsConflict) {
    // x <= 3 and x >= 5: the IIS is exactly the two rows with finite bounds out.
    ProblemBuilder builder("test");
    builder.add_variable(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 3.0, "ub_con");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 5.0, "lb_con");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    IISComputer computer;
    IISResult result = computer.compute(prob);

    ASSERT_TRUE(result.found);
    ASSERT_EQ(result.constraint_indices.size(), 2u);
    EXPECT_NE(std::find(result.constraint_indices.begin(), result.constraint_indices.end(), 0u),
              result.constraint_indices.end());
    EXPECT_NE(std::find(result.constraint_indices.begin(), result.constraint_indices.end(), 1u),
              result.constraint_indices.end());
}

TEST(IISComputerTest, RedundantRowExcluded) {
    // Rows: x >= 6, x <= 4, x <= 3. The irreducibly infeasible part is {x >= 6, x <= 3};
    // the redundant x <= 4 (dropped during the filter) must not appear.
    ProblemBuilder builder("test");
    builder.add_variable(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 6.0, "ge6");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 4.0, "le4");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 3.0, "le3");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    IISComputer computer;
    IISResult result = computer.compute(prob);

    ASSERT_TRUE(result.found);
    EXPECT_EQ(result.constraint_indices.size(), 2u);
    EXPECT_NE(std::find(result.constraint_indices.begin(), result.constraint_indices.end(), 0u),
              result.constraint_indices.end());
    EXPECT_NE(std::find(result.constraint_indices.begin(), result.constraint_indices.end(), 2u),
              result.constraint_indices.end());
    EXPECT_EQ(std::find(result.constraint_indices.begin(), result.constraint_indices.end(), 1u),
              result.constraint_indices.end());
}

TEST(IISComputerTest, BoundIncluded) {
    // x in [0, 1] and x >= 2: IIS = {row, upper bound}.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 2.0, "ge2");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    IISComputer computer;
    IISResult result = computer.compute(prob);

    ASSERT_TRUE(result.found);
    ASSERT_EQ(result.constraint_indices.size(), 1u);
    EXPECT_EQ(result.constraint_indices[0], 0u);
    ASSERT_EQ(result.bounds.size(), 1u);
    EXPECT_EQ(result.bounds[0].variable, 0u);
    EXPECT_EQ(result.bounds[0].upper, true);
    EXPECT_GT(result.lp_solves, 0u);
}

TEST(IISComputerTest, StringOutput) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 2.0, "ge2");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    IISComputer computer;
    IISResult result = computer.compute(prob);
    std::string str = result.to_string(prob);

    ASSERT_TRUE(result.found);
    EXPECT_NE(str.find("ge2"), std::string::npos);
    EXPECT_NE(str.find("upper bound"), std::string::npos);
}

TEST(CertificateTest, FarkasCertifiesLEGEClash) {
    // x <= 3, x >= 5, x free.
    ProblemBuilder builder("test");
    builder.add_variable(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 3.0, "le3");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 5.0, "ge5");
    builder.set_objective({{0, 0.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::INFEASIBLE);
    ASSERT_TRUE(res.farkas.valid) << res.farkas.message;
    ASSERT_EQ(res.farkas.multipliers.size(), 2u);
    EXPECT_GE(res.farkas.multipliers[0], 0.0);
    EXPECT_LE(res.farkas.multipliers[1], 0.0);
    EXPECT_LT(res.farkas.farkas_value, 0.0);
}

TEST(CertificateTest, FarkasWithRowAndBound) {
    // x in [0, 1] and x >= 2: bound + GE row contradiction.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 2.0, "ge2");
    builder.set_objective({{0, 0.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::INFEASIBLE);
    ASSERT_TRUE(res.farkas.valid) << res.farkas.message;
    EXPECT_LT(res.farkas.farkas_value, 0.0);
}

TEST(CertificateTest, FeasibleProblemNoFarkas) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::OPTIMAL);
    EXPECT_FALSE(res.farkas.valid);
    EXPECT_FALSE(res.ray.valid);
}

TEST(CertificateTest, RayCertifiesUnboundedMin) {
    // min -x, x >= 0: unbounded downward in -(x) i.e. x can grow forever.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 0.0, "ge0");
    builder.set_objective({{0, -1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::UNBOUNDED);
    ASSERT_TRUE(res.ray.valid) << res.ray.message;
    ASSERT_EQ(res.ray.direction.size(), 1u);
    EXPECT_GT(res.ray.direction[0], 0.0);
    EXPECT_LT(res.ray.objective_direction, 0.0);
}

TEST(CertificateTest, NoRayForBoundedModel) {
    // max x, x <= 5: bounded, never UNBOUNDED.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::OPTIMAL);
    EXPECT_FALSE(res.ray.valid);
}

TEST(CertificateTest, RayCertifiesUnboundedMax) {
    // max x, x free: direction d = +1 improves.
    ProblemBuilder builder("test");
    builder.add_variable(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::UNBOUNDED);
    ASSERT_TRUE(res.ray.valid) << res.ray.message;
    EXPECT_GT(res.ray.objective_direction, 0.0);
}

TEST(CertificateTest, FarkasWithEqualityRows) {
    // x >= 0, EQ x = 5, LE x <= 2: impossible.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::EQ, 5.0, "eq5");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 2.0, "le2");
    builder.set_objective({{0, 0.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::INFEASIBLE);
    ASSERT_TRUE(res.farkas.valid) << res.farkas.message;
    EXPECT_LT(res.farkas.farkas_value, 0.0);
}

TEST(CertificateTest, FarkasFreeVariableClash) {
    // x free, EQ x = 1 and EQ x = 0: contradiction between two equalities.
    ProblemBuilder builder("test");
    builder.add_variable(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::EQ, 1.0, "eq1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::EQ, 0.0, "eq0");
    builder.set_objective({{0, 0.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::INFEASIBLE);
    ASSERT_TRUE(res.farkas.valid) << res.farkas.message;
    ASSERT_EQ(res.farkas.multipliers.size(), 2u);
    EXPECT_LT(res.farkas.farkas_value, 0.0);
}

TEST(CertificateTest, RayDecreasesFreeVariableToMinusInfinity) {
    // min x, x free, x <= 3: unbounded below in x.
    ProblemBuilder builder("test");
    builder.add_variable(-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 3.0, "le3");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    CertificateAnalyzer analyzer;
    CertificateResult res = analyzer.analyze(prob);

    EXPECT_EQ(res.status, ProblemStatus::UNBOUNDED);
    ASSERT_TRUE(res.ray.valid) << res.ray.message;
    ASSERT_EQ(res.ray.direction.size(), 1u);
    EXPECT_LT(res.ray.direction[0], 0.0);
    EXPECT_LT(res.ray.objective_direction, 0.0);
}

TEST(DiagnosticsTest, GoodQualitySmallLP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 2.0}}, ConstraintSense::LE, 3.0, "c1");
    builder.set_objective({{0, 3.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    DiagnosticsAnalyzer analyzer;
    NumericalDiagnostics d = analyzer.analyze(prob);

    EXPECT_EQ(d.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(d.num_variables, 2u);
    EXPECT_EQ(d.num_constraints, 1u);
    EXPECT_EQ(d.num_nonzeros, 2u);
    EXPECT_DOUBLE_EQ(d.max_coefficient, 2.0);
    EXPECT_DOUBLE_EQ(d.min_nonzero_coefficient, 1.0);
    EXPECT_DOUBLE_EQ(d.dynamic_range, 2.0);
    EXPECT_GT(d.condition_estimate, 0.0);
    EXPECT_LT(d.condition_estimate, 1e12);
    EXPECT_LT(d.primal_infeasibility, 1e-6);
    EXPECT_EQ(d.quality, "good");
    EXPECT_TRUE(d.warnings.empty());
}

TEST(DiagnosticsTest, IllConditionedMatrix) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 1.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 1.0 + 1e-7}}, ConstraintSense::LE, 1.0, "c2");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    DiagnosticsAnalyzer analyzer;
    NumericalDiagnostics d = analyzer.analyze(prob);

    EXPECT_TRUE(d.matrix_rank_deficient || d.condition_estimate >= 1e12);
    EXPECT_TRUE(d.quality == "warn" || d.quality == "poor");
    ASSERT_FALSE(d.warnings.empty());
    bool saw_cond = false;
    for (const auto& w : d.warnings) {
        if (w.find("ill-conditioned") != std::string::npos ||
            w.find("singular") != std::string::npos) saw_cond = true;
    }
    EXPECT_TRUE(saw_cond);
}

TEST(DiagnosticsTest, RankDeficientMatrix) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 1.0, "c1");
    builder.add_constraint({{0, 2.0}}, ConstraintSense::LE, 2.0, "c2");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    DiagnosticsAnalyzer analyzer;
    NumericalDiagnostics d = analyzer.analyze(prob);

    EXPECT_EQ(d.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(d.matrix_rank_deficient);
    EXPECT_DOUBLE_EQ(d.condition_estimate, 0.0);
    EXPECT_EQ(d.quality, "warn");
}

TEST(DiagnosticsTest, UnboundedProbeReportsCondition) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    DiagnosticsAnalyzer analyzer;
    NumericalDiagnostics d = analyzer.analyze(prob);

    EXPECT_EQ(d.status, ProblemStatus::UNBOUNDED);
    EXPECT_EQ(d.num_constraints, 0u);
    EXPECT_DOUBLE_EQ(d.condition_estimate, 0.0);
    EXPECT_EQ(d.quality, "good");
}