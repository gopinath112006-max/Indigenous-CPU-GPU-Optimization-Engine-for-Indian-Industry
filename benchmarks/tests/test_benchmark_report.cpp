#include "benchmark_report.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace hypernova;
using namespace hypernova::benchmark;

TEST(ShiftedGeometricMean, SingleValue) {
    EXPECT_NEAR(shifted_geometric_mean({1.0}, 10.0), 1.0, 1e-12);
}

TEST(ShiftedGeometricMean, Empty) {
    EXPECT_NEAR(shifted_geometric_mean({}, 10.0), 0.0, 1e-12);
}

TEST(ShiftedGeometricMean, TwoValues) {
    double expected = std::exp((std::log(11.0) + std::log(12.0)) / 2.0) - 10.0;
    EXPECT_NEAR(shifted_geometric_mean({1.0, 2.0}, 10.0), expected, 1e-10);
}

TEST(ShiftedGeometricMean, IgnorInfNan) {
    double expected = std::exp(std::log(12.0)) - 10.0;
    double actual = shifted_geometric_mean({2.0, std::numeric_limits<double>::quiet_NaN()}, 10.0);
    EXPECT_NEAR(actual, expected, 1e-10);
}

TEST(Summarize, CountsAndPass) {
    InstanceResult a;
    a.status = model::ProblemStatus::OPTIMAL;
    a.verified = true;
    a.pass = true;

    InstanceResult b;
    b.status = model::ProblemStatus::OPTIMAL;
    b.verified = true;
    b.pass = true;

    InstanceResult c;
    c.status = model::ProblemStatus::INFEASIBLE;
    c.pass = true;

    SuiteReport report;
    report.instances = {a, b, c};
    report.summarize();

    EXPECT_EQ(report.total, 3u);
    EXPECT_EQ(report.num_optimal, 2u);
    EXPECT_EQ(report.num_infeasible, 1u);
    EXPECT_EQ(report.num_unbounded, 0u);
    EXPECT_EQ(report.num_other, 0u);
    EXPECT_TRUE(report.all_pass);
}

TEST(Summarize, FailIfAnyFail) {
    InstanceResult a;
    a.status = model::ProblemStatus::OPTIMAL;
    a.pass = true;
    InstanceResult b;
    b.status = model::ProblemStatus::OPTIMAL;
    b.pass = false;

    SuiteReport report;
    report.instances = {a, b};
    report.summarize();

    EXPECT_FALSE(report.all_pass);
    EXPECT_EQ(report.num_pass, 1u);
}

TEST(InstanceResult, CsvRowContainsFields) {
    InstanceResult r;
    r.name = "afiro";
    r.file = "afiro.mps";
    r.problem_type = "LP";
    r.status = model::ProblemStatus::OPTIMAL;
    r.objective = -464.753142;
    r.pass = true;
    r.engine_used = "simplex";
    r.iterations = 50;
    r.verified = true;
    r.has_reference = true;
    r.reference_status = "min";

    std::string row = r.to_csv_row();
    EXPECT_NE(row.find("afiro"), std::string::npos);
    EXPECT_NE(row.find("LP"), std::string::npos);
    EXPECT_NE(row.find("OPTIMAL"), std::string::npos);
    EXPECT_NE(row.find("PASS"), std::string::npos);
    EXPECT_NE(row.find("simplex"), std::string::npos);
}

TEST(SuiteReport, ToJsonContainsInstances) {
    InstanceResult r;
    r.name = "test";
    r.status = model::ProblemStatus::OPTIMAL;
    r.pass = true;

    SuiteReport report;
    report.suite_name = "unit";
    report.instances = {r};
    report.summarize();

    std::string json_str = report.to_json().dump();
    EXPECT_NE(json_str.find("\"suite_name\""), std::string::npos);
    EXPECT_NE(json_str.find("\"test\""), std::string::npos);
    EXPECT_NE(json_str.find("\"all_pass\""), std::string::npos);
}

TEST(SuiteReport, ToCsvCountsRows) {
    InstanceResult r1;
    r1.status = model::ProblemStatus::OPTIMAL;
    r1.pass = true;
    InstanceResult r2;
    r2.status = model::ProblemStatus::INFEASIBLE;
    r2.pass = true;

    SuiteReport report;
    report.instances = {r1, r2};
    report.summarize();

    std::string csv = report.to_csv();
    // Header + 2 data rows.
    std::istringstream lines(csv);
    int count = 0;
    std::string line;
    while (std::getline(lines, line)) {
        ++count;
    }
    EXPECT_EQ(count, 3); // header + 2
}