#pragma once

#include "problem.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace hypernova::model {

struct LPParseResult {
    Problem problem;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    bool success = false;
};

class LPParser {
public:
    LPParser() = default;

    LPParseResult parse_file(const std::string& filepath);
    LPParseResult parse_string(const std::string& content);

    void set_keep_names(bool keep) { keep_names_ = keep; }

private:
    enum class Section {
        NONE,
        OBJ,
        CONSTRAINTS,
        BOUNDS,
        GENERAL,
        BINARY,
        SEMI_CONT,
        SEMI_INT,
        SOS,
        QUADRATIC,
        END
    };

    bool keep_names_ = true;

    LPParseResult result_;
    ProblemBuilder builder_;
    std::unordered_map<std::string, std::size_t> var_name_to_idx_;
    std::unordered_map<std::string, std::size_t> con_name_to_idx_;
    ObjectiveSense obj_sense_ = ObjectiveSense::MINIMIZE;
    std::string obj_buf_;
    std::string con_acc_;
    std::string con_acc_name_;

    Section detect_section(const std::string& line);
    void finalize_obj();
    void parse_constraints(const std::string& line);
    void finalize_constraint();
    void parse_bounds(const std::string& line);
    void parse_general(const std::string& line);
    void parse_binary(const std::string& line);
    void parse_sos(const std::string& line);
    void parse_quadratic(const std::string& line);

    std::vector<std::pair<std::size_t, double>> parse_linear_expr(const std::string& expr);
    std::vector<QuadTerm> parse_quadratic_expr(const std::string& expr);

    std::string trim(const std::string& s);
    std::vector<std::string> split(const std::string& s, char delim);
};

void write_lp(const Problem& problem, const std::string& filepath);

} // namespace hypernova::model