#include <gtest/gtest.h>
#include <milp/branch_and_bound.hpp>
#include <model/problem.hpp>

#include <limits>
#include <random>

using namespace hypernova::milp;
using namespace hypernova::model;
using namespace hypernova::numerical;

namespace {

Problem make_binary_knapsack(std::mt19937& rng, int n, int seed) {
    ProblemBuilder builder("knap_par" + std::to_string(seed));
    std::uniform_int_distribution<int> wdist(2, 12);
    std::uniform_int_distribution<int> vdist(3, 20);
    std::vector<double> w(n), v(n);
    double wsum = 0.0;
    for (int j = 0; j < n; ++j) {
        w[j] = double(wdist(rng));
        v[j] = double(vdist(rng));
        wsum += w[j];
        builder.add_variable(0.0, 1.0, VarType::BINARY, "x" + std::to_string(j));
    }
    double cap = 0.35 * wsum;
    std::vector<std::pair<std::size_t, double>> row;
    for (int j = 0; j < n; ++j) row.push_back({static_cast<std::size_t>(j), w[j]});
    builder.add_constraint(row, ConstraintSense::LE, cap, "cap");
    std::vector<std::pair<std::size_t, double>> obj;
    for (int j = 0; j < n; ++j) obj.push_back({static_cast<std::size_t>(j), v[j]});
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);
    return builder.build();
}

Problem make_mixed_integer() {
    // min 3y + 10x0 + 9x1 + 8x2 + 7x3,  y + 6x0+6x1+6x2+7x3 >= 13,
    // x binary, y in [0, inf). Mixed model exercises continuous+mip threads.
    ProblemBuilder builder("mixed_par");
    for (int j = 0; j < 4; ++j) builder.add_variable(0.0, 1.0, VarType::BINARY, "x" + std::to_string(j));
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "y");
    builder.add_constraint({{0, 6.0}, {1, 6.0}, {2, 6.0}, {3, 7.0}, {4, 1.0}},
                           ConstraintSense::GE, 13.0, "demand");
    builder.set_objective({{0, 10.0}, {1, 9.0}, {2, 8.0}, {3, 7.0}, {4, 3.0}},
                          ObjectiveSense::MINIMIZE);
    return builder.build();
}

Problem make_miqp() {
    // min (x0 - 1.5)^2 + (x1 - 1.0)^2, x0 + x1 >= 1, x binary.
    ProblemBuilder builder("miqp_par");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x0");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::GE, 1.0, "c1");
    builder.set_objective({{0, -3.0}, {1, -2.0}}, ObjectiveSense::MINIMIZE);
    builder.add_quadratic_term(0, 0, 1.0);
    builder.add_quadratic_term(1, 1, 1.0);
    return builder.build();
}

} // namespace

TEST(ParallelBranchAndBoundTest, ParallelMatchesSerialObjective) {
    std::mt19937 rng(20260916u);
    for (int seed : {100, 101, 102}) {
        Problem prob = make_binary_knapsack(rng, 24, seed);

        BranchAndBoundOptions serial_opts;
        serial_opts.threads = 1;
        serial_opts.cuts.gomory_cuts = false;
        serial_opts.cuts.mir_cuts = false;
        BranchAndBoundSolver serial_solver(ToleranceConfig::industrial_defaults(), serial_opts);
        auto serial = serial_solver.solve(prob);

        ASSERT_EQ(serial.status, ProblemStatus::OPTIMAL) << "seed=" << seed;
        ASSERT_GT(serial.nodes_explored, 0u) << "seed=" << seed;

        for (int threads : {2, 4, 8}) {
            BranchAndBoundOptions par_opts = serial_opts;
            par_opts.threads = threads;
            BranchAndBoundSolver par_solver(ToleranceConfig::industrial_defaults(), par_opts);
            auto par = par_solver.solve(prob);

            EXPECT_EQ(par.status, serial.status) << "seed=" << seed << " threads=" << threads;
            EXPECT_NEAR(par.objective_value, serial.objective_value, 1e-4)
                << "seed=" << seed << " threads=" << threads;
        }
    }
}

TEST(ParallelBranchAndBoundTest, ParallelHandlesMixedInteger) {
    Problem prob = make_mixed_integer();

    BranchAndBoundOptions serial_opts;
    serial_opts.threads = 1;
    serial_opts.cuts.gomory_cuts = false;
    serial_opts.cuts.mir_cuts = false;
    BranchAndBoundSolver serial_solver(ToleranceConfig::industrial_defaults(), serial_opts);
    auto serial = serial_solver.solve(prob);
    // This model's LP relaxation is integral at the root, so it is solved at
    // the root node with no child nodes explored (serial and parallel alike).
    ASSERT_EQ(serial.status, ProblemStatus::OPTIMAL);
    ASSERT_EQ(serial.nodes_explored, 0u);

    for (int threads : {2, 4}) {
        BranchAndBoundOptions par_opts = serial_opts;
        par_opts.threads = threads;
        BranchAndBoundSolver par_solver(ToleranceConfig::industrial_defaults(), par_opts);
        auto par = par_solver.solve(prob);
        EXPECT_EQ(par.status, ProblemStatus::OPTIMAL) << "threads=" << threads;
        EXPECT_EQ(par.nodes_explored, 0u) << "threads=" << threads;
        EXPECT_NEAR(par.objective_value, serial.objective_value, 1e-4) << "threads=" << threads;
    }
}

TEST(ParallelBranchAndBoundTest, ParallelMIQP) {
    Problem prob = make_miqp();

    BranchAndBoundOptions serial_opts;
    serial_opts.threads = 1;
    serial_opts.qp_relaxation = true;
    serial_opts.cuts.gomory_cuts = false;
    serial_opts.cuts.mir_cuts = false;
    serial_opts.heuristics.rounding = false;
    serial_opts.heuristics.diving = false;
    BranchAndBoundSolver serial_solver(ToleranceConfig::industrial_defaults(), serial_opts);
    auto serial = serial_solver.solve(prob);
    ASSERT_TRUE(serial.status == ProblemStatus::OPTIMAL ||
                serial.status == ProblemStatus::SUBOPTIMAL);

    for (int threads : {2, 4}) {
        BranchAndBoundOptions par_opts = serial_opts;
        par_opts.threads = threads;
        BranchAndBoundSolver par_solver(ToleranceConfig::industrial_defaults(), par_opts);
        auto par = par_solver.solve(prob);
        EXPECT_EQ(par.status, serial.status) << "threads=" << threads;
        EXPECT_NEAR(par.objective_value, serial.objective_value, 1e-4) << "threads=" << threads;
    }
}

TEST(ParallelBranchAndBoundTest, InterruptWithMultipleWorkers) {
    std::mt19937 rng(777u);
    Problem prob = make_binary_knapsack(rng, 30, 777);

    BranchAndBoundOptions opts;
    opts.threads = 4;
    opts.interrupt_callback = []() { return true; };
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INTERRUPTED);
}

TEST(ParallelBranchAndBoundTest, TimeLimitWithMultipleWorkers) {
    std::mt19937 rng(2026u);
    Problem prob = make_binary_knapsack(rng, 32, 2026);  // large enough to avoid instant solve

    BranchAndBoundOptions opts;
    opts.threads = 4;
    opts.time_limit_seconds = 0.001;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::TIME_LIMIT);
    EXPECT_GT(result.solve_time_ms, 0.0);
}

TEST(ParallelBranchAndBoundTest, NodeLimitWithMultipleWorkers) {
    std::mt19937 rng(2027u);
    Problem prob = make_binary_knapsack(rng, 26, 2027);

    const int node_limit = 50;
    BranchAndBoundOptions opts;
    opts.threads = 4;
    opts.node_limit = node_limit;
    opts.cuts.gomory_cuts = false;
    opts.cuts.mir_cuts = false;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    // Node-limit exit with any incumbent is honestly reported as SUBOPTIMAL;
    // with no incumbent it is UNKNOWN.
    EXPECT_TRUE(result.status == ProblemStatus::SUBOPTIMAL ||
                result.status == ProblemStatus::UNKNOWN);
    EXPECT_LE(result.nodes_explored, static_cast<std::size_t>(node_limit) + 8u);
}

TEST(ParallelBranchAndBoundTest, StatisticsAreConsistent) {
    std::mt19937 rng(31337u);
    Problem prob = make_binary_knapsack(rng, 22, 31337);

    BranchAndBoundOptions opts;
    opts.threads = 4;
    opts.heuristics.feasibility_pump = true;
    opts.cuts.knapsack_cuts = true;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_GE(result.stats.nodes_created, result.nodes_explored);
    EXPECT_EQ(result.stats.nodes_pruned_by_bound + result.stats.nodes_pruned_infeasible,
              result.nodes_pruned);
    EXPECT_GE(result.nodes_explored, 1u);
    EXPECT_NEAR(result.gap, 0.0, 1e-9);
}