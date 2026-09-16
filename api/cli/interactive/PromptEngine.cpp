#include "PromptEngine.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace hypernova::cli {

PromptEngine::PromptEngine(bool use_colored_output)
    : colored_(use_colored_output) {}

std::string PromptEngine::prompt_string(const std::string& label,
                                         const std::string& default_val,
                                         bool allow_empty) {
    while (true) {
        std::cout << "  " << label;
        if (!default_val.empty()) {
            std::cout << " [" << default_val << "]";
        }
        std::cout << ": ";
        std::cout.flush();

        std::string input = read_line();

        if (input.empty() && !default_val.empty()) {
            return default_val;
        }

        if (input.empty() && allow_empty) {
            return "";
        }

        if (!input.empty()) {
            return input;
        }

        print_warning("Input cannot be empty.");
    }
}

int PromptEngine::prompt_int(const std::string& label,
                              int default_val,
                              int min_val,
                              int max_val) {
    while (true) {
        std::cout << "  " << label << " [" << default_val << "]: ";
        std::cout.flush();

        std::string input = read_line();

        if (input.empty()) {
            return default_val;
        }

        auto parsed = parse_int(input);
        if (parsed.has_value() && *parsed >= min_val && *parsed <= max_val) {
            return *parsed;
        }

        print_warning("Please enter a valid integer between " +
                      std::to_string(min_val) + " and " + std::to_string(max_val));
    }
}

double PromptEngine::prompt_double(const std::string& label,
                                    double default_val,
                                    double min_val,
                                    double max_val) {
    while (true) {
        std::cout << "  " << label << " [" << default_val << "]: ";
        std::cout.flush();

        std::string input = read_line();

        if (input.empty()) {
            return default_val;
        }

        auto parsed = parse_double(input);
        if (parsed.has_value() && *parsed >= min_val && *parsed <= max_val) {
            return *parsed;
        }

        print_warning("Please enter a valid number.");
    }
}

bool PromptEngine::prompt_bool(const std::string& label, bool default_val) {
    std::string hint = default_val ? "(Y/n)" : "(y/N)";

    while (true) {
        std::cout << "  " << label << " " << hint << ": ";
        std::cout.flush();

        std::string input = read_line();
        std::transform(input.begin(), input.end(), input.begin(), ::tolower);

        if (input.empty()) {
            return default_val;
        }

        if (input == "y" || input == "yes" || input == "true" || input == "1") {
            return true;
        }

        if (input == "n" || input == "no" || input == "false" || input == "0") {
            return false;
        }

        print_warning("Please enter y or n.");
    }
}

std::string PromptEngine::prompt_choice(const std::string& label,
                                          const std::vector<std::string>& options,
                                          const std::string& default_val) {
    while (true) {
        std::cout << "  " << label << " (";
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (i > 0) std::cout << "/";
            std::cout << options[i];
        }
        std::cout << ") [" << default_val << "]: ";
        std::cout.flush();

        std::string input = read_line();
        std::transform(input.begin(), input.end(), input.begin(), ::tolower);

        if (input.empty()) {
            return default_val;
        }

        for (const auto& opt : options) {
            std::string opt_lower = opt;
            std::transform(opt_lower.begin(), opt_lower.end(), opt_lower.begin(), ::tolower);
            if (input == opt_lower) {
                return opt;
            }
        }

        print_warning("Please enter one of the valid options.");
    }
}

std::vector<CoefficientEntry> PromptEngine::prompt_sparse_coefficients(
    std::size_t num_variables,
    const std::string& label) {

    std::vector<CoefficientEntry> result;

    print_info("Enter non-zero coefficients as: idx:value idx:value ...");
    print_info("Example: 0:2.5 1:-1.0 3:3.0");
    print_info("Variable indices: 0 to " + std::to_string(num_variables - 1));

    while (true) {
        std::cout << "  " << label << " (empty=done): ";
        std::cout.flush();

        std::string input = read_line();

        if (input.empty()) {
            break;
        }

        auto parsed = parse_coefficient_string(input, num_variables);

        if (parsed.empty()) {
            print_warning("No valid coefficients found. Format: idx:value idx:value ...");
            continue;
        }

        for (const auto& entry : parsed) {
            print_info("  " + std::to_string(entry.index) + ": " + std::to_string(entry.value));
        }

        result.insert(result.end(), parsed.begin(), parsed.end());
    }

    return result;
}

void PromptEngine::print_header(const std::string& title) {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "============================================\n\n";
}

void PromptEngine::print_section(const std::string& title) {
    std::cout << "\n--- " << title << " ---\n\n";
}

void PromptEngine::print_success(const std::string& message) {
    std::cout << "  [OK] " << message << "\n";
}

void PromptEngine::print_warning(const std::string& message) {
    std::cout << "  [!] " << message << "\n";
}

void PromptEngine::print_error(const std::string& message) {
    std::cerr << "  [ERROR] " << message << "\n";
}

void PromptEngine::print_info(const std::string& message) {
    std::cout << "  " << message << "\n";
}

void PromptEngine::print_separator() {
    std::cout << "--------------------------------------------\n";
}

void PromptEngine::print_blank() {
    std::cout << "\n";
}

bool PromptEngine::is_valid_identifier(const std::string& name) {
    if (name.empty()) return false;
    if (!std::isalpha(name[0]) && name[0] != '_') return false;
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!std::isalnum(name[i]) && name[i] != '_') return false;
    }
    return true;
}

std::string PromptEngine::read_line() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

std::optional<double> PromptEngine::parse_double(const std::string& str) {
    try {
        std::size_t pos = 0;
        double val = std::stod(str, &pos);
        if (pos != str.size()) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> PromptEngine::parse_int(const std::string& str) {
    try {
        std::size_t pos = 0;
        int val = std::stoi(str, &pos);
        if (pos != str.size()) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<CoefficientEntry> PromptEngine::parse_coefficient_string(
    const std::string& input,
    std::size_t max_index) {

    std::vector<CoefficientEntry> result;
    std::istringstream iss(input);
    std::string token;

    while (iss >> token) {
        auto colon_pos = token.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        std::string idx_str = token.substr(0, colon_pos);
        std::string val_str = token.substr(colon_pos + 1);

        auto idx = parse_int(idx_str);
        auto val = parse_double(val_str);

        if (idx.has_value() && val.has_value()) {
            if (*idx >= 0 && static_cast<std::size_t>(*idx) < max_index) {
                result.push_back({static_cast<std::size_t>(*idx), *val});
            }
        }
    }

    return result;
}

} // namespace hypernova::cli
