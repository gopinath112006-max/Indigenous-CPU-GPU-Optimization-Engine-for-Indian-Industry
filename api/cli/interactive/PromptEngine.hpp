#pragma once

#include "ProblemBuilder.hpp"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <limits>

namespace hypernova::cli {

class PromptEngine {
public:
    explicit PromptEngine(bool use_colored_output = true);

    std::string prompt_string(const std::string& label,
                              const std::string& default_val = "",
                              bool allow_empty = true);

    int prompt_int(const std::string& label,
                   int default_val,
                   int min_val = std::numeric_limits<int>::min(),
                   int max_val = std::numeric_limits<int>::max());

    double prompt_double(const std::string& label,
                         double default_val,
                         double min_val = -std::numeric_limits<double>::infinity(),
                         double max_val = std::numeric_limits<double>::infinity());

    bool prompt_bool(const std::string& label, bool default_val = true);

    std::string prompt_choice(const std::string& label,
                              const std::vector<std::string>& options,
                              const std::string& default_val);

    std::vector<CoefficientEntry> prompt_sparse_coefficients(
        std::size_t num_variables,
        const std::string& label);

    void print_header(const std::string& title);
    void print_section(const std::string& title);
    void print_success(const std::string& message);
    void print_warning(const std::string& message);
    void print_error(const std::string& message);
    void print_info(const std::string& message);
    void print_separator();
    void print_blank();

    static bool is_valid_identifier(const std::string& name);

private:
    std::string read_line();
    std::optional<double> parse_double(const std::string& str);
    std::optional<int> parse_int(const std::string& str);
    std::vector<CoefficientEntry> parse_coefficient_string(
        const std::string& input,
        std::size_t max_index);

    bool colored_;
};

} // namespace hypernova::cli
