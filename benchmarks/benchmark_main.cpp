#include "benchmark_runner.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace hypernova;
using namespace hypernova::benchmark;

namespace {

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " --dir <instance-dir> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --suite <name>            Suite name for the report (default: custom)\n";
    std::cout << "  --file <path>             Solve a specific file (repeatable; overrides --dir)\n";
    std::cout << "  --expect <csv>            Reference values CSV (name,status,objective)\n";
    std::cout << "  --no-reference            Disable built-in Netlib reference table\n";
    std::cout << "  --engine <engine>         auto|simplex|dual-simplex|ipm|bnb\n";
    std::cout << "  --time-limit <seconds>    Per-instance time limit (default: 3600)\n";
    std::cout << "  --gap <tolerance>         MIP gap tolerance (default: 1e-4)\n";
    std::cout << "  --threads <count>         Thread count (default: auto)\n";
    std::cout << "  --sweep <c1,c2,...>       Extra B&B runs at these thread counts (e.g. 1,2,4,8)\n";
    std::cout << "  --tol <relative>          Reference objective relative tolerance (default: 1e-6)\n";
    std::cout << "  --out <file>              Write JSON report\n";
    std::cout << "  --csv <file>              Write CSV report\n";
}

} // namespace

int main(int argc, char** argv) {
    BenchmarkConfig config;
    std::string out_json;
    std::string out_csv;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + arg);
            }
            return argv[++i];
        };
        if (arg == "--dir") {
            config.directory = next();
        } else if (arg == "--file") {
            config.files.push_back(next());
        } else if (arg == "--suite") {
            config.suite_name = next();
        } else if (arg == "--expect") {
            config.reference_csv = next();
        } else if (arg == "--no-reference") {
            config.use_builtin_netlib_reference = false;
        } else if (arg == "--engine") {
            std::string val = next();
            if (val == "simplex") config.engine = EngineType::PRIMAL_SIMPLEX;
            else if (val == "dual-simplex") config.engine = EngineType::DUAL_SIMPLEX;
            else if (val == "ipm") config.engine = EngineType::INTERIOR_POINT;
            else if (val == "bnb") config.engine = EngineType::BRANCH_AND_BOUND;
            else throw std::runtime_error("unknown engine: " + val);
        } else if (arg == "--time-limit") {
            config.time_limit_seconds = std::stod(next());
        } else if (arg == "--gap") {
            config.mip_gap_tolerance = std::stod(next());
        } else if (arg == "--threads") {
            config.thread_count = std::stoi(next());
        } else if (arg == "--sweep") {
            std::string val = next();
            std::istringstream ss(val);
            std::string token;
            while (std::getline(ss, token, ',')) {
                if (token.empty()) continue;
                config.thread_sweep.push_back(std::stoi(token));
            }
            std::sort(config.thread_sweep.begin(), config.thread_sweep.end());
            config.thread_sweep.erase(
                std::unique(config.thread_sweep.begin(), config.thread_sweep.end()),
                config.thread_sweep.end());
        } else if (arg == "--tol") {
            config.reference_rel_tol = std::stod(next());
        } else if (arg == "--out") {
            out_json = next();
        } else if (arg == "--csv") {
            out_csv = next();
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (config.directory.empty() && config.files.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        SuiteReport report = run_benchmark(config);

        std::cout << report.to_summary_string();
        std::cout << "\nPer-instance results:\n";
        for (const auto& instance : report.instances) {
            std::cout << "  [" << (instance.pass ? "PASS" : "FAIL") << "] "
                      << instance.name << " (" << instance.problem_type << ") "
                      << SuiteReport::status_name(instance.status) << " obj=" << instance.objective
                      << " time=" << instance.solve_time_seconds << "s\n";
            if (!instance.message.empty()) {
                std::cout << "        " << instance.message << "\n";
            }
        }

        if (!out_json.empty()) {
            std::ofstream oss(out_json);
            if (!oss.is_open()) {
                throw std::runtime_error("cannot open " + out_json + " for writing");
            }
            oss << report.to_json().dump(2) << "\n";
        }
        if (!out_csv.empty()) {
            std::ofstream oss(out_csv);
            if (!oss.is_open()) {
                throw std::runtime_error("cannot open " + out_csv + " for writing");
            }
            oss << report.to_csv();
        }
        std::cout << "\nSummary: " << report.num_pass << "/" << report.total
                  << " instances passing.\n";
        return report.all_pass ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}