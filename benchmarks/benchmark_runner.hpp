#pragma once

#include "benchmark_report.hpp"
#include <hypernova/api.hpp>
#include <string>
#include <vector>

namespace hypernova::benchmark {

struct BenchmarkConfig {
    std::string suite_name = "custom";
    std::string directory;             // scan dir for *.mps/*.lp/*.MPS/*.LP
    std::vector<std::string> files;    // explicit file list (takes precedence over directory)

    double time_limit_seconds = 3600.0;
    double mip_gap_tolerance = 1e-4;
    int thread_count = 0;
    std::vector<int> thread_sweep; // extra B&B runs at these thread counts
    EngineType engine = EngineType::AUTO;

    double reference_rel_tol = 1e-6;   // relative tolerance for objective match
    std::string reference_csv;         // optional explicit reference table
    bool use_builtin_netlib_reference = true;
};

// Runs the suite: parses every instance, solves it via the public API,
// re-verifies each result with the validation layer, and grades every
// instance against its reference (explicit CSV or built-in table).
SuiteReport run_benchmark(const BenchmarkConfig& config);

} // namespace hypernova::benchmark