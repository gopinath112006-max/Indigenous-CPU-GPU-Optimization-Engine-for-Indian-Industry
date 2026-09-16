#include "ExportManager.hpp"
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace hypernova::cli {

bool ExportManager::export_mps(const model::Problem& problem, const std::string& filepath) {
    try {
        model::write_mps(problem, filepath);
        return true;
    } catch (...) {
        return false;
    }
}

bool ExportManager::export_lp(const model::Problem& problem, const std::string& filepath) {
    try {
        model::write_lp(problem, filepath);
        return true;
    } catch (...) {
        return false;
    }
}

bool ExportManager::export_json(const model::Problem& problem, const std::string& filepath) {
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        file << "{\n";
        file << "  \"name\": \"" << problem.name << "\",\n";
        file << "  \"obj_sense\": " << static_cast<int>(problem.obj_sense) << ",\n";

        file << "  \"variables\": [\n";
        for (std::size_t i = 0; i < problem.variables.size(); ++i) {
            const auto& var = problem.variables[i];
            file << "    {\n";
            file << "      \"name\": \"" << var.name << "\",\n";
            file << "      \"type\": " << static_cast<int>(var.type) << ",\n";
            file << "      \"lower_bound\": " << std::setprecision(17) << var.lower_bound << ",\n";
            file << "      \"upper_bound\": " << std::setprecision(17) << var.upper_bound << ",\n";
            file << "      \"objective_coeff\": " << std::setprecision(17) << var.objective_coeff << "\n";
            file << "    }";
            if (i < problem.variables.size() - 1) file << ",";
            file << "\n";
        }
        file << "  ],\n";

        file << "  \"constraints\": [\n";
        for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
            const auto& con = problem.constraints[i];
            file << "    {\n";
            file << "      \"name\": \"" << con.name << "\",\n";
            file << "      \"sense\": " << static_cast<int>(con.sense) << ",\n";
            file << "      \"rhs\": " << std::setprecision(17) << con.rhs << "\n";
            file << "    }";
            if (i < problem.constraints.size() - 1) file << ",";
            file << "\n";
        }
        file << "  ],\n";

        file << "  \"quadratic_terms\": [\n";
        for (std::size_t i = 0; i < problem.quadratic_terms.size(); ++i) {
            const auto& qt = problem.quadratic_terms[i];
            file << "    {\"row\": " << qt.row
                 << ", \"col\": " << qt.col
                 << ", \"coeff\": " << std::setprecision(17) << qt.coeff << "}";
            if (i < problem.quadratic_terms.size() - 1) file << ",";
            file << "\n";
        }
        file << "  ]\n";

        file << "}\n";
        return true;

    } catch (...) {
        return false;
    }
}

std::string ExportManager::default_output_path(const std::string& problem_name,
                                                  const std::string& format) {
    std::string safe_name;
    for (char c : problem_name) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            safe_name += c;
        } else if (c == ' ') {
            safe_name += '_';
        }
    }
    if (safe_name.empty()) {
        safe_name = "problem";
    }
    return safe_name + "." + format;
}

} // namespace hypernova::cli
