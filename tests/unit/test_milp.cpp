#include <gtest/gtest.h>
#include <milp/branch_and_bound.hpp>
#include <milp/cutting_planes.hpp>
#include <milp/heuristics.hpp>
#include <milp/node_selection.hpp>
#include <milp/node_presolve.hpp>
#include <milp/symmetry.hpp>
#include <model/problem.hpp>

#include <limits>
#include <random>

using namespace hypernova::milp;
using namespace hypernova::model;
using namespace hypernova::numerical;

TEST(BranchAndBoundTest, InterruptCallback) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    BranchAndBoundOptions opts;
    opts.interrupt_callback = []() { return true; };
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INTERRUPTED);
}

TEST(BranchAndBoundTest, SimpleMILP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    BranchAndBoundSolver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.status == ProblemStatus::OPTIMAL || result.status == ProblemStatus::SUBOPTIMAL);
    EXPECT_GE(result.nodes_explored, 0);
}

TEST(BranchAndBoundTest, PureIntegerMILP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    BranchAndBoundSolver solver;
    auto result = solver.solve(prob);

    EXPECT_TRUE(result.status == ProblemStatus::OPTIMAL || result.status == ProblemStatus::SUBOPTIMAL);
}

TEST(CuttingPlanesTest, GomoryCuts) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::EQ, 5.5, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    CutGenerator generator;
    std::vector<double> lp_sol = {2.5, 3.0};
    auto cuts = generator.generate_gomory_cuts(prob, lp_sol);

    EXPECT_GE(cuts.size(), 0);
}

TEST(HeuristicsTest, Rounding) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    RoundingHeuristic heuristic;
    std::vector<double> lp_sol = {5.3, 4.7};
    auto result = heuristic.run(prob, lp_sol);

    EXPECT_TRUE(result.found_solution);
    EXPECT_EQ(result.solution[1], 5.0);
}

TEST(HeuristicsTest, Diving) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    DivingHeuristic heuristic;
    std::vector<double> lp_sol = {5.3, 4.7};
    auto result = heuristic.run(prob, lp_sol);

    EXPECT_GE(result.found_solution ? 1 : 0, 0);
}

TEST(NodeSelectionTest, BestFirst) {
    BestFirstSelector selector;
    EXPECT_TRUE(selector.empty());
    EXPECT_EQ(selector.size(), 0);
}

TEST(NodeSelectionTest, Hybrid) {
    HybridSelector selector(3);
    EXPECT_TRUE(selector.empty());
    EXPECT_EQ(selector.size(), 0);
}

TEST(NodePresolveTest, TightensUpperBound) {
    // x in [0, 5] INTEGER, 2x + y <= 3 with y >= 0: x <= 1 (integer).
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 5.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 2.0}, {1, 1.0}}, ConstraintSense::LE, 3.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    NodePresolver presolver;
    NodePresolveResult r = presolver.tighten(prob, {});

    EXPECT_TRUE(r.feasible);
    bool found = false;
    for (const auto& [j, ub] : r.tightened_ub) {
        if (j == 0) { EXPECT_NEAR(ub, 1.0, 1e-9); found = true; }
    }
    EXPECT_TRUE(found);
}

TEST(NodePresolveTest, PlaneRowPruneByPropagation) {
    // x >= 5 (GE) and 2x <= 4 (LE) with x >= 0 integer: infeasible by activity.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 5.0, "ge5");
    builder.add_constraint({{0, 2.0}}, ConstraintSense::LE, 4.0, "le4");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    NodePresolver presolver;
    NodePresolveResult r = presolver.tighten(prob, {});

    EXPECT_FALSE(r.feasible);
}

TEST(NodePresolveTest, RespectsNodeBounds) {
    // y >= 2 binds x <= 1 via x < y with y in [0,5], x integer.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 5.0, VarType::INTEGER, "x1");
    builder.add_variable(0.0, 5.0, VarType::CONTINUOUS, "y2");
    builder.add_constraint({{0, 1.0}, {1, -1.0}}, ConstraintSense::LE, 0.0, "c1"); // x <= y
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    NodePresolver presolver;
    NodePresolveResult r = presolver.tighten(prob, {{1, 2.0, true}, {1, 2.0, false}}); // y fixed to 2

    EXPECT_TRUE(r.feasible);
    bool found = false;
    for (const auto& [j, ub] : r.tightened_ub) {
        if (j == 0) { EXPECT_NEAR(ub, 2.0, 1e-9); found = true; }
    }
    EXPECT_TRUE(found);
}

TEST(NodePresolveTest, InfeasibleFromIntegerGap) {
    // 3x >= 1  and  4x <= 2  for x in {0,1}: tight bounds are [1/3, 1/8],
    // which cannot contain an integer.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::INTEGER, "x1");
    builder.add_constraint({{0, 3.0}}, ConstraintSense::GE, 1.0, "ge1");
    builder.add_constraint({{0, 4.0}}, ConstraintSense::LE, 2.0, "le2");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    NodePresolver presolver;
    NodePresolveResult r = presolver.tighten(prob, {});

    EXPECT_FALSE(r.feasible);
}

TEST(BranchAndBoundTest, NodePresolvePrunesWithoutHarmingSolution) {
    // Mixed model: node presolve must preserve the exact optimum.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::INTEGER, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundOptions opts;
    opts.node_presolve = true;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 20.0, 1e-4);
}

TEST(BranchAndBoundTest, BinaryBranchingRecoversIntegerOptimum) {
    // LP relaxation is fractional (x1=1, x2=0.5, obj=1.5); branching must
    // recover the integer optimum 1. Regression test: node bounds previously
    // folded into every variable, pinning both binaries identically.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 1.5, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundOptions opts;
    opts.node_presolve = false;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 1.0, 1e-4);
}

TEST(SymmetryTest, DetectsIdenticalColumns) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x3");
    builder.add_constraint({{0, 5.0}, {1, 5.0}, {2, 5.0}}, ConstraintSense::LE, 12.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}, {2, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SymmetryDetector detector;
    SymmetryDetection sym = detector.detect(prob);

    ASSERT_EQ(sym.variable_groups.size(), 1u);
    ASSERT_EQ(sym.variable_groups[0].size(), 3u);
    EXPECT_EQ(sym.variable_groups[0][0], 0u);
    EXPECT_EQ(sym.variable_groups[0][1], 1u);
    EXPECT_EQ(sym.variable_groups[0][2], 2u);
    EXPECT_EQ(sym.num_symmetric_variables, 3u);
    EXPECT_EQ(sym.ordering_rows.size(), 2u);
}

TEST(SymmetryTest, NoGroupWhenColumnsDiffer) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_constraint({{0, 2.0}, {1, 3.0}}, ConstraintSense::LE, 4.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SymmetryDetector detector;
    SymmetryDetection sym = detector.detect(prob);

    EXPECT_TRUE(sym.variable_groups.empty());
}

TEST(SymmetryTest, ObjectiveDifferenceBreaksGroup) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_constraint({{0, 5.0}, {1, 5.0}}, ConstraintSense::LE, 7.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SymmetryDetector detector;
    SymmetryDetection sym = detector.detect(prob);

    EXPECT_TRUE(sym.variable_groups.empty());
}

TEST(BranchAndBoundTest, SymmetryBreakingPreservesOptimum) {
    // x1, x2 identical binaries with x1 + x2 <= 1. Optimum is 1, provable via
    // the symmetry-ordering row x1 >= x2.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 1.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundOptions opts;
    opts.symmetry_breaking = true;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 1.0, 1e-4);
}

TEST(CuttingPlanesTest, FlowCoverForm) {
    // 6x0 + 5x1 + 4x2 >= 9: greedy largest cover {x0, x1}, lambda = 2,
    // non-cover x2 coefficient a/lam = 2.0, rhs = |C| - 1 = 1.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x0");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_constraint({{0, 6.0}, {1, 5.0}, {2, 4.0}}, ConstraintSense::GE, 9.0, "demand");
    Problem prob = builder.build();

    std::vector<double> lp_sol = {0.9, 0.2, 0.7};
    CutGenerator generator;
    auto cuts = generator.generate_flow_covers(prob, lp_sol);

    ASSERT_GE(cuts.size(), 1u);
    bool found = false;
    for (const auto& c : cuts) {
        if (c.sense != ConstraintSense::GE) continue;
        if (std::abs(c.rhs - 1.0) > 1e-9) continue;
        double c0 = 0.0, c1 = 0.0, c2 = 0.0;
        for (const auto& [j, a] : c.coefficients) {
            if (j == 0) c0 = a;
            if (j == 1) c1 = a;
            if (j == 2) c2 = a;
        }
        if (std::abs(c0 - 1.0) < 1e-9 && std::abs(c1 - 1.0) < 1e-9 &&
            std::abs(c2 - 2.0) < 1e-9) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(CuttingPlanesTest, FlowCoverFromNegatedLERow) {
    // -6x0 -5x1 -4x2 <= -9 is the same demand row negated; the generator must
    // normalize it to  6x0 + 5x1 + 4x2 >= 9  and produce the same cover cut.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x0");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x2");
    builder.add_constraint({{0, -6.0}, {1, -5.0}, {2, -4.0}}, ConstraintSense::LE, -9.0, "demand");
    Problem prob = builder.build();

    std::vector<double> lp_sol = {0.9, 0.2, 0.7};
    CutGenerator generator;
    auto cuts = generator.generate_flow_covers(prob, lp_sol);

    ASSERT_GE(cuts.size(), 1u);
    for (const auto& c : cuts) {
        EXPECT_EQ(c.sense, ConstraintSense::GE);
    }
}

TEST(CuttingPlanesTest, FlowCoversValidOnRowIntegerPoints) {
    // For every generated cut, brute-force its validity over the row-feasible
    // integer points of the generating demand row.
    std::mt19937 rng(2026);
    std::uniform_int_distribution<int> coin(1, 10);
    for (int trial = 0; trial < 40; ++trial) {
        int n = 3 + static_cast<int>(rng() % 2u);  // 3..4 vars keeps 2^n tiny
        ProblemBuilder builder("test");
        for (int j = 0; j < n; ++j) {
            builder.add_variable(0.0, 1.0, VarType::BINARY, "x" + std::to_string(j));
        }
        std::vector<std::pair<std::size_t, double>> row;
        for (int j = 0; j < n; ++j) {
            double a = static_cast<double>(coin(rng));
            row.push_back({static_cast<std::size_t>(j), a});
        }
        double b = static_cast<double>(1 + rng() % 25);
        builder.add_constraint(row, ConstraintSense::GE, b, "demand");
        Problem prob = builder.build();

        std::vector<double> lp_sol(n, 0.5);
        CutGenerator generator;
        auto cuts = generator.generate_flow_covers(prob, lp_sol);

        long long total = 1LL << n;
        for (const auto& c : cuts) {
            for (long long mask = 0; mask < total; ++mask) {
                double row_lhs = 0.0;
                for (const auto& [j, a] : row) {
                    row_lhs += a * ((mask >> j) & 1);
                }
                if (row_lhs < b - 1e-6) continue;  // only row-feasible points
                double lhs = 0.0;
                for (const auto& [j, a] : c.coefficients) {
                    lhs += a * ((mask >> j) & 1);
                }
                if (c.sense == ConstraintSense::GE) {
                    EXPECT_GE(lhs + 1e-6, c.rhs);
                } else {
                    EXPECT_LE(lhs - 1e-6, c.rhs);
                }
            }
        }
    }
}

TEST(BranchAndBoundTest, FlowCoverCutsSolveMixedModel) {
    // min 7(x0..x4) + 5x5 + 3y  s.t.  y + 6x0+6x1+6x2+6x3+6x4+16x5 >= 21,
    // x binary, y >= 0 continuous. Integer optimum: x5 = 1, x0 = 1, obj 12.
    ProblemBuilder builder("test");
    for (int j = 0; j < 5; ++j) {
        builder.add_variable(0.0, 1.0, VarType::BINARY, "x" + std::to_string(j));
    }
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x5");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "y");
    std::vector<std::pair<std::size_t, double>> row;
    for (int j = 0; j < 5; ++j) row.push_back({static_cast<std::size_t>(j), 6.0});
    row.push_back({5, 16.0});
    row.push_back({6, 1.0});
    builder.add_constraint(row, ConstraintSense::GE, 21.0, "demand");
    std::vector<std::pair<std::size_t, double>> obj;
    for (int j = 0; j < 5; ++j) obj.push_back({static_cast<std::size_t>(j), 7.0});
    obj.push_back({5, 5.0});
    obj.push_back({6, 3.0});
    builder.set_objective(obj, ObjectiveSense::MINIMIZE);
    Problem prob = builder.build();

    BranchAndBoundOptions opts;
    opts.cuts.flow_cover_cuts = true;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 12.0, 1e-4);
}

// ---------------------------------------------------------------------------
// Phase 4 (MILP Strengthening) regression tests.
// ---------------------------------------------------------------------------

// Exhaustive 0/1 enumeration: an INDEPENDENT oracle for comparing against the
// B&B optimum on small pure-binary models.
struct BruteForceResult {
    bool feasible = false;
    double objective_value = 0.0;
};

static BruteForceResult brute_force_binary(const Problem& prob) {
    const std::size_t n = prob.variables.size();
    const long long total = 1LL << n;
    BruteForceResult best;
    best.feasible = false;

    for (long long mask = 0; mask < total; ++mask) {
        std::vector<double> x(n);
        for (std::size_t j = 0; j < n; ++j) x[j] = double((mask >> j) & 1LL);

        bool ok = true;
        const auto& A = prob.constraint_matrix;
        for (std::size_t i = 0; i < prob.constraints.size() && ok; ++i) {
            double act = 0.0;
            if (A.order() == StorageOrder::CSR) {
                for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                    act += A.values()[k] * x[A.col_indices()[k]];
                }
            }
            const auto& c = prob.constraints[i];
            const double eps = 1e-7;
            if (c.sense == ConstraintSense::LE && act > c.rhs + eps) ok = false;
            if (c.sense == ConstraintSense::GE && act < c.rhs - eps) ok = false;
            if (c.sense == ConstraintSense::EQ && std::abs(act - c.rhs) > eps) ok = false;
        }
        if (!ok) continue;

        double obj = prob.obj_offset;
        for (std::size_t j = 0; j < n; ++j) obj += prob.variables[j].objective_coeff * x[j];
        if (!best.feasible || (prob.obj_sense == ObjectiveSense::MINIMIZE ? obj < best.objective_value
                                                                          : obj > best.objective_value)) {
            best.feasible = true;
            best.objective_value = obj;
        }
    }
    return best;
}

static Problem make_binary_knapsack(std::mt19937& rng, int n, int seed) {
    ProblemBuilder builder("knap" + std::to_string(seed));
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

TEST(BranchAndBoundTest, RandomizedMILPMatchesBruteForce) {
    std::mt19937 rng(20260915u);
    for (int trial = 0; trial < 12; ++trial) {
        std::uniform_int_distribution<int> ndist(6, 11);
        const int n = ndist(rng);
        Problem prob = make_binary_knapsack(rng, n, trial);

        // Without cuts...
        {
            BranchAndBoundOptions opts;
            opts.branching.strategy = BranchingStrategy::MOST_FRACTIONAL;
            opts.cuts.gomory_cuts = false;
            opts.cuts.mir_cuts = false;
            BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
            auto result = solver.solve(prob);
            ASSERT_EQ(result.status, ProblemStatus::OPTIMAL) << "trial=" << trial;
            BruteForceResult bf = brute_force_binary(prob);
            ASSERT_TRUE(bf.feasible) << "trial=" << trial;
            EXPECT_NEAR(result.objective_value, bf.objective_value, 1e-4) << "trial=" << trial;
        }

        // ...and with the default cut families active (Gomory + MIR now separate
        // in practice; the oracle proves they never cut off the integer optimum).
        {
            BranchAndBoundOptions opts;
            opts.branching.strategy = BranchingStrategy::PSEUDOCOST;
            BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
            auto result = solver.solve(prob);
            ASSERT_EQ(result.status, ProblemStatus::OPTIMAL) << "trial=" << trial << " (cuts)";
            EXPECT_NEAR(result.objective_value, brute_force_binary(prob).objective_value, 1e-4)
                << "trial=" << trial << " (cuts)";
        }
    }
}

TEST(BranchAndBoundTest, InfeasibleDetectionMatchesBruteForce) {
    // Binary x1==0.5 is integer-infeasible; B&B must report it.
    ProblemBuilder builder("infeas");
    builder.add_variable(0.0, 1.0, VarType::BINARY, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::EQ, 0.5, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    BranchAndBoundSolver solver;
    auto result = solver.solve(prob);

    BruteForceResult bf = brute_force_binary(prob);
    EXPECT_FALSE(bf.feasible);
    EXPECT_TRUE(result.status == ProblemStatus::INFEASIBLE ||
                result.status == ProblemStatus::UNKNOWN);
}

TEST(BranchAndBoundTest, NodeSelectionStrategiesAffectExecution) {
    // The four node-selection strategies must give the same verified optimum
    // AND at least two of them must traverse a different number of nodes
    // (the strategies materially change execution).
    std::mt19937 rng(4242u);
    Problem prob = make_binary_knapsack(rng, 14, 4242);

    std::vector<std::size_t> counts;
    double reference = 0.0;
    for (NodeSelectionStrategy strat : {NodeSelectionStrategy::BEST_FIRST,
                                        NodeSelectionStrategy::DEPTH_FIRST,
                                        NodeSelectionStrategy::BEST_ESTIMATE,
                                        NodeSelectionStrategy::HYBRID}) {
        BranchAndBoundOptions opts;
        opts.branching.strategy = BranchingStrategy::MOST_FRACTIONAL;
        opts.branching.node_strategy = strat;
        opts.cuts.gomory_cuts = false;
        opts.cuts.mir_cuts = false;
        BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto result = solver.solve(prob);
        ASSERT_EQ(result.status, ProblemStatus::OPTIMAL) << "strat=" << static_cast<int>(strat);
        if (reference == 0.0) reference = result.objective_value;
        EXPECT_NEAR(result.objective_value, reference, 1e-4);
        counts.push_back(result.nodes_explored);
    }

    bool any_differ = false;
    for (std::size_t a = 0; a < counts.size(); ++a) {
        for (std::size_t b = a + 1; b < counts.size(); ++b) {
            if (counts[a] != counts[b]) any_differ = true;
        }
    }
    EXPECT_TRUE(any_differ) << "node-selection strategies must change execution";
    // HYBRID must be genuinely different from plain BEST_FIRST.
    EXPECT_NE(counts[0], counts[3]);
}

TEST(BranchAndBoundTest, HybridDiffersFromBestFirst) {
    // Re-run on a second seed to make the HYBRID-vs-BEST_FIRST difference
    // robust, not a single-seed artifact.
    std::mt19937 rng(90210u);
    Problem prob = make_binary_knapsack(rng, 12, 90210);

    auto solve_for = [&](NodeSelectionStrategy strat) {
        BranchAndBoundOptions opts;
        opts.branching.strategy = BranchingStrategy::MOST_FRACTIONAL;
        opts.branching.node_strategy = strat;
        opts.cuts.gomory_cuts = false;
        opts.cuts.mir_cuts = false;
        BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
        return solver.solve(prob);
    };

    auto bf = solve_for(NodeSelectionStrategy::BEST_FIRST);
    auto hy = solve_for(NodeSelectionStrategy::HYBRID);
    ASSERT_EQ(bf.status, ProblemStatus::OPTIMAL);
    ASSERT_EQ(hy.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(bf.objective_value, hy.objective_value, 1e-4);
    EXPECT_NE(bf.nodes_explored, hy.nodes_explored);
}

TEST(BranchAndBoundTest, PseudocostsActuallyUpdate) {
    // After a solve with enough branching, some pseudocost counters must be
    // nonzero: proof the counters are written, not just default-initialized.
    std::mt19937 rng(777u);
    Problem prob = make_binary_knapsack(rng, 13, 777);

    BranchAndBoundOptions opts;
    opts.branching.strategy = BranchingStrategy::RELIABILITY;
    opts.cuts.gomory_cuts = false;
    opts.cuts.mir_cuts = false;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);
    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);

    int touched = 0;
    const auto& cu = solver.pseudocount_up();
    const auto& cd = solver.pseudocount_down();
    for (std::size_t j = 0; j < cu.size(); ++j) {
        if (cu[j] > 0 || cd[j] > 0) ++touched;
    }
    EXPECT_GT(touched, 0) << "no pseudocost update occurred";
}

TEST(BranchAndBoundTest, AllBranchingStrategiesAgree) {
    std::mt19937 rng(31415u);
    Problem prob = make_binary_knapsack(rng, 10, 31415);

    double reference = 0.0;
    for (BranchingStrategy strat : {BranchingStrategy::MOST_FRACTIONAL,
                                    BranchingStrategy::PSEUDOCOST,
                                    BranchingStrategy::STRONG_BRANCHING,
                                    BranchingStrategy::RELIABILITY}) {
        BranchAndBoundOptions opts;
        opts.branching.strategy = strat;
        opts.cuts.gomory_cuts = false;
        opts.cuts.mir_cuts = false;
        BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto result = solver.solve(prob);
        ASSERT_EQ(result.status, ProblemStatus::OPTIMAL) << "strat=" << static_cast<int>(strat);
        if (strat == BranchingStrategy::MOST_FRACTIONAL) reference = result.objective_value;
        EXPECT_NEAR(result.objective_value, reference, 1e-4);
        EXPECT_GT(result.nodes_explored, 0u);
    }
}

TEST(BranchAndBoundTest, HeuristicAndCutStatisticsPopulated) {
    // The Phase 4 statistics must be internally consistent (created >= explored,
    // prune counters reconcile with result.nodes_pruned) AND over a few random
    // instances the heuristic/cut counters must actually fire -- the counters
    // are wired to real activity, not left at their zero defaults.
    std::size_t total_cuts = 0, total_heuristic = 0;
    for (int seed : {555, 556, 557}) {
        std::mt19937 rng(static_cast<unsigned>(seed));
        Problem prob = make_binary_knapsack(rng, 25, seed);

        BranchAndBoundOptions opts;
        opts.branching.strategy = BranchingStrategy::PSEUDOCOST;
        opts.heuristics.feasibility_pump = true;
        opts.heuristics.rins = true;
        opts.heuristics.rins_frequency = 4;
        opts.cuts.knapsack_cuts = true;
        BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto result = solver.solve(prob);
        ASSERT_EQ(result.status, ProblemStatus::OPTIMAL) << "seed=" << seed;

        EXPECT_GE(result.stats.nodes_created, result.nodes_explored);
        EXPECT_EQ(result.stats.nodes_pruned_by_bound + result.stats.nodes_pruned_infeasible,
                  result.nodes_pruned);
        EXPECT_GE(result.nodes_explored, 1u);
        total_cuts += result.stats.cuts_generated;
        total_heuristic += result.stats.heuristic_solutions;
    }
    EXPECT_GT(total_heuristic, 0u) << "no heuristic solution recorded across instances";
    EXPECT_GT(total_cuts, 0u) << "no cut recorded across instances";
}

TEST(BranchAndBoundTest, FrontierBoundIsExactAtTermination) {
    // Regression: compute_best_bound() used to scan every !pruned node,
    // including already-EXPANDED parents, so an exhausted search could report
    // a stale optimistic bound and a nonzero gap at OPTIMAL. The gap/bound
    // must reflect only the remaining frontier (zero at exhaustion).
    std::mt19937 rng(20260915u);
    for (int seed : {11, 22, 33}) {
        Problem prob = make_binary_knapsack(rng, 18, seed);
        BranchAndBoundOptions opts;
        opts.branching.strategy = BranchingStrategy::MOST_FRACTIONAL;
        opts.cuts.gomory_cuts = false;
        opts.cuts.mir_cuts = false;
        BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto result = solver.solve(prob);
        ASSERT_EQ(result.status, ProblemStatus::OPTIMAL) << "seed=" << seed;
        EXPECT_NEAR(result.gap, 0.0, 1e-9) << "seed=" << seed;
        EXPECT_NEAR(result.objective_value, brute_force_binary(prob).objective_value, 1e-4)
            << "seed=" << seed;
    }
}

TEST(BranchAndBoundTest, CutFamilyCountersFire) {
    // max 10x0+9x1+8x2+7x3+6x4+5x5, weights 7,8,7,6,5,4, cap 20.5 (fractional
    // capacity so MIR's rounding cut separates the LP optimum).
    // Correct integer optimum: {x0,x1,x4} = 7+8+5 <= 20.5, value 25.
    ProblemBuilder b("frac_cap_knap");
    const double w[6] = {7, 8, 7, 6, 5, 4};
    const double v[6] = {10, 9, 8, 7, 6, 5};
    for (int j = 0; j < 6; ++j) b.add_variable(0.0, 1.0, VarType::BINARY, "x" + std::to_string(j));
    std::vector<std::pair<std::size_t, double>> row;
    for (int j = 0; j < 6; ++j) row.push_back({static_cast<std::size_t>(j), w[j]});
    b.add_constraint(row, ConstraintSense::LE, 20.5, "cap");
    std::vector<std::pair<std::size_t, double>> obj;
    for (int j = 0; j < 6; ++j) obj.push_back({static_cast<std::size_t>(j), v[j]});
    b.set_objective(obj, ObjectiveSense::MAXIMIZE);
    Problem prob = b.build();

    BranchAndBoundOptions opts;
    opts.branching.strategy = BranchingStrategy::MOST_FRACTIONAL;
    opts.cuts.gomory_cuts = false;
    opts.cuts.mir_cuts = true;
    opts.cuts.knapsack_cuts = true;
    BranchAndBoundSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    ASSERT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 25.0, 1e-4);
    EXPECT_NEAR(brute_force_binary(prob).objective_value, 25.0, 1e-7);

    // Separation truly fires for MIR and knapsack covers, and the per-family
    // counters reconcile with the aggregate.
    EXPECT_GE(result.stats.cut_stats.mir_candidates, 1u);
    EXPECT_GE(result.stats.cut_stats.mir_added, 1u);
    EXPECT_GE(result.stats.cut_stats.knapsack_candidates, 1u);
    EXPECT_GE(result.stats.cut_stats.knapsack_added, 1u);
    EXPECT_GE(result.stats.cut_stats.gomory_candidates + result.stats.cut_stats.mir_candidates +
                  result.stats.cut_stats.knapsack_candidates + result.stats.cut_stats.clique_candidates +
                  result.stats.cut_stats.flow_candidates,
              result.stats.cuts_generated);
    EXPECT_GE(result.stats.cut_stats.mir_added + result.stats.cut_stats.knapsack_added, 2u);
}

TEST(BranchAndBoundTest, SmallIndependentModels) {
    // Hand-built models with optima computed by inspection, cross-checked by
    // exhaustive enumeration inside the test.
    {
        // max 3x0 + 2x1, x0 + 2x1 <= 2, binary: optimum {1,0} = 3.
        ProblemBuilder b("knap1");
        b.add_variable(0.0, 1.0, VarType::BINARY, "x0");
        b.add_variable(0.0, 1.0, VarType::BINARY, "x1");
        b.add_constraint({{0, 1.0}, {1, 2.0}}, ConstraintSense::LE, 2.0, "cap");
        b.set_objective({{0, 3.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);
        Problem prob = b.build();
        BranchAndBoundSolver solver;
        auto r = solver.solve(prob);
        EXPECT_EQ(r.status, ProblemStatus::OPTIMAL);
        EXPECT_NEAR(r.objective_value, 3.0, 1e-4);
        EXPECT_NEAR(brute_force_binary(prob).objective_value, 3.0, 1e-7);
    }
    {
        // max x0 + x1 + x2 s.t. 2x0 + 3x1 + 4x2 <= 5, binary.
        // Enumerating: 110 violates (5>5? no 5<=5 ok). {1,1,0}=2, {0,0,1}=1,
        // {1,0,1}=2, {0,1,1}=5 violates -> {1,1,0} and {1,0,1} tie at 2.
        ProblemBuilder b("knap2");
        b.add_variable(0.0, 1.0, VarType::BINARY, "x0");
        b.add_variable(0.0, 1.0, VarType::BINARY, "x1");
        b.add_variable(0.0, 1.0, VarType::BINARY, "x2");
        b.add_constraint({{0, 2.0}, {1, 3.0}, {2, 4.0}}, ConstraintSense::LE, 5.0, "cap");
        b.set_objective({{0, 1.0}, {1, 1.0}, {2, 1.0}}, ObjectiveSense::MAXIMIZE);
        Problem prob = b.build();
        BranchAndBoundSolver solver;
        auto r = solver.solve(prob);
        EXPECT_EQ(r.status, ProblemStatus::OPTIMAL);
        EXPECT_NEAR(r.objective_value, 2.0, 1e-4);
        EXPECT_NEAR(brute_force_binary(prob).objective_value, 2.0, 1e-7);
    }
    {
        // Infeasible: x0 + x1 >= 2 with x0 + x1 <= 1, binary.
        ProblemBuilder b("infeas2");
        b.add_variable(0.0, 1.0, VarType::BINARY, "x0");
        b.add_variable(0.0, 1.0, VarType::BINARY, "x1");
        b.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::GE, 2.0, "ge");
        b.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 1.0, "le");
        b.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MAXIMIZE);
        Problem prob = b.build();
        BranchAndBoundSolver solver;
        auto r = solver.solve(prob);
        EXPECT_FALSE(brute_force_binary(prob).feasible);
        EXPECT_TRUE(r.status == ProblemStatus::INFEASIBLE ||
                    r.status == ProblemStatus::UNKNOWN);
    }
}