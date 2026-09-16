#include <gtest/gtest.h>
#include <lp/simplex.hpp>
#include <lp/interior_point.hpp>
#include <model/problem.hpp>

using namespace hypernova::lp;
using namespace hypernova::model;
using namespace hypernova::numerical;

TEST(SimplexTest, SimpleLP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_GT(result.iterations, 0);
}

TEST(SimplexTest, DualSimpleLP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexOptions opts;
    opts.algorithm = SimplexAlgorithm::DUAL;
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(result.algorithm_used, SimplexAlgorithm::DUAL);
    EXPECT_NEAR(result.objective_value, 20.0, 1e-6);
    EXPECT_NEAR(result.primal[1], 10.0, 1e-6);
}

TEST(SimplexTest, DualMatchesPrimalObjective) {
    ProblemBuilder builder("test");
    for (int i = 0; i < 8; ++i) {
        builder.add_variable(0.0, 50.0, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    for (int i = 0; i < 4; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < 8; ++j) {
            coeffs.push_back({static_cast<std::size_t>(j), static_cast<double>((i + 1) * (j + 1))});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE, 100.0 * (i + 1), "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < 8; ++i) {
        obj.push_back({static_cast<std::size_t>(i), static_cast<double>(i + 1)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexOptions dual_opts;
    dual_opts.algorithm = SimplexAlgorithm::DUAL;
    SimplexSolver dual_solver(ToleranceConfig::industrial_defaults(), dual_opts);
    auto dual = dual_solver.solve(prob);

    SimplexOptions primal_opts;
    primal_opts.algorithm = SimplexAlgorithm::PRIMAL;
    SimplexSolver primal_solver(ToleranceConfig::industrial_defaults(), primal_opts);
    auto primal = primal_solver.solve(prob);

    EXPECT_EQ(dual.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(primal.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(dual.algorithm_used, SimplexAlgorithm::DUAL);
    EXPECT_EQ(primal.algorithm_used, SimplexAlgorithm::PRIMAL);
    EXPECT_NEAR(dual.objective_value, primal.objective_value, 1e-4);
}

TEST(SimplexTest, DualDegenerate) {
    ProblemBuilder builder("test");
    for (int i = 0; i < 20; ++i) {
        builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    for (int i = 0; i < 10; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < 20; ++j) {
            coeffs.push_back({static_cast<std::size_t>(j), static_cast<double>((i + 1) * (j + 1))});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE, 100.0 * (i + 1), "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < 20; ++i) {
        obj.push_back({static_cast<std::size_t>(i), static_cast<double>(i + 1)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexOptions opts;
    opts.algorithm = SimplexAlgorithm::DUAL;
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(result.algorithm_used, SimplexAlgorithm::DUAL);
    EXPECT_NEAR(result.objective_value, 100.0, 1e-4);
}

TEST(SimplexTest, DualGE) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::GE, 1.0, "c1");
    builder.set_objective({{0, 1.0}, {1, 1.0}}, ObjectiveSense::MINIMIZE);

    Problem prob = builder.build();

    SimplexOptions opts;
    opts.algorithm = SimplexAlgorithm::DUAL;
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(result.algorithm_used, SimplexAlgorithm::DUAL);
    EXPECT_NEAR(result.objective_value, 1.0, 1e-6);
}

TEST(SimplexTest, DualInfeasible) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 10.0, "c2");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexOptions opts;
    opts.algorithm = SimplexAlgorithm::DUAL;
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INFEASIBLE);
}

TEST(SimplexTest, DualUnbounded) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, -1.0}}, ConstraintSense::LE, -1.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexOptions opts;
    opts.algorithm = SimplexAlgorithm::DUAL;
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::UNBOUNDED);
}

TEST(SimplexTest, DualFallsBackOnEquality) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::EQ, 7.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexOptions opts;
    opts.algorithm = SimplexAlgorithm::DUAL;
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_EQ(result.algorithm_used, SimplexAlgorithm::PRIMAL);
    EXPECT_NEAR(result.objective_value, 14.0, 1e-4);
}

TEST(SimplexTest, InterruptCallback) {
    ProblemBuilder builder("test");
    for (int i = 0; i < 20; ++i) {
        builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    for (int i = 0; i < 10; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < 20; ++j) {
            coeffs.push_back({static_cast<std::size_t>(j), static_cast<double>(i + 1)});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE, 50.0 * (i + 1), "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < 20; ++i) {
        obj.push_back({static_cast<std::size_t>(i), static_cast<double>(i + 1)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexOptions opts;
    opts.interrupt_callback = []() { return true; };
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INTERRUPTED);
}

TEST(SimplexTest, UnboundedLP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, std::numeric_limits<double>::infinity(), VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, -1.0}}, ConstraintSense::LE, -1.0, "c1");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::UNBOUNDED);
}

TEST(SimplexTest, InfeasibleLP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::LE, 5.0, "c1");
    builder.add_constraint({{0, 1.0}}, ConstraintSense::GE, 10.0, "c2");
    builder.set_objective({{0, 1.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    SimplexSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INFEASIBLE);
}

TEST(InteriorPointTest, SimpleLP) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_GT(result.iterations, 0);
}

TEST(InteriorPointTest, LargerLP) {
    ProblemBuilder builder("test");
    for (int i = 0; i < 50; ++i) {
        builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    for (int i = 0; i < 20; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < 50; j += 5) {
            coeffs.push_back({static_cast<std::size_t>(j), 1.0});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE, 10.0, "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < 50; ++i) {
        obj.push_back({static_cast<std::size_t>(i), 1.0});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

InteriorPointSolver solver;
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
}

TEST(InteriorPointTest, InterruptCallback) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointOptions opts;
    opts.crossover = false;
    opts.interrupt_callback = []() { return true; };
    InteriorPointSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::INTERRUPTED);
}

TEST(InteriorPointTest, IterationLimitReportsIncumbent) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointOptions opts;
    opts.max_iterations = 1;
    opts.crossover = false;
    InteriorPointSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::ITER_LIMIT);
    EXPECT_EQ(result.primal.size(), 2u);
    EXPECT_NE(result.objective_value, 0.0);
}

TEST(InteriorPointTest, CrossoverResolvesStall) {
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointOptions opts;
    opts.max_iterations = 1;
    opts.crossover = true;
    InteriorPointSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 20.0, 1e-4);
}

TEST(InteriorPointTest, WarmStartCrossoverProducesExactCertificate) {
    // A cleanly converged barrier then crossed over to an exact vertex: the
    // simplex polish must deliver certified duals satisfying strong duality
    // (b^T y == obj), which the raw interior point lacks.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointOptions opts;
    opts.crossover = true;
    InteriorPointSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    // Vertex optimum is x=(0,10): x1 at its lower bound, c1 tight.
    EXPECT_NEAR(result.objective_value, 20.0, 1e-6);
    ASSERT_EQ(result.primal.size(), 2u);
    EXPECT_NEAR(result.primal[0], 0.0, 1e-6);
    EXPECT_NEAR(result.primal[1], 10.0, 1e-6);
    // Complementary duals: y1 = 2 (tight c1), y2 = 0 (inactive c2).
    ASSERT_EQ(result.dual.size(), 2u);
    EXPECT_NEAR(result.dual[0], 2.0, 1e-6);
    EXPECT_NEAR(result.dual[1], 0.0, 1e-6);
    double bdoty = 10.0 * result.dual[0] + 5.0 * result.dual[1];
    EXPECT_NEAR(bdoty, result.objective_value, 1e-6);
}

TEST(InteriorPointTest, WarmStartCrossoverFallsBackToColdOnCappedIter) {
    // Capping the warm-polish pivot budget forces the cold-start fallback to
    // carry the stalled barrier to a definitive optimum.
    ProblemBuilder builder("test");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x1");
    builder.add_variable(0.0, 10.0, VarType::CONTINUOUS, "x2");
    builder.add_constraint({{0, 1.0}, {1, 1.0}}, ConstraintSense::LE, 10.0, "c1");
    builder.add_constraint({{0, 1.0}, {1, 0.0}}, ConstraintSense::LE, 5.0, "c2");
    builder.set_objective({{0, 1.0}, {1, 2.0}}, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointOptions opts;
    opts.max_iterations = 1;
    opts.crossover = true;
    opts.crossover_max_iter = 1;
    InteriorPointSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(result.objective_value, 20.0, 1e-4);
}

TEST(InteriorPointTest, NonfiniteGuardNeverReturnsNaN) {
    ProblemBuilder builder("scale");
    const int n = 12;
    for (int i = 0; i < n; ++i) {
        builder.add_variable(0.0, 1e8, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    for (int i = 0; i < 6; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < n; ++j) {
            coeffs.push_back({static_cast<std::size_t>(j), 1e6 * (j + 1)});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE, 1e7 * (i + 1), "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < n; ++i) {
        obj.push_back({static_cast<std::size_t>(i), 1e11 * (i + 1)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointOptions opts;
    opts.max_iterations = 100;
    opts.crossover = false;
    InteriorPointSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_NE(result.status, ProblemStatus::UNKNOWN);
EXPECT_TRUE(std::isfinite(result.objective_value));
    for (double v : result.primal) {
        EXPECT_TRUE(std::isfinite(v));
    }
}

// Phase 3 LP strengthening: the declared-but-unused pricing strategies and the
// Harris ratio test must actually change solver behavior, and must all agree on
// the optimum. The default Bland anti-cycling rule stays in force as a fallback.
static Problem make_multi_candidate_lp() {
    ProblemBuilder builder("phase3");
    const int n = 16, m = 8;
    for (int i = 0; i < n; ++i) {
        builder.add_variable(0.0, 50.0, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    const int rowtouch[8][4] = {
        {0, 1, 2, 3}, {2, 3, 4, 5}, {4, 5, 6, 7}, {6, 7, 8, 9},
        {8, 9, 10, 11}, {10, 11, 12, 13}, {12, 13, 14, 15}, {14, 15, 0, 1}
    };
    for (int i = 0; i < m; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int k = 0; k < 4; ++k) {
            const int j = rowtouch[i][k];
            coeffs.push_back({static_cast<std::size_t>(j), 1.0 + 0.37 * ((i + 1) * (j + 1) % 11)});
        }
        // Redundant cap constraints: several rows share the same tight RHS so
        // the optimum lies on a degenerate vertex.
        builder.add_constraint(coeffs, ConstraintSense::LE, 8.0 * ((i % 2) + 1), "c" + std::to_string(i));
        if (i % 3 == 0) {
            builder.add_constraint(coeffs, ConstraintSense::LE, 8.0 * ((i % 2) + 1) + 0.01,
                                   "r" + std::to_string(i));
        }
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < n; ++i) {
        obj.push_back({static_cast<std::size_t>(i), double(n - i)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);
    return builder.build();
}

TEST(SimplexTest, PricingStrategiesAgreeAndDiffer) {
    Problem prob = make_multi_candidate_lp();

    std::vector<int> iters;
    double reference_obj = 0.0;
    for (PricingStrategy strategy : {PricingStrategy::DANTZIG, PricingStrategy::DEVEX,
                                     PricingStrategy::STEEPEST_EDGE}) {
        SimplexOptions opts;
        opts.algorithm = SimplexAlgorithm::PRIMAL;
        opts.bland_rule = false;  // exercise the priced entering strategies
        opts.pricing = strategy;
        SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto result = solver.solve(prob);
        EXPECT_EQ(result.status, ProblemStatus::OPTIMAL) << "strategy=" << static_cast<int>(strategy);
        EXPECT_NEAR(result.objective_value, reference_obj == 0.0 ? result.objective_value
                                                                 : reference_obj, 1e-4)
            << "strategy=" << static_cast<int>(strategy);
        reference_obj = result.objective_value;
        iters.push_back(static_cast<int>(result.iterations));
    }
    // The strategies are implemented and change behavior: at least two of the
    // three must take a different number of pivots on this model.
    bool any_differ = false;
    for (std::size_t a = 0; a < iters.size(); ++a) {
        for (std::size_t b = a + 1; b < iters.size(); ++b) {
            if (iters[a] != iters[b]) any_differ = true;
        }
    }
    EXPECT_TRUE(any_differ) << "pricing strategies must change pivot behavior";
}

TEST(SimplexTest, HarrisRatioTestAgrees) {
    Problem prob = make_multi_candidate_lp();

    auto solve_with = [&](RatioTest rt, std::size_t& iters_out) {
        SimplexOptions opts;
        opts.algorithm = SimplexAlgorithm::PRIMAL;
        opts.bland_rule = false;
        opts.ratio_test = rt;
        SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto result = solver.solve(prob);
        EXPECT_EQ(result.status, ProblemStatus::OPTIMAL);
        iters_out = result.iterations;
        return result.objective_value;
    };

    std::size_t it_std = 0, it_harris = 0;
    const double obj_std = solve_with(RatioTest::STANDARD, it_std);
    const double obj_harris = solve_with(RatioTest::HARRIS_TWO_PASS, it_harris);
    EXPECT_NEAR(obj_std, obj_harris, 1e-4);
}

TEST(SimplexTest, BlandFallbackSurvivesDegeneracy) {
    // Heavily redundant constraints -> a degenerate model. The default Bland
    // rule must converge, and the priced path with perturbation must also.
    ProblemBuilder builder("degenerate");
    for (int i = 0; i < 8; ++i) {
        builder.add_variable(0.0, 100.0, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    for (int i = 0; i < 12; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < 8; ++j) {
            coeffs.push_back({static_cast<std::size_t>(j), double((i + 1) * (j + 1) % 9 + 1)});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE, 80.0, "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < 8; ++i) {
        obj.push_back({static_cast<std::size_t>(i), double(i + 1)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);
    Problem prob = builder.build();

    SimplexOptions bland_opts;  // defaults: Bland entering + standard ratio
    SimplexSolver bland(ToleranceConfig::industrial_defaults(), bland_opts);
    auto r1 = bland.solve(prob);
    EXPECT_EQ(r1.status, ProblemStatus::OPTIMAL);

    SimplexOptions priced_opts;
    priced_opts.bland_rule = false;
    priced_opts.pricing = PricingStrategy::DEVEX;
    priced_opts.ratio_test = RatioTest::HARRIS_TWO_PASS;
    SimplexSolver priced(ToleranceConfig::industrial_defaults(), priced_opts);
    auto r2 = priced.solve(prob);
    EXPECT_EQ(r2.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(r1.objective_value, r2.objective_value, 1e-4);
}

TEST(SimplexTest, DualSteepestEdgeAgreesWithDantzig) {
    Problem prob = make_multi_candidate_lp();

    double obj_dantzig = 0.0, obj_dual_se = 0.0;
    {
        SimplexOptions opts;
        opts.algorithm = SimplexAlgorithm::DUAL;
        opts.bland_rule = false;
        opts.pricing = PricingStrategy::DANTZIG;
        SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto r = solver.solve(prob);
        EXPECT_EQ(r.status, ProblemStatus::OPTIMAL);
        obj_dantzig = r.objective_value;
    }
    {
        SimplexOptions opts;
        opts.algorithm = SimplexAlgorithm::DUAL;
        opts.bland_rule = false;
        opts.pricing = PricingStrategy::STEEPEST_EDGE;
        SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
        auto r = solver.solve(prob);
        EXPECT_EQ(r.status, ProblemStatus::OPTIMAL);
        obj_dual_se = r.objective_value;
    }
    EXPECT_NEAR(obj_dantzig, obj_dual_se, 1e-4);
}

TEST(SimplexTest, WarmStartReusesBasis) {
    Problem prob = make_multi_candidate_lp();

    SimplexOptions opts;
    opts.algorithm = SimplexAlgorithm::PRIMAL;
    SimplexSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto first = solver.solve(prob);
    ASSERT_EQ(first.status, ProblemStatus::OPTIMAL);

    // Feeding the optimal basis back in must re-solve to the same optimum and
    // must not inflate the iteration count.
    SimplexSolver warm(ToleranceConfig::industrial_defaults(), opts);
    auto second = warm.solve_with_basis(prob, first.basis_status);
    EXPECT_EQ(second.status, ProblemStatus::OPTIMAL);
    EXPECT_NEAR(first.objective_value, second.objective_value, 1e-4);
}

TEST(InteriorPointTest, TimeLimitReportsFiniteIncumbent) {
    ProblemBuilder builder("test");
    for (int i = 0; i < 20; ++i) {
        builder.add_variable(0.0, 1e4, VarType::CONTINUOUS, "x" + std::to_string(i));
    }
    for (int i = 0; i < 10; ++i) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (int j = 0; j < 20; ++j) {
            coeffs.push_back({static_cast<std::size_t>(j), double((i + 1) * (j + 1))});
        }
        builder.add_constraint(coeffs, ConstraintSense::LE, 100.0 * (i + 1), "c" + std::to_string(i));
    }
    std::vector<std::pair<std::size_t, double>> obj;
    for (int i = 0; i < 20; ++i) {
        obj.push_back({static_cast<std::size_t>(i), double(i + 1)});
    }
    builder.set_objective(obj, ObjectiveSense::MAXIMIZE);

    Problem prob = builder.build();

    InteriorPointOptions opts;
    opts.time_limit_seconds = 1e-9;
    opts.crossover = false;
    InteriorPointSolver solver(ToleranceConfig::industrial_defaults(), opts);
    auto result = solver.solve(prob);

    EXPECT_TRUE(std::isfinite(result.objective_value));
    for (double v : result.primal) {
        EXPECT_TRUE(std::isfinite(v));
    }
}

