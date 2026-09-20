#include <gtest/gtest.h>
#include <hypernova/api.hpp>

using namespace hypernova;

TEST(IndustrialCasesTest, CrudeBlendingQP) {
    model::ProblemBuilder builder("mrpl_crude_blending_qp");
    std::size_t c_arab   = builder.add_variable(0.0, 120.0, model::VarType::CONTINUOUS, "Arab_Light");
    std::size_t c_bonny  = builder.add_variable(0.0,  90.0, model::VarType::CONTINUOUS, "Bonny_Light");
    std::size_t c_maya   = builder.add_variable(0.0,  60.0, model::VarType::CONTINUOUS, "Maya_Heavy");
    std::size_t c_murban = builder.add_variable(0.0, 100.0, model::VarType::CONTINUOUS, "Murban_Sweet");
    std::size_t c_basrah = builder.add_variable(0.0,  70.0, model::VarType::CONTINUOUS, "Basrah_Heavy");

    builder.set_objective({
        {c_arab, 75.0}, {c_bonny, 82.0}, {c_maya, 62.0}, {c_murban, 79.0}, {c_basrah, 64.0}
    }, model::ObjectiveSense::MINIMIZE);

    double devs[5] = {0.60, -1.11, 2.15, -0.47, 1.70};
    std::size_t vars[5] = {c_arab, c_bonny, c_maya, c_murban, c_basrah};
    for (int i = 0; i < 5; ++i) {
        builder.add_quadratic_term(vars[i], vars[i], devs[i] * devs[i]);
        for (int j = i + 1; j < 5; ++j) {
            builder.add_quadratic_term(vars[i], vars[j], devs[i] * devs[j]);
        }
    }


    builder.add_constraint({
        {c_arab, 1.0}, {c_bonny, 1.0}, {c_maya, 1.0}, {c_murban, 1.0}, {c_basrah, 1.0}
    }, model::ConstraintSense::EQ, 300.0, "Total_Throughput_Target");

    builder.add_constraint({
        {c_arab, 2.2}, {c_bonny, 4.4}, {c_maya, -9.2}, {c_murban, 9.2}, {c_basrah, -7.5}
    }, model::ConstraintSense::GE, 0.0, "API_Gravity_Min_BSVI");

    builder.add_constraint({
        {c_arab, 1.85}, {c_bonny, 0.14}, {c_maya, 3.40}, {c_murban, 0.78}, {c_basrah, 2.95}
    }, model::ConstraintSense::LE, 540.0, "Sulfur_Max_Desulfurization_Cap");

    builder.add_constraint({
        {c_arab, 1.2}, {c_bonny, 0.1}, {c_maya, 6.5}, {c_murban, -0.2}, {c_basrah, 5.1}
    }, model::ConstraintSense::GE, 0.0, "Viscosity_Min_Limit");


    model::Problem problem = builder.build();
    SolverOptions opts;
    opts.engine = EngineType::QP_ACTIVE_SET;
    Solver solver(opts);
    Solution sol = solver.solve(problem);

    EXPECT_TRUE(sol.is_optimal());
}



TEST(IndustrialCasesTest, ProductionPlanningMILP) {
    model::ProblemBuilder builder("mrpl_production_planning_milp");
    std::size_t p_gas = builder.add_variable(0.0, 300.0, model::VarType::CONTINUOUS, "Gasoline");
    std::size_t p_die = builder.add_variable(0.0, 450.0, model::VarType::CONTINUOUS, "Diesel");
    std::size_t p_atf = builder.add_variable(0.0, 250.0, model::VarType::CONTINUOUS, "ATF");
    std::size_t m_die = builder.add_variable(0.0, 1.0, model::VarType::BINARY, "Mode_Max_Diesel");
    std::size_t m_atf = builder.add_variable(0.0, 1.0, model::VarType::BINARY, "Mode_Max_ATF");

    builder.set_objective({
        {p_gas, 820.0}, {p_die, 870.0}, {p_atf, 920.0},
        {m_die, -4500.0}, {m_atf, -6200.0}
    }, model::ObjectiveSense::MAXIMIZE);

    builder.add_constraint({{m_die, 1.0}, {m_atf, 1.0}}, model::ConstraintSense::EQ, 1.0, "Exclusive_Mode");
    builder.add_constraint({{p_die, 1.0}, {m_die, -450.0}, {m_atf, -250.0}}, model::ConstraintSense::LE, 0.0, "Diesel_Yield_Limit");
    builder.add_constraint({{p_gas, 1.0}}, model::ConstraintSense::GE, 100.0, "Min_Gas_Demand");
    builder.add_constraint({{p_die, 1.0}}, model::ConstraintSense::GE, 200.0, "Min_Die_Demand");

    model::Problem problem = builder.build();
    SolverOptions opts;
    opts.engine = EngineType::BRANCH_AND_BOUND;
    Solver solver(opts);
    Solution sol = solver.solve(problem);

    EXPECT_TRUE(sol.is_optimal());
    EXPECT_GT(sol.objective_value, 0.0);
    EXPECT_NEAR(sol.primal[m_die] + sol.primal[m_atf], 1.0, 1e-4);
}

TEST(IndustrialCasesTest, LogisticsFreightMILP) {
    model::ProblemBuilder builder("mrpl_logistics_freight_milp");
    std::size_t x_pipe_hsn = builder.add_variable(0.0, 300.0, model::VarType::CONTINUOUS, "Vol_Pipe_Hassan");
    std::size_t z_pipe_hsn = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Trigger_Pipe_Hassan");
    std::size_t x_pipe_blr = builder.add_variable(0.0, 400.0, model::VarType::CONTINUOUS, "Vol_Pipe_Bengaluru");
    std::size_t z_pipe_blr = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Trigger_Pipe_Bengaluru");
    std::size_t x_ship_goa = builder.add_variable(0.0, 150.0, model::VarType::CONTINUOUS, "Vol_Tanker_Goa");
    std::size_t x_ship_koc = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "Vol_Tanker_Kochi");
    std::size_t x_rail_hyd = builder.add_variable(0.0, 180.0, model::VarType::CONTINUOUS, "Vol_Rail_Hyderabad");
    std::size_t z_rail_hyd = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Trigger_Rail_Hyderabad");
    builder.set_objective({
        {x_pipe_hsn, 5.0},  {z_pipe_hsn, 500.0},
        {x_pipe_blr, 8.0},  {z_pipe_blr, 800.0},
        {x_ship_goa, 7.0},
        {x_ship_koc, 9.0},
        {x_rail_hyd, 16.0}, {z_rail_hyd, 300.0}
    }, model::ObjectiveSense::MINIMIZE);
    builder.add_constraint({{x_pipe_hsn, 1.0}, {z_pipe_hsn, -50.0}}, model::ConstraintSense::GE, 0.0, "Pipe_Hsn_Min_Batch");
    builder.add_constraint({{x_pipe_hsn, 1.0}, {z_pipe_hsn, -300.0}}, model::ConstraintSense::LE, 0.0, "Pipe_Hsn_Max_Cap");
    builder.add_constraint({{x_pipe_blr, 1.0}, {z_pipe_blr, -80.0}}, model::ConstraintSense::GE, 0.0, "Pipe_Blr_Min_Batch");
    builder.add_constraint({{x_pipe_blr, 1.0}, {z_pipe_blr, -400.0}}, model::ConstraintSense::LE, 0.0, "Pipe_Blr_Max_Cap");
    builder.add_constraint({{x_rail_hyd, 1.0}, {z_rail_hyd, -40.0}}, model::ConstraintSense::GE, 0.0, "Rail_Hyd_Min_Rake");
    builder.add_constraint({{x_rail_hyd, 1.0}, {z_rail_hyd, -180.0}}, model::ConstraintSense::LE, 0.0, "Rail_Hyd_Max_Cap");
    builder.add_constraint({{x_pipe_hsn, 1.0}}, model::ConstraintSense::GE, 120.0, "Demand_Hassan");
    builder.add_constraint({{x_pipe_blr, 1.0}}, model::ConstraintSense::GE, 250.0, "Demand_Bengaluru");
    builder.add_constraint({{x_ship_goa, 1.0}}, model::ConstraintSense::GE,  90.0, "Demand_Goa");
    builder.add_constraint({{x_ship_koc, 1.0}}, model::ConstraintSense::GE, 140.0, "Demand_Kochi");
    builder.add_constraint({{x_rail_hyd, 1.0}}, model::ConstraintSense::GE, 100.0, "Demand_Hyderabad");
    model::Problem problem = builder.build();

    for (bool presolve_on : {true, false}) {
        SolverOptions opts;
        opts.engine = EngineType::BRANCH_AND_BOUND;
        opts.presolve = presolve_on ? PresolveLevel::AGGRESSIVE : PresolveLevel::OFF;
        Solver solver(opts);
        Solution sol = solver.solve(problem);
        
        EXPECT_TRUE(sol.is_optimal());
        EXPECT_NEAR(sol.objective_value, 7690.0, 1e-4);
        EXPECT_NEAR(sol.primal[x_pipe_hsn], 120.0, 1e-4);
        EXPECT_NEAR(sol.primal[z_pipe_hsn], 1.0, 1e-4);
        EXPECT_NEAR(sol.primal[x_pipe_blr], 250.0, 1e-4);
        EXPECT_NEAR(sol.primal[z_pipe_blr], 1.0, 1e-4);
        EXPECT_NEAR(sol.primal[x_ship_goa], 90.0, 1e-4);
        EXPECT_NEAR(sol.primal[x_ship_koc], 140.0, 1e-4);
        EXPECT_NEAR(sol.primal[x_rail_hyd], 100.0, 1e-4);
        EXPECT_NEAR(sol.primal[z_rail_hyd], 1.0, 1e-4);
    }
}

TEST(IndustrialCasesTest, HydrogenNetworkLP) {
    model::ProblemBuilder builder("mrpl_hydrogen_network_lp");
    std::size_t h_hgu  = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "HGU");
    std::size_t h_ccr  = builder.add_variable(0.0,  80.0, model::VarType::CONTINUOUS, "CCR");
    std::size_t h_psa  = builder.add_variable(0.0, 100.0, model::VarType::CONTINUOUS, "PSA");
    std::size_t h_dhds = builder.add_variable(0.0, 150.0, model::VarType::CONTINUOUS, "DHDS");
    std::size_t h_vgt  = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "VGT");

    builder.set_objective({{h_hgu, 0.45}, {h_ccr, 0.15}}, model::ObjectiveSense::MINIMIZE);
    builder.add_constraint({{h_hgu, 1.0}, {h_psa, 0.85}, {h_dhds, -1.0}, {h_vgt, -1.0}}, model::ConstraintSense::GE, 0.0, "Mass_Balance");
    builder.add_constraint({{h_dhds, 1.0}}, model::ConstraintSense::GE, 80.0, "DHDS_Demand");
    builder.add_constraint({{h_vgt, 1.0}}, model::ConstraintSense::GE, 110.0, "VGT_Demand");
    builder.add_constraint({{h_psa, 1.0}, {h_ccr, -0.90}}, model::ConstraintSense::LE, 0.0, "PSA_Limit");

    model::Problem problem = builder.build();
    SolverOptions opts;
    opts.engine = EngineType::AUTO;
    Solver solver(opts);
    Solution sol = solver.solve(problem);

    EXPECT_TRUE(sol.is_optimal());
    EXPECT_NEAR(sol.primal[h_dhds], 80.0, 1e-4);
    EXPECT_NEAR(sol.primal[h_vgt], 110.0, 1e-4);
}
