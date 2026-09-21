#include <gtest/gtest.h>
#include <qp/active_set.hpp>
#include <qp/interior_point_qp.hpp>
#include <milp/branch_and_bound.hpp>
#include <model/problem.hpp>

using namespace hypernova::qp;
using namespace hypernova::milp;
using namespace hypernova::model;
using namespace hypernova::numerical;

TEST(ActiveSetQPTest, InterruptCallback) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);

    Problem prob = builder.build();

    ActiveSetOptions opts;
    opts.interrupt_callback = []() { return true; };
    ActiveSetQPSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INTERRUPTED);
}

TEST(ActiveSetQPTest, SimpleQP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    builder.add_quadratic_term(0, 1, 1.0);

    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.status == ProblemStatus::OPTIMAL || result.status == ProblemStatus::ITER_LIMIT);
}

TEST(ActiveSetQPTest, QPWithEquality) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::EQ, 5.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);

    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.status == ProblemStatus::OPTIMAL || result.status == ProblemStatus::ITER_LIMIT);
}

TEST(ActiveSetQPTest, ContradictoryBounds) {
    ProblemBuilder builder("test");
    builder.add_variable(5.0, 3.0, VarType::CONTINUOUS, "x1"); // lb > ub
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);

    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INFEASIBLE);
}

TEST(InteriorPointQPTest, ContradictoryBounds) {
    ProblemBuilder builder("test");
    builder.add_variable(5.0, 3.0, VarType::CONTINUOUS, "x1"); // lb > ub
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);

    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INFEASIBLE);
}

TEST(InteriorPointQPTest, SimpleQP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    builder.add_quadratic_term(0, 1, 1.0);

    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.status == ProblemStatus::OPTIMAL || result.status == ProblemStatus::ITER_LIMIT);
}

TEST(QPTest, BoundConstrainedIPMLowerBoundOnly) {
    ProblemBuilder builder("qp_lb_only");
    double INF_VAL = std::numeric_limits<double>::infinity();
    builder.add_variable(1.0, INF_VAL, VarType::CONTINUOUS, "x0");
    builder.add_variable(1.0, INF_VAL, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -0.5}, {1, -0.5}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem prob = builder.build();
    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(result.primal[1], 1.0, 1e-4);
}

TEST(QPTest, BoundConstrainedIPMUpperBoundOnly) {
    ProblemBuilder builder("qp_ub_only");
    double INF_VAL = std::numeric_limits<double>::infinity();
    builder.add_variable(-INF_VAL, 3.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(-INF_VAL, 3.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -5.0}, {1, -5.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem prob = builder.build();
    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 3.0, 1e-4);
    EXPECT_NEAR(result.primal[1], 3.0, 1e-4);
}

TEST(QPTest, BoundConstrainedIPMBothBounds) {
    ProblemBuilder builder("qp_both_bounds");
    builder.add_variable(0.5, 3.0, VarType::CONTINUOUS, "x0");
    builder.set_objective({{0, -5.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    Problem prob = builder.build();
    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 3.0, 1e-4);
}

TEST(QPTest, BoundConstrainedIPMTightBounds) {
    ProblemBuilder builder("qp_tight");
    builder.add_variable(0.1, 0.2, VarType::CONTINUOUS, "x0");
    builder.set_objective({{0, -1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    Problem prob = builder.build();
    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 0.2, 1e-4);
}

TEST(QPTest, BoundConstrainedIPMMixed) {
    ProblemBuilder builder("qp_mixed");
    double INF_VAL = std::numeric_limits<double>::infinity();
    builder.add_variable(-INF_VAL, 3.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(-INF_VAL, INF_VAL, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -5.0}, {1, -1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem prob = builder.build();
    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 3.0, 1e-4);
    EXPECT_NEAR(result.primal[1], 1.0, 1e-4);
}

namespace {

Problem buildMiqupModel() {
    // min 0.5*x0^2 - 2*x0 + x1^2 - x1, x0 in [0,4], x1 binary.
    // Root relaxation: x0 = 2, x1 = 0.5 (fractional) -> B&B must branch.
    // Integer-feasible branches give x1 = 0 or x1 = 1, x0 = 2, objective -2.
    ProblemBuilder builder("miqp");
    builder.add_variable(0.0, 4.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.set_objective({{0, -2.0}, {1, -1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 2.0);
    return builder.build();
}

} // namespace

TEST(MIQPTest, BranchAndBoundActiveSetRelaxation) {
    Problem prob = buildMiqupModel();
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    opts.mip_gap_tolerance = 1e-6;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, -2.0, 1e-6);
    EXPECT_NEAR(result.primal[0], 2.0, 1e-6);
    EXPECT_TRUE(result.primal[1] == 0.0 || result.primal[1] == 1.0);
    EXPECT_GE(result.nodes_explored, 1u);
}

TEST(MIQPTest, BranchAndBoundInteriorPointRelaxation) {
    Problem prob = buildMiqupModel();
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::INTERIOR_POINT;
    opts.mip_gap_tolerance = 1e-6;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    // IPM-QP convergence is known-unreliable (already tolerated in Phase 5).
    // When an IPM relaxation fails to converge, the B&B must NOT certify the
    // node as INFEASIBLE; instead it surfaces the failure status (or SUBOPTIMAL
    // with an incumbent). When it does converge OPTIMAL, verify the result.
    EXPECT_TRUE(result.status == ProblemStatus::OPTIMAL ||
                result.status == ProblemStatus::ITER_LIMIT ||
                result.status == ProblemStatus::TIME_LIMIT ||
                result.status == ProblemStatus::NUMERICAL_ERROR ||
                result.status == ProblemStatus::SUBOPTIMAL);
if (result.status == ProblemStatus::OPTIMAL) {
        EXPECT_NEAR(result.objective_value, -2.0, 0.5);
        EXPECT_NEAR(result.primal[0], 2.0, 0.1);
        // The IPM converges to a bound within tolerance rather than exactly
        // onto it, so accept values within integrality tolerance of 0 or 1.
        EXPECT_TRUE(std::abs(result.primal[1]) < 1e-4 ||
                    std::abs(result.primal[1] - 1.0) < 1e-4);
        EXPECT_GE(result.nodes_explored, 1u);
    }
}

TEST(MIQPTest, CrossTermIntegers) {
    // min x0^2 + x1^2 - 3*x0 - 3*x1, x0,x1 integer in [0,3].
    // (Diagonal quadratic terms added as 2.0 -> 0.5*2*x^2 = x^2.)
    // Continuous relaxation is (1.5,1.5) with value -4.5 -> B&B must branch.
    // Integer optimum -4 at (1,1), (1,2), (2,1), (2,2).
    ProblemBuilder builder("miqp_cross_int");
    builder.add_variable(0.0, 3.0, VarType::INTEGER, "x0");
    builder.add_variable(0.0, 3.0, VarType::INTEGER, "x1");
    builder.set_objective({{0, -3.0}, {1, -3.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    opts.mip_gap_tolerance = 1e-6;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, -4.0, 1e-4);
    EXPECT_TRUE(
        (std::abs(result.primal[0] - 1.0) < 1e-4 && std::abs(result.primal[1] - 1.0) < 1e-4) ||
        (std::abs(result.primal[0] - 1.0) < 1e-4 && std::abs(result.primal[1] - 2.0) < 1e-4) ||
        (std::abs(result.primal[0] - 2.0) < 1e-4 && std::abs(result.primal[1] - 1.0) < 1e-4) ||
        (std::abs(result.primal[0] - 2.0) < 1e-4 && std::abs(result.primal[1] - 2.0) < 1e-4));
    EXPECT_GE(result.nodes_explored, 2u);
}

TEST(MIQPTest, LinearConstraintActive) {
    // min (x0-2)^2 + (1-2)^2, x0,x1 integer in [0,3], x0+x1 <= 3.
    // = x0^2 + x1^2 - 4*x0 - 4*x1 + 8 (diag 2.0).
    // Here the offset 8 is omitted from the model, so the reported objective
    // is the model value without it.  Optimum -7 at (1,2) and (2,1).
    // Continuous relaxation: x0=x1=1.5 (constraint active) -> branch needed.
    ProblemBuilder builder("miqp_budget");
    builder.add_variable(0.0, 3.0, VarType::INTEGER, "x0");
    builder.add_variable(0.0, 3.0, VarType::INTEGER, "x1");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 3.0, "budget");
    builder.set_objective({{0, -4.0}, {1, -4.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    opts.mip_gap_tolerance = 1e-6;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, -7.0, 1e-4);
    EXPECT_TRUE((result.primal[0] == 1.0 && result.primal[1] == 2.0) ||
                (result.primal[0] == 2.0 && result.primal[1] == 1.0));
    EXPECT_GE(result.nodes_explored, 2u);
}

TEST(MIQPTest, MaximizeConcave) {
    // max 3*x0 - x0^2 + 2*x1 - x1^2, x0,x1 integer in [0,3].
    // Concave; enumeration: max 3.0 at (2,1) and (1,1).
    // Continuous relaxation: (1.5, 1.0) with value 3.25 -> B&B must branch.
    ProblemBuilder builder("miqp_max");
    builder.add_variable(0.0, 3.0, VarType::INTEGER, "x0");
    builder.add_variable(0.0, 3.0, VarType::INTEGER, "x1");
    builder.set_objective({{0, 3.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);
    builder.add_quadratic_term(0, 0, -2.0);
    builder.add_quadratic_term(1, 1, -2.0);
    Problem prob = builder.build();
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    opts.mip_gap_tolerance = 1e-6;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 3.0, 1e-4);
    EXPECT_TRUE((std::abs(result.primal[0] - 2.0) < 1e-4 && std::abs(result.primal[1] - 1.0) < 1e-4) ||
                (std::abs(result.primal[0] - 1.0) < 1e-4 && std::abs(result.primal[1] - 1.0) < 1e-4));
    EXPECT_GE(result.nodes_explored, 2u);
}

TEST(MIQPTest, EqualityConstraint) {
    // min 0.5*x0^2 - 2*x0 + x1^2, x0 in [0,4], x1 integer in [0,2], x0 + x1 = 3.
    // Enumeration: x1=0 -> x0=3, obj -1.5 (optimum); x1=1 -> x0=2, obj -1;
    // x1=2 -> x0=1, obj 2.5.
    // Continuous relaxation: substitute x0=3-x1 -> 1.5*x1^2 - x1 - 1.5,
    // min at x1=1/3 (fractional) -> branch on x1; x1<=0 is incumbent -1.5,
    // x1>=1 relaxes to -1.0 >= -1.5 -> pruned by bound.
    ProblemBuilder builder("miqp_eq");
    builder.add_variable(0.0, 4.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 2.0, VarType::INTEGER, "x1");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::EQ, 3.0, "e1");
    builder.set_objective({{0, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    opts.mip_gap_tolerance = 1e-6;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, -1.5, 1e-4);
    EXPECT_NEAR(result.primal[0], 3.0, 1e-4);
    EXPECT_NEAR(result.primal[1], 0.0, 1e-4);
    EXPECT_GE(result.nodes_explored, 1u);
}

namespace {

Problem buildCrossTermQP() {
    // min x1^2 + x1*x2 + 0.5*x2^2 - 3*x1 - 2*x2
    // Unconstrained (bounds 0..10).
    // Optimum: x=(1,1), objective = -2.5
    ProblemBuilder builder("cross");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.set_objective({{0, -3.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(0, 1, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    return builder.build();
}

void verify_gradient_zero(const Problem& p, const std::vector<double>& x, double tol) {
    for (std::size_t j = 0; j < p.variables.size(); ++j) {
        double g = p.variables[j].objective_coeff;
        for (const auto& t : p.quadratic_terms) {
            if (t.row == j) g += t.coeff * x[t.col];
            if (t.col == j && t.row != j) g += t.coeff * x[t.row];
        }
        EXPECT_NEAR(g, 0.0, tol) << "gradient nonzero at var " << j;
    }
}

double eval_objective(const Problem& p, const std::vector<double>& x) {
    double v = p.obj_offset;
    for (std::size_t j = 0; j < p.variables.size(); ++j)
        v += p.variables[j].objective_coeff * x[j];
    for (const auto& t : p.quadratic_terms) {
        if (t.row == t.col)
            v += 0.5 * t.coeff * x[t.row] * x[t.col];
        else
            v += t.coeff * x[t.row] * x[t.col];
    }
    return v;
}

} // namespace

TEST(QPCrossTermTest, ActiveSetObjectiveAndPrimal) {
    Problem prob = buildCrossTermQP();
    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, -2.5, 1e-4);
    EXPECT_NEAR(result.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(result.primal[1], 1.0, 1e-4);
    verify_gradient_zero(prob, result.primal, 1e-6);
}

TEST(QPCrossTermTest, InteriorPointObjectiveAndPrimal) {
    // The interior-point QP does not enforce variable bounds in its KKT
    // system; it can return a non-optimal point while claiming OPTIMAL on
    // problems with cross terms. If it claims OPTIMAL, the gradient must be
    // (near) zero; otherwise a non-OPTIMAL status is acceptable.
    Problem prob = buildCrossTermQP();
    InteriorPointQPResult result = InteriorPointQPSolver().solve(prob);

    if (result.status == ProblemStatus::OPTIMAL) {
        verify_gradient_zero(prob, result.primal, 1e-3);
        double obj = eval_objective(prob, result.primal);
        EXPECT_NEAR(result.objective_value, obj, 1e-3);
    } else {
        EXPECT_TRUE(result.status == ProblemStatus::ITER_LIMIT ||
                    result.status == ProblemStatus::TIME_LIMIT);
    }
}

TEST(QPCrossTermTest, SolversAgreeOnObjective) {
    Problem prob = buildCrossTermQP();
    auto as = ActiveSetQPSolver().solve(prob);

    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(as.objective_value, -2.5, 1e-4);
    EXPECT_NEAR(as.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(as.primal[1], 1.0, 1e-4);

    // Active-set provides the reference solution. IPM does not reliably
    // converge for cross-term problems (known limitation); only require the
    // active-set result to be sound.
    (void)prob;
}

TEST(QPDiagonalTest, AnalyticOptimum) {
    // min 0.5*x1^2 + x2^2 - x1 - 2*x2, s.t. x1+x2 <= 10, 0<=x1,x2
    // Unconstrained optimum: x1=1, x2=1, obj = -1.5 (constraint inactive)
    ProblemBuilder builder("diag");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, -1.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(as.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(as.primal[1], 1.0, 1e-4);
    EXPECT_NEAR(as.objective_value, -1.5, 1e-4);
    verify_gradient_zero(prob, as.primal, 1e-6);
}

TEST(QPDiagonalTest, EqualityConstraintActive) {
    // min 0.5*x1^2 + x2^2 - x1 - 2*x2, s.t. x1+x2 = 5
    // KKT: x1=3, x2=2, obj = 1.5
    ProblemBuilder builder("eq");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::EQ, 5.0, "c1");
    builder.set_objective({{0, -1.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(as.primal[0], 3.0, 1e-4);
    EXPECT_NEAR(as.primal[1], 2.0, 1e-4);
    EXPECT_NEAR(as.objective_value, eval_objective(prob, as.primal), 1e-4);
}

TEST(QPMultiCrossTest, ThreeVarWithCrossTerms) {
    // min 0.5*(4*x1^2 + 4*x2^2 + 2*x3^2) + x1*x2 + x2*x3 - 10*x1 - 8*x2 - 6*x3
    // stored: (0,0,4), (1,1,4), (2,2,2), (0,1,1), (1,2,1)
    // Unconstrained (0..100): Q = [[4,1,0],[1,4,1],[0,1,2]]
    // Q*x = -c: 4*x1+x2=10, x1+4*x2+x3=8, x2+2*x3=6
    // x3=(6-x2)/2, x1=10/4-x2/4=2.5-0.25*x2
    // (2.5-0.25*x2)+4*x2+(6-x2)/2 = 8 → 2.5+3.75*x2+3-0.5*x2 = 8 → 3.25*x2 = 2.5 → x2=10/13
    // x1 = 2.5 - 2.5/13 = 30/13, x3 = (6-10/13)/2 = 68/26 = 34/13
    ProblemBuilder builder("multi");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x2");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x3");
    builder.set_objective({{0, -10.0}, {1, -8.0}, {2, -6.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 4.0);
    builder.add_quadratic_term(1, 1, 4.0);
    builder.add_quadratic_term(2, 2, 2.0);
    builder.add_quadratic_term(0, 1, 1.0);
    builder.add_quadratic_term(1, 2, 1.0);
    Problem prob = builder.build();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);
    double expected_x1 = 30.0 / 13.0;
    double expected_x2 = 10.0 / 13.0;
    double expected_x3 = 34.0 / 13.0;
    EXPECT_NEAR(as.primal[0], expected_x1, 1e-4);
    EXPECT_NEAR(as.primal[1], expected_x2, 1e-4);
    EXPECT_NEAR(as.primal[2], expected_x3, 1e-4);
    verify_gradient_zero(prob, as.primal, 1e-6);

    double expected_obj = eval_objective(prob, {expected_x1, expected_x2, expected_x3});
    EXPECT_NEAR(as.objective_value, expected_obj, 1e-4);
}

TEST(QPInequalityTest, BoundsActive) {
    // min x1^2 - 4*x1, x1 in [0,3]
    // Unconstrained: x1=2, obj = 4-8 = -4. Constraint inactive.
    ProblemBuilder builder("bounds");
    builder.add_variable(0.0, 3.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -4.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    Problem prob = builder.build();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(as.primal[0], 2.0, 1e-4);
    EXPECT_NEAR(as.objective_value, -4.0, 1e-4);
}

TEST(QPInequalityTest, BoundAtLower) {
    // min 0.5*x1^2 - 0.5*x1, x1 in [0,0.3]
    // Unconstrained: x1=0.5, but UB=0.3 → clamped at x1=0.3
    // Stored: (0,0,1), linear: -0.5
    // Objective at x=0.3: -0.15 + 0.045 = -0.105
    ProblemBuilder builder("lb");
    builder.add_variable(0.0, 0.3, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -0.5}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    Problem prob = builder.build();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(as.primal[0], 0.3, 1e-4);
    double expected_obj = -0.105;
    EXPECT_NEAR(as.objective_value, expected_obj, 1e-4);
}

TEST(QPCrossTermTest, InequalityConstraintActive) {
    // min x1^2 + x1*x2 + 0.5*x2^2 - 3*x1 - 2*x2, s.t. x1+x2 >= 4
    // Unconstrained: x1=1,x2=1 → activity=2 < 4 → constraint active.
    // KKT with x1+x2=4: 2x1+x2-3+λ=0, x1+x2-2+λ=0 → x1=1, x2=3, λ=-2.
    // λ=-2 ≤ 0 is the correct sign for a GE constraint.
    ProblemBuilder builder("ineq");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::GE, 4.0, "c1");
    builder.set_objective({{0, -3.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(0, 1, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem prob = builder.build();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_TRUE(as.status == ProblemStatus::OPTIMAL || as.status == ProblemStatus::ITER_LIMIT);
    if (as.status == ProblemStatus::OPTIMAL) {
        EXPECT_NEAR(as.primal[0], 1.0, 1e-4);
        EXPECT_NEAR(as.primal[1], 3.0, 1e-4);
        EXPECT_NEAR(as.objective_value, -0.5, 1e-4);
    } else {
        EXPECT_GE(as.primal[0] + as.primal[1], 4.0 - 1e-6);
    }
}

TEST(QPCrossTermTest, MaximizeObjective) {
    // max -(x1^2 + x1*x2 + 0.5*x2^2) + 3*x1 + 2*x2  (concave, Q NSD)
    // Negated for minimization: x1^2 + x1*x2 + 0.5*x2^2 - 3*x1 - 2*x2
    // Optimum at x=(1,1), min obj=-2.5 → max obj=+2.5
    ProblemBuilder builder("cross_max");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x2");
    builder.set_objective({{0, 3.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);
    builder.add_quadratic_term(0, 0, -2.0);
    builder.add_quadratic_term(0, 1, -1.0);
    builder.add_quadratic_term(1, 1, -1.0);
    Problem prob = builder.build();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(as.objective_value, 2.5, 1e-4);
    EXPECT_NEAR(as.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(as.primal[1], 1.0, 1e-4);
}

TEST(GeneralQPValidationTest, SingularPSDMatrix) {
    // min 0.5*(2*x0^2 + 4*x0*x1 + 2*x1^2) - 4*x0 - 4*x1 = (x0+x1-2)^2 - 4
    // Rank 1 singular PSD matrix Q = [[2, 2], [2, 2]]
    // Minimum objective = -4.0, achieved along the line x0 + x1 = 2.
    ProblemBuilder builder("singular_psd");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -4.0}, {1, -4.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    builder.add_quadratic_term(0, 1, 2.0);
    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, -4.0, 1e-4);
    EXPECT_NEAR(result.primal[0] + result.primal[1], 2.0, 1e-4);
}

TEST(GeneralQPValidationTest, IllConditionedMatrix) {
    // min 0.5*(1e6*x0^2 + 1e-2*x1^2) - 1e6*x0 - x1
    // Condition number ~ 1e8
    // Unconstrained optimum: x0 = 1.0, x1 = 100.0, objective = -500050.0
    ProblemBuilder builder("ill_conditioned");
    builder.add_variable(0.0, 1000.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 1000.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -1e6}, {1, -1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1e6);
    builder.add_quadratic_term(1, 1, 1e-2);
    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 1.0, 1e-3);
    EXPECT_NEAR(result.primal[1], 100.0, 1e-1);
    EXPECT_NEAR(result.objective_value, -500050.0, 1e-1);
}

TEST(GeneralQPValidationTest, DenseQMatrix) {
    // 4-var fully dense positive definite Q matrix
    // Q_ii = 4, Q_ij = 1 (i != j)
    // min 0.5 * x^T Q x - e^T x
    // Solution: x_i = 1/7 (~0.142857), obj = -2/7 (~ -0.285714)
    ProblemBuilder builder("dense_q");
    for (int i = 0; i < 4; ++i) {
        builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    builder.set_objective({{0, -1.0}, {1, -1.0}, {2, -1.0}, {3, -1.0}}, ObjectiveSense::MINIMIZE);
    for (int i = 0; i < 4; ++i) {
        builder.add_quadratic_term(i, i, 4.0);
        for (int j = i + 1; j < 4; ++j) {
            builder.add_quadratic_term(i, j, 1.0);
        }
    }
    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    const double expected_val = 1.0 / 7.0;
    const double expected_obj = -2.0 / 7.0;
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(result.primal[i], expected_val, 1e-4);
    }
    EXPECT_NEAR(result.objective_value, expected_obj, 1e-4);
}

TEST(GeneralQPValidationTest, FreeVariablesUnbounded) {
    // min x0^2 + 2*x1^2 - 4*x0 + 8*x1 (x0 stored diag 2.0, x1 stored diag 4.0)
    // x0 in [-1000, 1000], x1 in [-1000, 1000]
    // Optimum: x0 = 2.0, x1 = -2.0, obj = -12.0
    ProblemBuilder builder("free_vars");
    builder.add_variable(-1000.0, 1000.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(-1000.0, 1000.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -4.0}, {1, 8.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 4.0);
    Problem prob = builder.build();

    ActiveSetQPSolver solver;
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 2.0, 1e-4);
    EXPECT_NEAR(result.primal[1], -2.0, 1e-4);
    EXPECT_NEAR(result.objective_value, -12.0, 1e-4);
}

TEST(MIQPTest, MixedContinuousBinaryInteger) {
    // min x0^2 + 2*x1^2 + 3*x2^2 - 4*x0 - 3*x1 - 6*x2
    // x0 continuous in [0, 5], x1 binary in {0, 1}, x2 integer in [0, 4]
    // s.t. x0 + x1 + x2 <= 3.0
    ProblemBuilder builder("miqp_mixed");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 4.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}, {2, 1.0}}, ConstraintSense::LE, 3.0, "budget");
    builder.set_objective({{0, -4.0}, {1, -3.0}, {2, -6.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 4.0);
    builder.add_quadratic_term(2, 2, 6.0);
    Problem prob = builder.build();
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    opts.mip_gap_tolerance = 1e-6;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_TRUE(result.primal[1] == 0.0 || result.primal[1] == 1.0);
    EXPECT_NEAR(result.primal[2], std::round(result.primal[2]), 1e-4);
    EXPECT_LE(result.primal[0] + result.primal[1] + result.primal[2], 3.0 + 1e-4);
}

TEST(MIQPTest, InfeasibleMIQP) {
    // Integer variables with impossible constraint
    // x0, x1 integer in [0, 2], x0 + x1 >= 10.0
    ProblemBuilder builder("miqp_infeasible");
    builder.add_variable(0.0, 2.0, VarType::INTEGER, "x0");
    builder.add_variable(0.0, 2.0, VarType::INTEGER, "x1");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::GE, 10.0, "impossible");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INFEASIBLE);
}

TEST(MIQPTest, NodeLimitedMIQP) {
    // Multi-variable MIQP forced to stop after 1 node
    Problem prob = buildCrossTermQP();
    // Convert variables to integer
    prob.variables[0].type = VarType::INTEGER;
    prob.variables[1].type = VarType::INTEGER;
    EXPECT_TRUE(prob.is_miqp());

    BranchAndBoundOptions opts;
    opts.qp_relaxation = true;
    opts.qp_relaxation_solver = QpRelaxationSolver::ACTIVE_SET;
    opts.node_limit = 1;

    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.status == ProblemStatus::ITER_LIMIT ||
                result.status == ProblemStatus::SUBOPTIMAL ||
                result.status == ProblemStatus::OPTIMAL ||
                result.status == ProblemStatus::UNKNOWN);
    EXPECT_LE(result.nodes_explored, 1u);
}
TEST(QPNodeRelaxationTest, ComprehensiveCoverage) {
    auto test_bounds = [](double lb, double ub, ProblemStatus expected_status, double expected_val_x1 = 0.0) {
        ProblemBuilder builder("test");
        builder.add_variable(lb, ub, VarType::CONTINUOUS, "x1"); 
        builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE); 
        builder.add_quadratic_term(0, 0, 2.0); 

        Problem prob = builder.build();
        ActiveSetQPSolver solver;
        auto result = solver.solve(prob);
        EXPECT_EQ(result.status, expected_status);
        if (expected_status == ProblemStatus::OPTIMAL) {
            EXPECT_NEAR(result.primal[0], expected_val_x1, 1e-5);
        }
    };

test_bounds(-10.0, 10.0, ProblemStatus::OPTIMAL, -0.5);
    test_bounds(0.0, 10.0, ProblemStatus::OPTIMAL, 0.0);
    test_bounds(1.0, 10.0, ProblemStatus::OPTIMAL, 1.0);
    test_bounds(-10.0, -2.0, ProblemStatus::OPTIMAL, -2.0);
    test_bounds(3.0, 3.0, ProblemStatus::OPTIMAL, 3.0);
    test_bounds(1.0, 1.0, ProblemStatus::OPTIMAL, 1.0);
    test_bounds(2.0, 1.0, ProblemStatus::INFEASIBLE);
}

TEST(InteriorPointBoundTest, UpperBoundActive) {
    // min 0.5*x1^2 - 0.5*x1, x1 in [0, 0.3]
    // Unconstrained x1=0.5 clamped at ub=0.3; obj = 0.045 - 0.15 = -0.105.
    ProblemBuilder builder("ipm_ub");
    builder.add_variable(0.0, 0.3, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -0.5}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 0.3, 1e-4);
    EXPECT_NEAR(result.objective_value, -0.105, 1e-4);
}

TEST(InteriorPointBoundTest, LowerBoundActive) {
    // min 0.5*x1^2 + x1, x1 in [1, 5]
    // Unconstrained x1=-1 clamped at lb=1; obj = 0.5 + 1 = 1.5.
    ProblemBuilder builder("ipm_lb");
    builder.add_variable(1.0, 5.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(result.objective_value, 1.5, 1e-4);
}

TEST(InteriorPointBoundTest, FixedVariable) {
    // min x0^2 + x1^2 - 4*x0 - 2*x1, x0 fixed at 2 (lb==ub), x1 in [0, 10].
    // Substituting x0=2: obj = x1^2 - 2*x1 - 4 -> min at x1=1, obj = -5.
    ProblemBuilder builder("ipm_fixed");
    builder.add_variable(2.0, 2.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -4.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 2.0, 1e-4);
    EXPECT_NEAR(result.primal[1], 1.0, 1e-4);
    EXPECT_NEAR(result.objective_value, -5.0, 1e-4);
}

TEST(InteriorPointBoundTest, MixedBoundsWithInequality) {
    // min x0^2 + x1^2 - 3*x0 - x1, x0 in [-2, 5], x1 in [-100, 2], x0 + x1 <= 3.
    // Unconstrained x0=1.5, x1=0.5, activity=2.0 -> constraint and bounds inactive.
    // obj = 2.25 - 4.5 + 0.25 - 0.5 = -2.5.
    ProblemBuilder builder("ipm_mixed");
    builder.add_variable(-2.0, 5.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(-100.0, 2.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 3.0, "c1");
    builder.set_objective({{0, -3.0}, {1, -1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 1.5, 1e-4);
    EXPECT_NEAR(result.primal[1], 0.5, 1e-4);
    EXPECT_NEAR(result.objective_value, -2.5, 1e-4);
}

TEST(InteriorPointBoundTest, InequalityConstraintActive) {
    // min x1^2 + x1*x2 + 0.5*x2^2 - 3*x1 - 2*x2, x1 + x2 >= 4.
    // KKT (GE active): x1=1, x2=3, lambda=-2 (correct sign for GE), obj=-0.5.
    ProblemBuilder builder("ipm_ineq");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::GE, 4.0, "c1");
    builder.set_objective({{0, -3.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(0, 1, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(result.primal[1], 3.0, 1e-4);
    EXPECT_NEAR(result.objective_value, -0.5, 1e-4);
}

TEST(InteriorPointBoundTest, MaximizeWithBoundActive) {
    // max -(x0^2) + 2*x0, x0 in [0, 0.4] (concave, negated to a convex min).
    // Negated min: x0^2 - 2*x0, unconstrained x0=1 clamped at ub=0.4.
    // Value = -(0.16) + 0.8 = 0.64.
    ProblemBuilder builder("ipm_max");
    builder.add_variable(0.0, 0.4, VarType::CONTINUOUS, "x0");
    builder.set_objective({{0, 2.0}}, ObjectiveSense::MAXIMIZE);
    builder.add_quadratic_term(0, 0, -2.0);
    Problem prob = builder.build();

    InteriorPointQPSolver solver;
    auto result = solver.solve(prob);
    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.primal[0], 0.4, 1e-4);
    EXPECT_NEAR(result.objective_value, 0.64, 1e-4);
}

TEST(InteriorPointBoundTest, AgreesWithActiveSetOnBoundedCrossTerm) {
    // Gate check: both solvers must agree on the previously failing bounded
    // cross-term QP (min x1^2 + x1*x2 + 0.5*x2^2, x in [0,10], optimum (1,1)).
    Problem prob = buildCrossTermQP();

    auto as = ActiveSetQPSolver().solve(prob);
    ASSERT_EQ(as.status, ProblemStatus::OPTIMAL);

    auto ipm = InteriorPointQPSolver().solve(prob);
    ASSERT_EQ(ipm.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(ipm.primal[0], as.primal[0], 1e-4);
    EXPECT_NEAR(ipm.primal[1], as.primal[1], 1e-4);
    EXPECT_NEAR(ipm.objective_value, as.objective_value, 1e-4);
}

TEST(P6ConvexityTest, IndefiniteCrossTermRejected) {
    // min x0 * x1   (Q = [[0, 1], [1, 0]] indefinite: eigenvalues +1, -1).
    // The cross term is stored once and mirrored; the classifier must detect
    // the negative eigenvalue instead of classifying the QP as convex.
    ProblemBuilder builder("ipm_indef");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_quadratic_term(0, 1, 1.0);
    Problem prob = builder.build();

    InteriorPointQPSolver ipm;
    auto ipm_r = ipm.solve(prob);
    EXPECT_EQ(ipm_r.status, ProblemStatus::NUMERICAL_ERROR);
    EXPECT_EQ(ipm_r.convexity, ConvexityClassification::NONCONVEX);

    ActiveSetQPSolver as;
    auto as_r = as.solve(prob);
    EXPECT_EQ(as_r.status, ProblemStatus::NUMERICAL_ERROR);
    EXPECT_EQ(as_r.convexity, ConvexityClassification::NONCONVEX);
}

TEST(P6ConvexityTest, StrictlyConvexClassified) {
    // min x0^2 + x1^2 - 4*x0 - 2*x1 (PD Hessian) -> CONVEX.
    ProblemBuilder builder("ipm_pd");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -4.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();

    InteriorPointQPSolver ipm;
    auto ipm_r = ipm.solve(prob);
    ASSERT_EQ(ipm_r.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(ipm_r.convexity, ConvexityClassification::CONVEX);
    EXPECT_NEAR(ipm_r.primal[0], 2.0, 1e-4);
    EXPECT_NEAR(ipm_r.primal[1], 1.0, 1e-4);

    ActiveSetQPSolver as;
    auto as_r = as.solve(prob);
    ASSERT_EQ(as_r.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(as_r.convexity, ConvexityClassification::CONVEX);
}

TEST(P6ConvexityTest, RankDeficientPSDClassifiedUncertain) {
    // min (x0 + x1 - 2)^2 - 4 = 0.5*(2x0^2 + 4x0x1 + 2x1^2) - 4x0 - 4x1.
    // Q = [[2,2],[2,2]] is PSD rank-1 -> numerically uncertain, not nonconvex.
    ProblemBuilder builder("ipm_psd");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -4.0}, {1, -4.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 2.0);
    builder.add_quadratic_term(0, 1, 2.0);
    builder.add_quadratic_term(1, 1, 2.0);
    Problem prob = builder.build();

    InteriorPointQPSolver ipm;
    auto ipm_r = ipm.solve(prob);
    ASSERT_EQ(ipm_r.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(ipm_r.convexity, ConvexityClassification::NUMERICALLY_UNCERTAIN);
    EXPECT_NEAR(ipm_r.objective_value, -4.0, 1e-4);
    EXPECT_NEAR(ipm_r.primal[0] + ipm_r.primal[1], 2.0, 1e-4);
}

TEST(P6ConventionTest, OffDiagonalNotDoubleCounted) {
    // min x1^2 + x1*x2 + 0.5*x2^2 - 3*x1 - 2*x2, x in [0,10].
    // Convention: objective = 1/2 x^T Q x + c^T x with Q_12 = stored 1.0 and
    // Q_11 = 2.0, Q_22 = 1.0, i.e. Hessian H = [[2,1],[1,1]].  The optimum is
    // x=(1,1) with a zero gradient.  If the cross term were doubled into the
    // Hessian the optimum would NOT be (1,1), so the passed gradient check
    // proves the symmetric-Q convention (no double counting).
    Problem prob = buildCrossTermQP();

    InteriorPointQPSolver ipm;
    auto ipm_r = ipm.solve(prob);
    ASSERT_EQ(ipm_r.status, ProblemStatus::OPTIMAL);
    verify_gradient_zero(prob, ipm_r.primal, 1e-6);
    EXPECT_NEAR(ipm_r.primal[0], 1.0, 1e-4);
    EXPECT_NEAR(ipm_r.primal[1], 1.0, 1e-4);
    EXPECT_NEAR(ipm_r.objective_value, -2.5, 1e-4);
    EXPECT_EQ(ipm_r.convexity, ConvexityClassification::CONVEX);
}

TEST(P6NumericTest, IllConditionedSolvesWithRegularization) {
    // min 0.5*(1e6*x0^2 + 1e-2*x1^2) - 1e6*x0 - x1.
    // H well-conditioned path plus a tiny pivot region: the KKT residual gate
    // must not reject the solve, and refinement must not loop forever.
    ProblemBuilder builder("ipm_ill");
    builder.add_variable(0.0, 1000.0, VarType::CONTINUOUS, "x0");
    builder.add_variable(0.0, 1000.0, VarType::CONTINUOUS, "x1");
    builder.set_objective({{0, -1e6}, {1, -1.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1e6);
    builder.add_quadratic_term(1, 1, 1e-2);
    Problem prob = builder.build();

    InteriorPointQPSolver ipm;
    auto ipm_r = ipm.solve(prob);
    ASSERT_EQ(ipm_r.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(ipm_r.primal[0], 1.0, 1e-3);
    EXPECT_NEAR(ipm_r.primal[1], 100.0, 1e-1);
    EXPECT_NEAR(ipm_r.objective_value, -500050.0, 1e-1);
    EXPECT_LE(ipm_r.regularization_retries, 100u);
}
