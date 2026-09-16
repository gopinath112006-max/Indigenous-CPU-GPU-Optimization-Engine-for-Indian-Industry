#pragma once

#include "problem.hpp"
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>

namespace hypernova::model {

struct MPSParseResult {
    Problem problem;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    bool success = false;
};

class MPSParser {
public:
    MPSParser() = default;

    MPSParseResult parse_file(const std::string& filepath);
    MPSParseResult parse_string(const std::string& content);

    void set_free_format(bool free) { free_format_ = free; free_format_explicit_ = true; }
    void set_keep_names(bool keep) { keep_names_ = keep; }

private:
    enum class Section {
        NONE,
        NAME,
        ROWS,
        COLUMNS,
        RHS,
        RANGES,
        BOUNDS,
        SOS,
        QUADOBJ,
        QCMATRIX,
        OBJSENSE,
        ENDATA
    };

    bool free_format_ = true;
    bool free_format_explicit_ = false;
    bool keep_names_ = true;

    MPSParseResult result_;
    ProblemBuilder builder_;
    std::size_t current_row_ = 0;
    std::size_t current_col_ = 0;
    std::string obj_row_name_;
    std::unordered_map<std::string, std::size_t> col_name_to_idx_;
    std::vector<numerical::Triplet> constraint_triplets_;

    Section detect_section(const std::string& line);
    bool detect_free_format(const std::string& content);
    void parse_name(const std::string& line);
    void parse_rows(const std::string& line);
    void parse_columns(const std::string& line);
    void parse_rhs(const std::string& line);
    void parse_ranges(const std::string& line);
    void parse_bounds(const std::string& line);
    void parse_sos(const std::string& line);
    void parse_quadobj(const std::string& line);
    void parse_qcmatrix(const std::string& line);

    std::optional<std::tuple<std::string, std::string, double, std::string, double>>
        parse_free_format_line(const std::string& line);

    std::optional<std::tuple<std::string, std::string, double, std::string, double>>
        parse_fixed_format_line(const std::string& line);

    void add_warning(const std::string& msg) { result_.warnings.push_back(msg); }
    void add_error(const std::string& msg) { result_.errors.push_back(msg); }
};

void write_mps(const Problem& problem, const std::string& filepath);

} // namespace hypernova::model