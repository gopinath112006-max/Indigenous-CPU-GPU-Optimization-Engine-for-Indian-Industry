#include "mps_parser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <limits>

namespace hypernova::model {

MPSParseResult MPSParser::parse_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        MPSParseResult result;
        result.errors.push_back("Cannot open file: " + filepath);
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_string(buffer.str());
}

MPSParseResult MPSParser::parse_string(const std::string& content) {
    result_ = MPSParseResult{};
    builder_ = ProblemBuilder{};
    current_row_ = 0;
    current_col_ = 0;
    obj_row_name_.clear();
    col_name_to_idx_.clear();
    constraint_triplets_.clear();

    if (!free_format_explicit_) {
        free_format_ = detect_free_format(content);
    }

    Section current_section = Section::NONE;
    std::istringstream stream(content);
    std::string line;
    std::size_t line_num = 0;

    while (std::getline(stream, line)) {
        ++line_num;

        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

        if (trimmed.empty() || trimmed[0] == '*') continue;

        Section new_section = detect_section(trimmed);
        if (new_section != Section::NONE) {
            current_section = new_section;
            if (new_section == Section::NAME) {
                parse_name(trimmed);
            } else if (new_section == Section::OBJSENSE) {
                std::string sense;
                std::istringstream(trimmed) >> sense >> sense;
                std::string upper_sense = sense;
                std::transform(upper_sense.begin(), upper_sense.end(), upper_sense.begin(), ::toupper);
                if (upper_sense == "MAX" || upper_sense == "MAXIMIZE") {
                    builder_.get_problem().obj_sense = ObjectiveSense::MAXIMIZE;
                }
            }
            continue;
        }

        try {
            switch (current_section) {
                case Section::NAME: parse_name(line); break;
                case Section::ROWS: parse_rows(line); break;
                case Section::COLUMNS: parse_columns(line); break;
                case Section::RHS: parse_rhs(line); break;
                case Section::RANGES: parse_ranges(line); break;
                case Section::BOUNDS: parse_bounds(line); break;
                case Section::SOS: parse_sos(line); break;
                case Section::QUADOBJ: parse_quadobj(line); break;
                case Section::QCMATRIX: parse_qcmatrix(line); break;
                case Section::OBJSENSE: {
                    std::string sense;
                    std::istringstream(trimmed) >> sense;
                    std::string upper_sense = sense;
                    std::transform(upper_sense.begin(), upper_sense.end(), upper_sense.begin(), ::toupper);
                    if (upper_sense == "MAX" || upper_sense == "MAXIMIZE") {
                        builder_.get_problem().obj_sense = ObjectiveSense::MAXIMIZE;
                    }
                    break;
                }
                default: break;
            }
        } catch (const std::exception& e) {
            add_error("Line " + std::to_string(line_num) + ": " + e.what());
        }
    }

    if (!result_.errors.empty()) {
        result_.success = false;
    } else {
        result_.problem = builder_.build();
        // MPS integer blocks (MARKER INTORG/INTEND) declare integer variables;
        // those that are also restricted to [0,1] by bounds are binary. Make
        // that classification explicit so downstream engines branch correctly.
        for (auto& v : result_.problem.variables) {
            if (v.type == VarType::INTEGER && v.lower_bound >= 0.0 && v.upper_bound <= 1.0) {
                v.type = VarType::BINARY;
            }
        }
        if (!constraint_triplets_.empty()) {
            result_.problem.constraint_matrix = numerical::SparseMatrix::from_triplets(
                result_.problem.constraints.size(),
                result_.problem.variables.size(),
                constraint_triplets_);
        }
        result_.success = true;
    }

    return result_;
}

// Free vs. fixed MPS layout detection (coin-utils style): within COLUMNS data
// lines, the fixed format places the second token (row name) in columns 15-22,
// i.e. starting exactly at index 14. Free format bounds these tokens purely by
// whitespace, so alignment at column 15 across most sampled lines is a strong
// fixed-format signature. Netlib LP files are fixed format and routinely leave
// the RHS vector-name field blank, which free-format parsing would misread as
// shifting the row name / value fields by one.
bool MPSParser::detect_free_format(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    bool in_columns = false;
    int samples = 0;
    int fixed_aligned = 0;
    while (std::getline(stream, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        if (trimmed.empty() || trimmed[0] == '*') continue;

        Section sec = detect_section(trimmed);
        if (sec != Section::NONE) {
            in_columns = (sec == Section::COLUMNS);
            continue;
        }
        if (!in_columns) continue;

        std::size_t first_ns = line.find_first_not_of(" \t");
        if (first_ns == std::string::npos) continue;
        std::size_t second_ns = line.find_first_not_of(" \t", first_ns + 1);
        if (second_ns == std::string::npos) continue;

        ++samples;
        if (second_ns == 14) ++fixed_aligned;
        if (samples >= 8) break;
    }

    if (samples >= 3 && fixed_aligned * 2 >= samples) {
        return false;  // fixed format
    }
    return true;  // default: free format
}

MPSParser::Section MPSParser::detect_section(const std::string& line) {
    std::string first_token;
    std::istringstream iss(line);
    iss >> first_token;

    std::string upper = first_token;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    std::string second_token;
    const bool single_token = !(iss >> second_token);

    if (upper == "NAME") return Section::NAME;
    if (upper == "OBJSENSE") return Section::OBJSENSE;
    if (!single_token) return Section::NONE;
    if (upper == "ROWS") return Section::ROWS;
    if (upper == "COLUMNS") return Section::COLUMNS;
    if (upper == "RHS") return Section::RHS;
    if (upper == "RANGES") return Section::RANGES;
    if (upper == "BOUNDS") return Section::BOUNDS;
    if (upper == "SOS") return Section::SOS;
    if (upper == "QUADOBJ") return Section::QUADOBJ;
    if (upper == "QCMATRIX") return Section::QCMATRIX;
    if (upper == "ENDATA") return Section::ENDATA;
    return Section::NONE;
}

void MPSParser::parse_name(const std::string& line) {
    std::istringstream iss(line);
    std::string name;
    iss >> name;
    if (iss >> name) {
        builder_ = ProblemBuilder{name};
    }
}

void MPSParser::parse_rows(const std::string& line) {
    std::istringstream iss(line);
    std::string sense_str, name;
    iss >> sense_str >> name;

    if (sense_str == "N") {
        obj_row_name_ = name;
        return;
    }

    ConstraintSense sense = ConstraintSense::LE;
    if (sense_str == "L") sense = ConstraintSense::LE;
    else if (sense_str == "G") sense = ConstraintSense::GE;
    else if (sense_str == "E") sense = ConstraintSense::EQ;
    else {
        add_warning("Unknown row type: " + sense_str + " for row " + name);
    }

    builder_.add_constraint({}, sense, 0.0, keep_names_ ? name : "");
    ++current_row_;
}

void MPSParser::parse_columns(const std::string& line) {
    // MPS MARKER records (INTORG/INTEND) delimit the integer-variable block:
    //     MARK0000  'MARKER'  'INTORG'
    //     ...
    //     MARK0001  'MARKER'  'INTEND'
    // The parser must not turn the marker name into a variable.
    if (is_marker_record(line)) return;

    auto parsed = free_format_ ? parse_free_format_line(line) : parse_fixed_format_line(line);
    if (!parsed) return;

    auto [col_name, row1_name, coeff1, row2_name, coeff2] = *parsed;

    if (col_name == "MARKER") {
        return;
    }

    auto it = col_name_to_idx_.find(col_name);
    std::size_t col_idx;
    if (it == col_name_to_idx_.end()) {
        model::VarType new_type = in_integer_block_ ? model::VarType::INTEGER
                                                    : model::VarType::CONTINUOUS;
        col_idx = builder_.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                         new_type, keep_names_ ? col_name : "");
        col_name_to_idx_[col_name] = col_idx;
    } else {
        col_idx = it->second;
    }

    auto set_coeff = [&](const std::string& row_name, double coeff) {
        if (row_name.empty() || coeff == 0.0) return;
        auto& prob = builder_.get_problem();
        if (row_name == obj_row_name_) {
            prob.variables[col_idx].objective_coeff = coeff;
            return;
        }
        for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
            if (prob.constraints[i].name == row_name) {
                constraint_triplets_.emplace_back(i, col_idx, coeff);
                return;
            }
        }
        // Row referenced in COLUMNS but never declared in ROWS and not equal
        // to the objective row. Non-conforming writers frequently omit the
        // 'N' objective row entirely; leniently treat the first such row as
        // an implicit objective row instead of silently dropping the data.
        if (obj_row_name_.empty()) {
            obj_row_name_ = row_name;
            prob.variables[col_idx].objective_coeff = coeff;
            add_warning("Implicit objective row '" + row_name + "' (no N row declared in ROWS)");
            return;
        }
        add_warning("Unknown row '" + row_name + "' referenced in COLUMNS is ignored");
    };

    set_coeff(row1_name, coeff1);
    set_coeff(row2_name, coeff2);
}

bool MPSParser::is_marker_record(const std::string& line) {
    // Marker records are:  <name>  'MARKER'  'INTORG' | 'INTEND'
    // Standard writers name the column arbitrarily (e.g. MARK0000); a record
    // whose first token is the quoted string "MARKER" itself is also legal.
    auto tokens = [&]() {
        std::vector<std::string> toks;
        std::istringstream iss(line);
        std::string t;
        while (iss >> t) {
            if (t.size() >= 2 && t.front() == '\'') t.erase(0, 1);
            if (!t.empty() && t.back() == '\'') t.pop_back();
            toks.push_back(t);
        }
        return toks;
    };

    std::vector<std::string> t = tokens();
    if (t.size() < 2) return false;
    bool has_marker = false;
    std::string kind;
    for (std::size_t i = 0; i < t.size(); ++i) {
        std::string u = t[i];
        std::transform(u.begin(), u.end(), u.begin(), ::toupper);
        if (u == "MARKER") { has_marker = true; continue; }
        if (u == "INTORG" || u == "INTEND") { kind = u; break; }
    }
    if (!has_marker) return false;
    if (kind == "INTORG") { in_integer_block_ = true; return true; }
    if (kind == "INTEND") { in_integer_block_ = false; return true; }
    return true;  // bare 'MARKER' name with no kind: still not a variable
}

void MPSParser::parse_rhs(const std::string& line) {
    auto parsed = free_format_ ? parse_free_format_line(line) : parse_fixed_format_line(line);
    if (!parsed) return;

    auto [rhs_name, row1_name, val1, row2_name, val2] = *parsed;

    auto& prob = builder_.get_problem();
    auto apply = [&](const std::string& row_name, double val) {
        for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
            if (prob.constraints[i].name == row_name) {
                prob.constraints[i].rhs = val;
                return true;
            }
        }
        return false;
    };

    bool matched1 = !row1_name.empty() && apply(row1_name, val1);
    if (!matched1 && !row1_name.empty()) {
        // Free-format parsing misread the fields (e.g. a blank RHS vector name
        // shifted the row name into field 1); retry this line in fixed layout.
        if (free_format_ && line.size() >= 50) {
            auto fixed = parse_fixed_format_line(line);
            if (fixed) {
                auto [f_rhs, f_row1, f_val1, f_row2, f_val2] = *fixed;
                if (!f_row1.empty() && apply(f_row1, f_val1)) {
                    if (!f_row2.empty()) apply(f_row2, f_val2);
                    return;
                }
            }
        }
    } else if (row1_name.empty()) {
        if (!row2_name.empty()) apply(row2_name, val2);
    }
    if (!row2_name.empty() && !apply(row2_name, val2)) {
        if (free_format_ && line.size() >= 50) {
            auto fixed = parse_fixed_format_line(line);
            if (fixed) {
                auto [f_rhs, f_row1, f_val1, f_row2, f_val2] = *fixed;
                if (!f_row1.empty() && apply(f_row1, f_val1)) {
                    if (!f_row2.empty()) apply(f_row2, f_val2);
                }
            }
        }
    }
}

void MPSParser::parse_ranges(const std::string& line) {
    auto parsed = free_format_ ? parse_free_format_line(line) : parse_fixed_format_line(line);
    if (!parsed) return;

    auto [range_name, row1_name, val1, row2_name, val2] = *parsed;

    auto& prob = builder_.get_problem();
    if (!row1_name.empty()) {
        for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
            if (prob.constraints[i].name == row1_name) {
                double rhs = prob.constraints[i].rhs;
                if (prob.constraints[i].sense == ConstraintSense::LE) {
                    prob.constraints[i].upper_bound = rhs + val1;
                    prob.constraints[i].lower_bound = rhs;
                } else if (prob.constraints[i].sense == ConstraintSense::GE) {
                    prob.constraints[i].lower_bound = rhs - val1;
                    prob.constraints[i].upper_bound = rhs;
                } else {
                    prob.constraints[i].lower_bound = rhs - std::abs(val1);
                    prob.constraints[i].upper_bound = rhs + std::abs(val1);
                }
                break;
            }
        }
    }
    if (!row2_name.empty()) {
        for (std::size_t i = 0; i < prob.constraints.size(); ++i) {
            if (prob.constraints[i].name == row2_name) {
                double rhs = prob.constraints[i].rhs;
                if (prob.constraints[i].sense == ConstraintSense::LE) {
                    prob.constraints[i].upper_bound = rhs + val2;
                    prob.constraints[i].lower_bound = rhs;
                } else if (prob.constraints[i].sense == ConstraintSense::GE) {
                    prob.constraints[i].lower_bound = rhs - val2;
                    prob.constraints[i].upper_bound = rhs;
                } else {
                    prob.constraints[i].lower_bound = rhs - std::abs(val2);
                    prob.constraints[i].upper_bound = rhs + std::abs(val2);
                }
                break;
            }
        }
    }
}

void MPSParser::parse_bounds(const std::string& line) {
    std::istringstream iss(line);
    std::string bound_type, bound_name, col_name;
    double value = 0.0;

    iss >> bound_type >> bound_name >> col_name;
    if (bound_type != "FR" && bound_type != "MI" && bound_type != "PL") {
        iss >> value;
    }

    auto& prob = builder_.get_problem();
    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (prob.variables[j].name == col_name) {
            if (bound_type == "LO") {
                prob.variables[j].lower_bound = value;
            } else if (bound_type == "UP") {
                prob.variables[j].upper_bound = value;
            } else if (bound_type == "FX") {
                prob.variables[j].lower_bound = value;
                prob.variables[j].upper_bound = value;
            } else if (bound_type == "FR") {
                prob.variables[j].lower_bound = -std::numeric_limits<double>::infinity();
                prob.variables[j].upper_bound = std::numeric_limits<double>::infinity();
            } else if (bound_type == "MI") {
                prob.variables[j].lower_bound = -std::numeric_limits<double>::infinity();
            } else if (bound_type == "PL") {
                prob.variables[j].upper_bound = std::numeric_limits<double>::infinity();
            } else if (bound_type == "BV") {
                prob.variables[j].type = VarType::BINARY;
                prob.variables[j].lower_bound = 0.0;
                prob.variables[j].upper_bound = 1.0;
            } else if (bound_type == "UI") {
                prob.variables[j].type = VarType::INTEGER;
                prob.variables[j].upper_bound = value;
            } else if (bound_type == "LI") {
                prob.variables[j].type = VarType::INTEGER;
                prob.variables[j].lower_bound = value;
            } else if (bound_type == "SC") {
                prob.variables[j].type = VarType::SEMI_CONTINUOUS;
                prob.variables[j].lower_bound = value;
            } else if (bound_type == "SI") {
                prob.variables[j].type = VarType::SEMI_INTEGER;
                prob.variables[j].lower_bound = value;
            }
            break;
        }
    }
}

void MPSParser::parse_sos(const std::string& line) {
    std::istringstream iss(line);
    std::string sos_name, sos_type, row_name;
    double weight = 0.0;
    std::size_t col_idx = 0;

    iss >> sos_name >> sos_type;
    if (sos_type != "S1" && sos_type != "S2") return;

    if (sos_type != "S1" && sos_type != "S2") return;

    SOS::Type type = (sos_type == "S1") ? SOS::Type::SOS1 : SOS::Type::SOS2;
    std::vector<std::size_t> vars;
    std::vector<double> weights;

    while (iss >> row_name >> weight >> col_idx) {
        auto& prob = builder_.get_problem();
        for (std::size_t j = 0; j < prob.variables.size(); ++j) {
            if (prob.variables[j].name == row_name) {
                vars.push_back(j);
                weights.push_back(weight);
                break;
            }
        }
    }

    if (!vars.empty()) {
        if (type == SOS::Type::SOS1) {
            builder_.add_sos1(vars, weights, keep_names_ ? sos_name : "");
        } else {
            builder_.add_sos2(vars, weights, keep_names_ ? sos_name : "");
        }
    }
}

void MPSParser::parse_quadobj(const std::string& line) {
    auto parsed = free_format_ ? parse_free_format_line(line) : parse_fixed_format_line(line);
    if (!parsed) return;

    auto [col1_name, col2_name, coeff, col3_name, coeff2] = *parsed;

    auto& prob = builder_.get_problem();
    std::size_t idx1 = std::numeric_limits<std::size_t>::max();
    std::size_t idx2 = std::numeric_limits<std::size_t>::max();

    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (prob.variables[j].name == col1_name) idx1 = j;
        if (prob.variables[j].name == col2_name) idx2 = j;
    }

    if (idx1 != std::numeric_limits<std::size_t>::max() &&
        idx2 != std::numeric_limits<std::size_t>::max()) {
        builder_.add_quadratic_term(idx1, idx2, coeff);
    }

    if (!col3_name.empty() && coeff2 != 0.0) {
        std::size_t idx3 = std::numeric_limits<std::size_t>::max();
        for (std::size_t j = 0; j < prob.variables.size(); ++j) {
            if (prob.variables[j].name == col3_name) { idx3 = j; break; }
        }
        if (idx3 != std::numeric_limits<std::size_t>::max()) {
            builder_.add_quadratic_term(idx1, idx3, coeff2);
        }
    }
}

void MPSParser::parse_qcmatrix(const std::string& line) {
    parse_quadobj(line);
}

std::optional<std::tuple<std::string, std::string, double, std::string, double>>
MPSParser::parse_free_format_line(const std::string& line) {
    std::istringstream iss(line);
    std::string name1, name2, name3;
    double val1 = 0.0, val2 = 0.0;

    if (!(iss >> name1)) return std::nullopt;
    if (!(iss >> name2)) return std::nullopt;
    if (!(iss >> val1)) return std::nullopt;
    if (!(iss >> name3)) name3 = "";
    if (!(iss >> val2)) val2 = 0.0;

    return std::make_tuple(name1, name2, val1, name3, val2);
}

std::optional<std::tuple<std::string, std::string, double, std::string, double>>
MPSParser::parse_fixed_format_line(const std::string& line) {

    auto field = [&](std::size_t offset, std::size_t width) -> std::string {
        if (offset > line.size()) return "";
        return line.substr(offset, std::min(width, line.size() - offset));
    };

    auto trim = [](std::string s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
        return s;
    };

    auto is_numeric = [](const std::string& s) {
        if (s.empty()) return false;
        std::size_t consumed = 0;
        try { (void)std::stod(s, &consumed); } catch (...) { return false; }
        return consumed == s.size();
    };

    // Right-justified fixed-format writers (e.g. the official MIPLIB archives)
    // align each numeric field to the trailing edge of its fixed column window.
    // A value that is 12+ characters long then spills left of the nominal field
    // start. The strict fixed-offset slice (below) would truncate the leading
    // digit or sign of such a value, so prefer a whitespace-token interpretation
    // whenever the line matches the required shape:
    //     <name> [<name>] <value> [<name>] <value>
    // with genuinely numeric value fields. Blank-field writers (e.g. Netlib,
    // which omits the RHS vector name) fall through to the fixed offsets.
    std::istringstream iss(line);
    std::vector<std::string> tok;
    { std::string t; while (iss >> t) tok.push_back(t); }

    if (tok.size() == 3 && !is_numeric(tok[0]) && !is_numeric(tok[1]) &&
        is_numeric(tok[2])) {
        return std::make_tuple(tok[0], tok[1], std::stod(tok[2]), std::string(), 0.0);
    }
    if (tok.size() == 5 && !is_numeric(tok[0]) && !is_numeric(tok[1]) &&
        is_numeric(tok[2]) && !is_numeric(tok[3]) && is_numeric(tok[4])) {
        return std::make_tuple(tok[0], tok[1], std::stod(tok[2]), tok[3], std::stod(tok[4]));
    }

    // Fixed-offset windows (MPS fixed format). Field 5 occupies columns 50-61
    // (zero-based offsets 49-60); this previously read field(50,12), which
    // dropped the leading character or sign of right-justified 12+ character
    // values.
    std::string name1 = field(4, 8);
    std::string name2 = field(14, 8);
    std::string val1_str = field(24, 12);
    std::string name3 = field(39, 8);
    std::string val2_str = field(49, 12);

    name1 = trim(name1);
    name2 = trim(name2);
    name3 = trim(name3);

    double val1 = 0.0, val2 = 0.0;
    try { val1 = std::stod(trim(val1_str)); } catch (...) {}
    try { val2 = std::stod(trim(val2_str)); } catch (...) {}

    return std::make_tuple(name1, name2, val1, name3, val2);
}

void write_mps(const Problem& problem, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filepath);
    }

    file << "NAME          " << problem.name << "\n";

    if (problem.obj_sense == ObjectiveSense::MAXIMIZE) {
        file << "OBJSENSE\n";
        file << "    MAX\n";
    }

    file << "ROWS\n";

    bool has_objective = false;
    for (const auto& var : problem.variables) {
        if (var.objective_coeff != 0.0) {
            has_objective = true;
            break;
        }
    }
    if (has_objective) {
        file << " N  OBJ\n";
    }

    for (const auto& con : problem.constraints) {
        char sense_char = 'L';
        if (con.sense == ConstraintSense::GE) sense_char = 'G';
        else if (con.sense == ConstraintSense::EQ) sense_char = 'E';
        file << " " << sense_char << "  " << con.name << "\n";
    }

    file << "COLUMNS\n";
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        const auto& var = problem.variables[j];
        if (var.objective_coeff != 0.0) {
            file << "    " << var.name << "  OBJ       " << var.objective_coeff << "\n";
        }
        for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
            double coeff = problem.constraint_matrix.get(i, j);
            if (coeff != 0.0) {
                file << "    " << var.name << "  " << problem.constraints[i].name << "  " << coeff << "\n";
            }
        }
    }

    file << "RHS\n";
    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        const auto& con = problem.constraints[i];
        if (con.rhs != 0.0) {
            file << "    RHS1      " << con.name << "  " << con.rhs << "\n";
        }
    }

    file << "BOUNDS\n";
    for (const auto& var : problem.variables) {
        if (var.type == VarType::BINARY) {
            file << " BV BND1      " << var.name << "\n";
        } else if (var.type == VarType::INTEGER) {
            if (var.upper_bound < std::numeric_limits<double>::infinity()) {
                file << " UI BND1      " << var.name << "  " << var.upper_bound << "\n";
            }
            if (var.lower_bound > 0.0) {
                file << " LI BND1      " << var.name << "  " << var.lower_bound << "\n";
            }
        } else {
            if (var.lower_bound > 0.0) {
                file << " LO BND1      " << var.name << "  " << var.lower_bound << "\n";
            }
            if (var.upper_bound < std::numeric_limits<double>::infinity()) {
                file << " UP BND1      " << var.name << "  " << var.upper_bound << "\n";
            }
            if (var.lower_bound == 0.0 && var.upper_bound == std::numeric_limits<double>::infinity()) {
            }
        }
    }

    if (!problem.quadratic_terms.empty()) {
        file << "\nQUADOBJ\n";
        for (const auto& term : problem.quadratic_terms) {
            const auto& r = problem.variables[term.row];
            const auto& c = problem.variables[term.col];
            file << "    " << r.name << " " << c.name << "  " << term.coeff << "\n";
        }
    }

    file << "ENDATA\n";
}

} // namespace hypernova::model