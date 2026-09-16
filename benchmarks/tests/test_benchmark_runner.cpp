#include "benchmark_runner.hpp"
#include "benchmark_report.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

using namespace hypernova::benchmark;

class BenchmarkRunnerTest : public ::testing::Test {
protected:
    std::filesystem::path tmp;

    void SetUp() override {
        tmp = std::filesystem::temp_directory_path() / "hypernova_bench_test";
        std::filesystem::remove_all(tmp);
        std::filesystem::create_directories(tmp);
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp);
    }

    void write_file(const std::string& rel, const std::string& content) {
        std::ofstream out(tmp / rel);
        ASSERT_TRUE(out.is_open()) << rel;
        out << content;
    }

    BenchmarkConfig default_config() const {
        BenchmarkConfig c;
        c.use_builtin_netlib_reference = false;
        c.time_limit_seconds = 5.0;
        return c;
    }
};

TEST_F(BenchmarkRunnerTest, SolvesKnownInstances) {
    // MAX 3x1+5x2, x1+x2<=10, 2x1+x2<=14, 0<=xi<=10  → opt 50 at (0,10)
    write_file("lp_max50.lp", R"(Maximize
 OBJ: 3 x1 + 5 x2
Subject To
 c1: x1 + x2 <= 10
 c2: 2 x1 + x2 <= 14
Bounds
 0 <= x1 <= 10
 0 <= x2 <= 10
End
)");

    // MIN -4x1-5x2+0.5x1^2+0.5x2^2, x1+x2<=4, 0<=xi<=10 → opt -14.25 at (1.5,2.5)
    write_file("qp_m14p25.mps", R"(NAME          qp_test
ROWS
 L  c1
COLUMNS
    x1  OBJ       -4
    x1  c1  1
    x2  OBJ       -5
    x2  c1  1
RHS
    RHS1      c1  4
BOUNDS
 UP BND1      x1  10
 UP BND1      x2  10
QUADOBJ
    x1 x1  1
    x2 x2  1
ENDATA
)");

    // MAX 5x1+4x2, x1+x2<=1, binary → opt 5 at (1,0)
    write_file("milp_max5.mps", R"(NAME          milp_test
OBJSENSE
    MAX
ROWS
 L  c1
COLUMNS
    x1  OBJ       5
    x1  c1  1
    x2  OBJ       4
    x2  c1  1
RHS
    RHS1      c1  1
BOUNDS
 BV BND1      x1
 BV BND1      x2
ENDATA
)");

    BenchmarkConfig config = default_config();
    config.files = {(tmp / "lp_max50.lp").string(),
                    (tmp / "qp_m14p25.mps").string(),
                    (tmp / "milp_max5.mps").string()};

    SuiteReport report = run_benchmark(config);

    ASSERT_EQ(report.total, 3u);
    EXPECT_EQ(report.num_optimal, 3u);
    EXPECT_EQ(report.num_pass, 3u);
    EXPECT_TRUE(report.all_pass);

    double obj_lp = 0.0;
    double obj_qp = 0.0;
    double obj_milp = 0.0;
    for (const auto& r : report.instances) {
        if (r.name == "lp_max50") obj_lp = r.objective;
        else if (r.name == "qp_m14p25") obj_qp = r.objective;
        else if (r.name == "milp_max5") obj_milp = r.objective;
    }
    EXPECT_NEAR(obj_lp, 50.0, 1e-4);
    EXPECT_NEAR(obj_qp, -14.25, 1e-4);
    EXPECT_NEAR(obj_milp, 5.0, 1e-4);
}

TEST_F(BenchmarkRunnerTest, DirectoryScan) {
    write_file("scan1.mps", R"(NAME scan1
ROWS
 L  c1
COLUMNS
    x1  OBJ  1
    x1  c1  1
RHS
    RHS1 c1  10
BOUNDS
 LO BND1 x1 0
 UP BND1 x1 20
ENDATA
)");
    write_file("scan2.lp", R"(Maximize
 OBJ: 7 x1
Subject To
 c1: x1 <= 10
Bounds
 0 <= x1
End
)");
    BenchmarkConfig config = default_config();
    config.directory = tmp.string();

    SuiteReport report = run_benchmark(config);
    // At least the two files we wrote should be present.
    EXPECT_GE(report.total, 2u);
    EXPECT_TRUE(report.all_pass);
}

TEST_F(BenchmarkRunnerTest, ReferenceGradesCorrectly) {
    // MIN 0, x<=50, lb=0 → opt 0
    write_file("tiny.mps", R"(NAME tiny
ROWS
 L  c1
COLUMNS
    x1  c1  1
RHS
    RHS1 c1  50
BOUNDS
 LO BND1 x1 0
 UP BND1 x1 100
ENDATA
)");

    write_file("ref_ok.csv", "name,status,objective\ntiny,min,0.0\n");
    write_file("ref_bad.csv", "name,status,objective\ntiny,min,0.1\n");

    BenchmarkConfig cfg_ok = default_config();
    cfg_ok.files = {(tmp / "tiny.mps").string()};
    cfg_ok.reference_csv = (tmp / "ref_ok.csv").string();
    SuiteReport rep_ok = run_benchmark(cfg_ok);
    ASSERT_EQ(rep_ok.total, 1u);
    EXPECT_TRUE(rep_ok.instances[0].pass);

    BenchmarkConfig cfg_bad = default_config();
    cfg_bad.files = {(tmp / "tiny.mps").string()};
    cfg_bad.reference_csv = (tmp / "ref_bad.csv").string();
    SuiteReport rep_bad = run_benchmark(cfg_bad);
    ASSERT_EQ(rep_bad.total, 1u);
    EXPECT_FALSE(rep_bad.instances[0].pass);
    EXPECT_NE(rep_bad.instances[0].message.find("not matched"), std::string::npos);
}

TEST_F(BenchmarkRunnerTest, ParseFailureYieldsFail) {
    BenchmarkConfig config = default_config();
    config.files = {(tmp / "does_not_exist.mps").string()};

    SuiteReport report = run_benchmark(config);
    ASSERT_EQ(report.total, 1u);
    EXPECT_FALSE(report.instances[0].pass);
    EXPECT_EQ(report.instances[0].problem_type, "parse-failed");
}