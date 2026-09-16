#pragma once

#include "ProblemBuilder.hpp"
#include <string>

namespace hypernova::cli {

class ExportManager {
public:
    static bool export_mps(const model::Problem& problem, const std::string& filepath);

    static bool export_lp(const model::Problem& problem, const std::string& filepath);

    static bool export_json(const model::Problem& problem, const std::string& filepath);

    static std::string default_output_path(const std::string& problem_name,
                                            const std::string& format);
};

} // namespace hypernova::cli
