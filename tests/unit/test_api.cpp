#include <gtest/gtest.h>
#include <hypernova/api.hpp>
#include <limits>

using namespace hypernova;

TEST(APITest, SolverCreateDefault) {
    Solver solver;
    EXPECT_FALSE(solver.interrupted());
    EXPECT_EQ(solver.options().engine, EngineType::AUTO);
}

TEST(APITest, SolverSetOptions) {
    Solver solver;
    SolverOptions opts;
    opts.engine = EngineType::PRIMAL_SIMPLEX;
    opts.mip_gap_tolerance = 1e-6;
    solver.set_options(opts);

    EXPECT_EQ(solver.options().engine, EngineType::PRIMAL_SIMPLEX);
    EXPECT_NEAR(solver.options().mip_gap_tolerance, 1e-6, 1e-12);
}

TEST(APITest, SolverInterrupt) {
    Solver solver;
    EXPECT_FALSE(solver.interrupted());
    solver.interrupt();
    EXPECT_TRUE(solver.interrupted());
}

TEST(APITest, SolverSolveLP) {
    Problem prob;
    prob.name = "api_lp";
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x1");
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x2");
    prob.add_constraint({{0, 1.0}, {1, 1.0}}, model::ConstraintSense::LE, 10.0, "c1");
    prob.add_constraint({{0, 2.0}, {1, 1.0}}, model::ConstraintSense::LE, 14.0, "c2");
    prob.set_objective({{0, 3.0}, {1, 5.0}}, model::ObjectiveSense::MAXIMIZE);

    Solver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.is_feasible()) << "Status was: " << static_cast<int>(result.status);
    EXPECT_GT(result.solve_time_ms, 0.0);
}

TEST(APITest, SolverComputeIIS) {
    Problem prob;
    prob.name = "api_iis";
    prob.add_variable(-1e9, 1e9, model::VarType::CONTINUOUS, "x1");
    prob.add_constraint({{0, 1.0}}, model::ConstraintSense::LE, 5.0, "upper");
    prob.add_constraint({{0, 1.0}}, model::ConstraintSense::GE, 7.0, "lower");

    Solver solver;
    auto iis = solver.compute_iis(prob);

    EXPECT_TRUE(iis.found);
    EXPECT_EQ(iis.constraint_indices.size(), 2u);
    EXPECT_GT(iis.lp_solves, 0u);
}

TEST(APITest, SolverComputeIISFeasible) {
    Problem prob;
    prob.name = "api_iis_ok";
    prob.add_variable(-1e9, 1e9, model::VarType::CONTINUOUS, "x1");
    prob.add_constraint({{0, 1.0}}, model::ConstraintSense::LE, 5.0, "upper");
    prob.add_constraint({{0, 1.0}}, model::ConstraintSense::GE, -3.0, "lower");

    Solver solver;
    auto iis = solver.compute_iis(prob);

    EXPECT_FALSE(iis.found);
    EXPECT_TRUE(iis.constraint_indices.empty());
}

TEST(APITest, SolverDiagnoseInfeasible) {
    Problem prob;
    prob.name = "api_diag_inf";
    prob.add_variable(0.0, 1.0, model::VarType::CONTINUOUS, "x1");
    prob.add_constraint({{0, 1.0}}, model::ConstraintSense::GE, 2.0, "ge2");

    Solver solver;
    auto res = solver.diagnose(prob);

    EXPECT_EQ(res.status, model::ProblemStatus::INFEASIBLE);
    EXPECT_TRUE(res.farkas.valid);
    EXPECT_LT(res.farkas.farkas_value, 0.0);
}

TEST(APITest, SolverDiagnoseUnbounded) {
    Problem prob;
    prob.name = "api_diag_unb";
    prob.add_variable(0.0, std::numeric_limits<double>::infinity(), model::VarType::CONTINUOUS, "x1");
    prob.set_objective({{0, -1.0}}, model::ObjectiveSense::MINIMIZE);

    Solver solver;
    auto res = solver.diagnose(prob);

    EXPECT_EQ(res.status, model::ProblemStatus::UNBOUNDED);
    EXPECT_TRUE(res.ray.valid);
    EXPECT_LT(res.ray.objective_direction, 0.0);
}

TEST(APITest, SolverDiagnoseFeasible) {
    Problem prob;
    prob.name = "api_diag_ok";
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x1");
    prob.add_constraint({{0, 1.0}}, model::ConstraintSense::LE, 5.0, "le5");

    Solver solver;
    auto res = solver.diagnose(prob);

    EXPECT_EQ(res.status, model::ProblemStatus::OPTIMAL);
    EXPECT_FALSE(res.farkas.valid);
    EXPECT_FALSE(res.ray.valid);
}

TEST(APITest, SolverDiagnoseNumerical) {
    Problem prob;
    prob.name = "api_diag_num";
    prob.add_variable(0.0, std::numeric_limits<double>::infinity(), model::VarType::CONTINUOUS, "x1");
    prob.add_variable(0.0, std::numeric_limits<double>::infinity(), model::VarType::CONTINUOUS, "x2");
    prob.add_constraint({{0, 1.0}, {1, 2.0}}, model::ConstraintSense::LE, 3.0, "c1");
    prob.set_objective({{0, 3.0}, {1, 2.0}}, model::ObjectiveSense::MAXIMIZE);

    Solver solver;
    auto d = solver.diagnose_numerical(prob);

    EXPECT_EQ(d.status, model::ProblemStatus::OPTIMAL);
    EXPECT_EQ(d.num_variables, 2u);
    EXPECT_EQ(d.num_constraints, 1u);
    EXPECT_EQ(d.num_nonzeros, 2u);
    EXPECT_GT(d.condition_estimate, 0.0);
    EXPECT_LT(d.condition_estimate, 1e12);
    EXPECT_EQ(d.quality, "good");
    EXPECT_LT(d.primal_infeasibility, 1e-6);
    EXPECT_FALSE(d.to_json().empty());
}

TEST(APITest, SolverDiagnoseNumericalIllConditioned) {
    Problem prob;
    prob.name = "api_diag_num_bad";
    prob.add_variable(0.0, std::numeric_limits<double>::infinity(), model::VarType::CONTINUOUS, "x1");
    prob.add_variable(0.0, std::numeric_limits<double>::infinity(), model::VarType::CONTINUOUS, "x2");
    prob.add_constraint({{0, 1.0}, {1, 1.0}}, model::ConstraintSense::LE, 1.0, "c1");
    prob.add_constraint({{0, 1.0}, {1, 1.0 + 1e-7}}, model::ConstraintSense::LE, 1.0, "c2");
    prob.set_objective({{0, 1.0}}, model::ObjectiveSense::MAXIMIZE);

    Solver solver;
    auto d = solver.diagnose_numerical(prob);

    EXPECT_TRUE(d.matrix_rank_deficient || d.condition_estimate >= 1e12);
    EXPECT_TRUE(d.quality == "warn" || d.quality == "poor");
    ASSERT_FALSE(d.warnings.empty());
    bool saw_cond = false;
    for (const auto& w : d.warnings) {
        if (w.find("singular") != std::string::npos ||
            w.find("ill-conditioned") != std::string::npos) saw_cond = true;
    }
    EXPECT_TRUE(saw_cond);
}

TEST(APITest, SolverSolveMILP) {
    Problem prob;
    prob.name = "api_milp";
    prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x1");
    prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x2");
    prob.add_constraint({{0, 1.0}, {1, 1.0}}, model::ConstraintSense::LE, 7.0, "c1");
    prob.set_objective({{0, 5.0}, {1, 4.0}}, model::ObjectiveSense::MAXIMIZE);

    Solver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.is_feasible());
    EXPECT_NEAR(result.objective_value, 35.0, 1e-2);
}

TEST(APITest, ProblemAddVariableAndConstraint) {
    Problem prob;
    auto v0 = prob.add_variable(0.0, 5.0, model::VarType::CONTINUOUS, "x1");
    auto v1 = prob.add_variable(0.0, 3.0, model::VarType::CONTINUOUS, "x2");
    prob.add_constraint({{v0, 1.0}, {v1, 2.0}}, model::ConstraintSense::LE, 10.0, "c1");
    prob.set_objective({{v0, 1.0}, {v1, 3.0}}, model::ObjectiveSense::MAXIMIZE);

    EXPECT_EQ(prob.variables.size(), 2u);
    EXPECT_EQ(prob.constraints.size(), 1u);
    EXPECT_NEAR(prob.variables[0].lower_bound, 0.0, 1e-12);
    EXPECT_NEAR(prob.variables[0].upper_bound, 5.0, 1e-12);
    EXPECT_NEAR(prob.variables[1].lower_bound, 0.0, 1e-12);
    EXPECT_NEAR(prob.variables[1].upper_bound, 3.0, 1e-12);
    EXPECT_EQ(prob.obj_sense, model::ObjectiveSense::MAXIMIZE);
}

TEST(APITest, ProblemSetObjective) {
    Problem prob;
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x1");
    prob.set_objective({{0, 7.0}}, model::ObjectiveSense::MINIMIZE);

    EXPECT_NEAR(prob.variables[0].objective_coeff, 7.0, 1e-12);
    EXPECT_EQ(prob.obj_sense, model::ObjectiveSense::MINIMIZE);
}

TEST(APITest, ProblemAddQuadraticTerm) {
    Problem prob;
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x1");
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x2");
    prob.add_quadratic_term(0, 0, 2.0);
    prob.add_quadratic_term(0, 1, 1.0);

    EXPECT_EQ(prob.quadratic_terms.size(), 2u);
    EXPECT_TRUE(prob.is_qp());
}

TEST(APITest, SolverSolveQP) {
    Problem prob;
    prob.name = "api_qp";
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x1");
    prob.add_variable(0.0, 10.0, model::VarType::CONTINUOUS, "x2");
    prob.add_constraint({{0, 1.0}, {1, 1.0}}, model::ConstraintSense::LE, 4.0, "c1");
    prob.set_objective({{0, -4.0}, {1, -5.0}}, model::ObjectiveSense::MINIMIZE);
    prob.add_quadratic_term(0, 0, 1.0);
    prob.add_quadratic_term(1, 1, 1.0);

    Solver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.is_feasible());
    EXPECT_NEAR(result.objective_value, -14.25, 0.5);
}

TEST(APITest, SolverSolveMIQP) {
    Problem prob;
    prob.name = "api_miqp";
    prob.add_variable(0.0, 4.0, model::VarType::CONTINUOUS, "x0");
    prob.add_variable(0.0, 1.0, model::VarType::BINARY, "x1");
    prob.set_objective({{0, -2.0}, {1, -1.0}}, model::ObjectiveSense::MINIMIZE);
    prob.add_quadratic_term(0, 0, 1.0);
    prob.add_quadratic_term(1, 1, 2.0);

    Solver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.is_optimal());
    EXPECT_NEAR(result.objective_value, -2.0, 1e-3);
    EXPECT_NEAR(result.primal[0], 2.0, 1e-3);
    EXPECT_TRUE(result.primal[1] == 0.0 || result.primal[1] == 1.0);
    EXPECT_GE(result.bb_nodes, 1u);
}

TEST(APITest, SolverSolveMIQPInteriorPoint) {
    Problem prob;
    prob.name = "api_miqp_ipm";
    prob.add_variable(0.0, 4.0, model::VarType::CONTINUOUS, "x0");
    prob.add_variable(0.0, 1.0, model::VarType::BINARY, "x1");
    prob.set_objective({{0, -2.0}, {1, -1.0}}, model::ObjectiveSense::MINIMIZE);
    prob.add_quadratic_term(0, 0, 1.0);
    prob.add_quadratic_term(1, 1, 2.0);

    SolverOptions opts;
    opts.engine = EngineType::QP_INTERIOR_POINT;
    Solver solver;
    solver.set_options(opts);
    auto result = solver.solve(prob);

    // Barrier-QP node relaxations must converge for a well-posed convex MIQP;
    // a fixed (branching) variable is modelled as an equality row in the KKT,
    // so node solves must not return a failure status.
    ASSERT_TRUE(result.is_optimal());
    EXPECT_NEAR(result.objective_value, -2.0, 1e-3);
    EXPECT_NEAR(result.primal[0], 2.0, 1e-3);
    EXPECT_TRUE(result.primal[1] == 0.0 || result.primal[1] == 1.0);
    EXPECT_GE(result.bb_nodes, 1u);
}

TEST(APITest, SolverSolveLPBAC) {
    Problem prob;
    prob.name = "api_bac";
    prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x1");
    prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x2");
    prob.add_constraint({{0, 1.0}, {1, 1.0}}, model::ConstraintSense::LE, 7.0, "c1");
    prob.set_objective({{0, 5.0}, {1, 4.0}}, model::ObjectiveSense::MAXIMIZE);

    SolverOptions opts;
    opts.engine = EngineType::BRANCH_AND_CUT;
    Solver solver(opts);
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.is_feasible());
}

TEST(APITest, OptionWiringVerification) {
    Problem prob;
    prob.name = "option_wiring";
    prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x1");
    prob.add_variable(0.0, 10.0, model::VarType::INTEGER, "x2");
    prob.add_constraint({{0, 1.0}, {1, 1.0}}, model::ConstraintSense::LE, 7.0, "c1");
    prob.set_objective({{0, 5.0}, {1, 4.0}}, model::ObjectiveSense::MAXIMIZE);

    SolverOptions opts;
    opts.node_limit = 5;
    opts.solution_limit = 1;
    opts.use_gpu = true;
    opts.compute_target = ComputeTarget::CPU_GPU_AUTO;
    opts.presolve = PresolveLevel::AGGRESSIVE;
    opts.scaling = ScalingMethod::GEOMETRIC;

    Solver solver(opts);
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.is_feasible() || result.status == model::ProblemStatus::ITER_LIMIT);
    EXPECT_FALSE(result.backend_used.empty());
    EXPECT_EQ(solver.options().node_limit, 5u);
    EXPECT_EQ(solver.options().solution_limit, 1u);
    EXPECT_TRUE(solver.options().use_gpu);
}
