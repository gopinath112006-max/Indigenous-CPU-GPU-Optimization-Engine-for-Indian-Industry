#include <gtest/gtest.h>
#include <hypernova/api.hpp>

using namespace hypernova;

TEST(ScalabilityStressTest, TimeLimitBehavior) {
    model::ProblemBuilder builder("knapsack_stress");
    std::size_t n = 150;
    for (std::size_t j = 0; j < n; ++j) {
        builder.add_variable(0.0, 1.0, model::VarType::BINARY, "x" + std::to_string(j));
    }

    std::vector<std::pair<std::size_t, double>> obj, con;
    for (std::size_t j = 0; j < n; ++j) {
        obj.push_back({j, static_cast<double>((j * 17) % 100 + 10)});
        con.push_back({j, static_cast<double>((j * 13) % 90 + 15)});
    }
    builder.set_objective(obj, model::ObjectiveSense::MAXIMIZE);
    builder.add_constraint(con, model::ConstraintSense::LE, 2000.0, "Capacity");

    model::Problem problem = builder.build();

    SolverOptions opts;
    opts.engine = EngineType::BRANCH_AND_BOUND;
    opts.time_limit_seconds = 0.05; // 50 ms
    Solver solver(opts);
    Solution sol = solver.solve(problem);

    EXPECT_TRUE(sol.status == model::ProblemStatus::TIME_LIMIT || sol.is_optimal());
}

TEST(ScalabilityStressTest, InterruptBehavior) {
    model::ProblemBuilder builder("interrupt_test");
    for (std::size_t j = 0; j < 50; ++j) {
        builder.add_variable(0.0, 1.0, model::VarType::BINARY, "b" + std::to_string(j));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (std::size_t j = 0; j < 50; ++j) obj.push_back({j, static_cast<double>(j + 1)});
    builder.set_objective(obj, model::ObjectiveSense::MAXIMIZE);

    model::Problem problem = builder.build();

    SolverOptions opts;
    opts.engine = EngineType::BRANCH_AND_BOUND;
    Solver solver(opts);
    solver.interrupt(); // Trigger interrupt
    Solution sol = solver.solve(problem);

    EXPECT_EQ(sol.status, model::ProblemStatus::INTERRUPTED);
}

