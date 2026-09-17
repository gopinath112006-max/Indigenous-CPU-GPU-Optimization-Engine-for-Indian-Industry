#include "lp_parser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <limits>
#include <regex>

namespace hypernova::model {

LPParseResult LPParser::parse_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LPParseResult result;
        result.errors.push_back("Cannot open file: " + filepath);
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_string(buffer.str());
}

LPParseResult LPParser::parse_string(const std::string& content) {
    result_ = LPParseResult{};
    builder_ = ProblemBuilder{};
    var_name_to_idx_.clear();
    con_name_to_idx_.clear();
    obj_sense_ = ObjectiveSense::MINIMIZE;
    obj_buf_.clear();
    con_acc_.clear();
    con_acc_name_.clear();

    Section current_section = Section::NONE;
    std::istringstream stream(content);
    std::string line;
    std::size_t line_num = 0;

    while (std::getline(stream, line)) {
        ++line_num;
        std::string trimmed = trim(line);

        if (trimmed.empty() || trimmed[0] == '\\' || (trimmed.size() >= 2 && trimmed[0] == '*' && trimmed[1] == '*')) continue;

        Section new_section = detect_section(trimmed);
        if (new_section != Section::NONE) {
            if (current_section == Section::OBJ) finalize_obj();
            if (current_section == Section::CONSTRAINTS) finalize_constraint();
            current_section = new_section;
            continue;
        }

        if (current_section == Section::END) break;

        try {
            switch (current_section) {
                case Section::OBJ: {
                    std::string expr = trimmed;
                    std::size_t colon_pos = expr.find(':');
                    if (colon_pos != std::string::npos) {
                        expr = expr.substr(colon_pos + 1);
                    }
                    obj_buf_ += " " + expr;
                    break;
                }
                case Section::CONSTRAINTS: {
                    if (trimmed.find(':') != std::string::npos) {
                        if (!con_acc_.empty()) {
                            result_.errors.push_back("Unterminated constraint before: " + trimmed);
                            con_acc_.clear();
                            con_acc_name_.clear();
                        }
                        std::size_t colon_pos = trimmed.find(':');
                        con_acc_name_ = trim(trimmed.substr(0, colon_pos));
                        con_acc_ = trim(trimmed.substr(colon_pos + 1));
                    } else {
                        con_acc_ += " " + trimmed;
                    }
                    bool has_sense = con_acc_.find("<=") != std::string::npos ||
                                     con_acc_.find(">=") != std::string::npos ||
                                     con_acc_.find('=') != std::string::npos ||
                                     con_acc_.find("==") != std::string::npos;
                    if (has_sense) finalize_constraint();
                    break;
                }
                case Section::BOUNDS: parse_bounds(trimmed); break;
                case Section::GENERAL: parse_general(trimmed); break;
                case Section::BINARY: parse_binary(trimmed); break;
                case Section::SOS: parse_sos(trimmed); break;
                case Section::QUADRATIC: parse_quadratic(trimmed); break;
                default: break;
            }
        } catch (const std::exception& e) {
            result_.errors.push_back("Line " + std::to_string(line_num) + ": " + e.what());
        }
    }

    if (current_section == Section::OBJ) finalize_obj();
    if (current_section == Section::CONSTRAINTS) finalize_constraint();

    if (!result_.errors.empty()) {
        result_.success = false;
    } else {
        builder_.get_problem().obj_sense = obj_sense_;
        result_.problem = builder_.build();
        result_.success = true;
    }

    return result_;
}

LPParser::Section LPParser::detect_section(const std::string& line) {
    std::string upper = line;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    if (upper == "MINIMIZE" || upper == "MAXIMIZE" || upper == "MIN" || upper == "MAX") {
        obj_sense_ = (upper == "MAXIMIZE" || upper == "MAX") ? ObjectiveSense::MAXIMIZE : ObjectiveSense::MINIMIZE;
        return Section::OBJ;
    }
    if (upper == "SUBJECT TO" || upper == "ST" || upper == "S.T." || upper == "CONSTRAINTS") return Section::CONSTRAINTS;
    if (upper == "BOUNDS") return Section::BOUNDS;
    if (upper == "GENERAL" || upper == "GENERALS") return Section::GENERAL;
    if (upper == "BINARY" || upper == "BINARIES") return Section::BINARY;
    if (upper == "SEMI-CONTINUOUS" || upper == "SEMI-CONT") return Section::SEMI_CONT;
    if (upper == "SEMI-INTEGER" || upper == "SEMI-INT") return Section::SEMI_INT;
    if (upper == "SOS") return Section::SOS;
    if (upper == "QUADRATIC" || upper == "Q") return Section::QUADRATIC;
    if (upper == "END") return Section::END;
    return Section::NONE;
}

void LPParser::finalize_obj() {
    if (obj_buf_.empty()) return;

    std::string linear;
    std::vector<std::pair<double, std::string>> qgroups;
    std::string cur;
    int depth = 0;
    double group_sign = 1.0;

    std::size_t i = 0;
    const std::size_t n = obj_buf_.size();
    while (i < n) {
        char c = obj_buf_[i];
        if (c == '[') {
            if (depth == 0) {
                std::size_t k = cur.find_last_not_of(" \t");
                if (k != std::string::npos && (cur[k] == '+' || cur[k] == '-')) {
                    group_sign = (cur[k] == '-') ? -1.0 : 1.0;
                    cur.erase(k);
                } else {
                    group_sign = 1.0;
                }
            }
            ++depth;
            ++i;
            continue;
        }
        if (c == ']') {
            if (depth > 0) {
                --depth;
                if (depth == 0) {
                    qgroups.emplace_back(group_sign, trim(cur));
                    cur.clear();
                    group_sign = 1.0;
                }
            }
            ++i;
            continue;
        }
        if (depth > 0) cur += c;
        else linear += c;
        ++i;
    }
    if (depth > 0 && !trim(cur).empty()) {
        qgroups.emplace_back(group_sign, trim(cur));
    }

    auto lterms = parse_linear_expr(linear);
    Problem& prob = builder_.get_problem();
    for (const auto& [idx, coeff] : lterms) {
        if (idx < prob.variables.size()) {
            prob.variables[idx].objective_coeff = coeff;
        }
    }
    for (const auto& [sg, content] : qgroups) {
        if (content.empty()) continue;
        for (const auto& term : parse_quadratic_expr(content)) {
            double coeff = sg * term.coeff;
            if (term.row == term.col) coeff *= 2.0;
            builder_.add_quadratic_term(term.row, term.col, coeff);
        }
    }
    obj_buf_.clear();
}

void LPParser::finalize_constraint() {
    if (con_acc_.empty() && con_acc_name_.empty()) return;

    std::string expr = con_acc_;
    ConstraintSense sense = ConstraintSense::LE;
    std::size_t sense_pos = std::string::npos;
    for (std::size_t i = 0; i + 1 < expr.size(); ++i) {
        if (expr[i] == '<' && expr[i+1] == '=') { sense_pos = i; sense = ConstraintSense::LE; break; }
        if (expr[i] == '>' && expr[i+1] == '=') { sense_pos = i; sense = ConstraintSense::GE; break; }
    }
    if (sense_pos == std::string::npos) {
        sense_pos = expr.find('=');
        if (sense_pos != std::string::npos) sense = ConstraintSense::EQ;
    }

    if (sense_pos == std::string::npos) {
        result_.errors.push_back("Invalid constraint format: " + expr);
        con_acc_.clear();
        con_acc_name_.clear();
        return;
    }

    std::string lhs = trim(expr.substr(0, sense_pos));
    std::string rhs_str = trim(expr.substr(sense_pos + (sense == ConstraintSense::EQ ? 1 : 2)));
    if (rhs_str.empty()) {
        return;
    }
    double rhs = 0.0;
    try {
        rhs = std::stod(rhs_str);
    } catch (const std::exception&) {
        result_.errors.push_back("Invalid constraint RHS: " + rhs_str);
        con_acc_.clear();
        con_acc_name_.clear();
        return;
    }

    auto terms = parse_linear_expr(lhs);
    builder_.add_constraint(terms, sense, rhs, keep_names_ ? con_acc_name_ : "");
    con_name_to_idx_[con_acc_name_] = builder_.get_problem().constraints.size() - 1;
    con_acc_.clear();
    con_acc_name_.clear();
}

void LPParser::parse_bounds(const std::string& line) {
    std::string var_name;
    double lb = -std::numeric_limits<double>::infinity();
    double ub = std::numeric_limits<double>::infinity();
    bool has_lb = false, has_ub = false, has_fixed = false;

    auto to_double = [&](const std::string& tok) -> double {
        std::string t = trim(tok);
        std::string lower = t;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "inf" || lower == "+inf" || lower == "infinity") {
            return std::numeric_limits<double>::infinity();
        }
        if (lower == "-inf" || lower == "-infinity") {
            return -std::numeric_limits<double>::infinity();
        }
        return std::stod(t);
    };

    static const std::regex twosided_regex(
        R"(^\s*(-?(?:\d+\.?\d*|\.\d+|inf))\s*<=\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*<=\s*(-?(?:\d+\.?\d*|\.\d+|inf))\s*$)");
    static const std::regex lb_regex(
        R"(^\s*(-?(?:\d+\.?\d*|\.\d+|inf))\s*<=\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*$)");
    static const std::regex ub_regex(
        R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*<=\s*(-?(?:\d+\.?\d*|\.\d+|inf))\s*$)");
    static const std::regex ge_regex(
        R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*>=\s*(-?(?:\d+\.?\d*|\.\d+|inf))\s*$)");
    static const std::regex eq_regex(
        R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(-?(?:\d+\.?\d*|\.\d+|inf))\s*$)");

    std::smatch match;
    if (std::regex_match(line, match, twosided_regex)) {
        lb = to_double(match[1].str());
        var_name = match[2].str();
        ub = to_double(match[3].str());
        has_lb = has_ub = true;
    } else if (std::regex_match(line, match, lb_regex)) {
        lb = to_double(match[1].str());
        var_name = match[2].str();
        has_lb = true;
    } else if (std::regex_match(line, match, ub_regex)) {
        var_name = match[1].str();
        ub = to_double(match[2].str());
        has_ub = true;
    } else if (std::regex_match(line, match, ge_regex)) {
        var_name = match[1].str();
        lb = to_double(match[2].str());
        has_lb = true;
    } else if (std::regex_match(line, match, eq_regex)) {
        var_name = match[1].str();
        double val = to_double(match[2].str());
        lb = ub = val;
        has_lb = has_ub = true;
        has_fixed = true;
    } else {
        var_name = trim(line);
    }

    auto& prob = builder_.get_problem();
    for (std::size_t j = 0; j < prob.variables.size(); ++j) {
        if (prob.variables[j].name == var_name) {
            if (has_fixed) {
                prob.variables[j].lower_bound = lb;
                prob.variables[j].upper_bound = ub;
            } else {
                if (has_lb) prob.variables[j].lower_bound = lb;
                if (has_ub) prob.variables[j].upper_bound = ub;
            }
            break;
        }
    }
}

void LPParser::parse_general(const std::string& line) {
    std::istringstream iss(line);
    std::string var_name;
    while (iss >> var_name) {
        auto& prob = builder_.get_problem();
        for (std::size_t j = 0; j < prob.variables.size(); ++j) {
            if (prob.variables[j].name == var_name) {
                prob.variables[j].type = VarType::INTEGER;
                break;
            }
        }
    }
}

void LPParser::parse_binary(const std::string& line) {
    std::istringstream iss(line);
    std::string var_name;
    while (iss >> var_name) {
        auto& prob = builder_.get_problem();
        for (std::size_t j = 0; j < prob.variables.size(); ++j) {
            if (prob.variables[j].name == var_name) {
                prob.variables[j].type = VarType::BINARY;
                prob.variables[j].lower_bound = 0.0;
                prob.variables[j].upper_bound = 1.0;
                break;
            }
        }
    }
}

void LPParser::parse_sos(const std::string& line) {
    std::istringstream iss(line);
    std::string sos_type, sos_name;
    iss >> sos_type >> sos_name;

    SOS::Type type = (sos_type == "S1") ? SOS::Type::SOS1 : SOS::Type::SOS2;
    std::vector<std::size_t> vars;
    std::vector<double> weights;

    std::string var_name;
    double weight;
    while (iss >> var_name >> weight) {
        auto& prob = builder_.get_problem();
        for (std::size_t j = 0; j < prob.variables.size(); ++j) {
            if (prob.variables[j].name == var_name) {
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

void LPParser::parse_quadratic(const std::string& line) {
    auto terms = parse_quadratic_expr(line);
    for (const auto& term : terms) {
        builder_.add_quadratic_term(term.row, term.col, term.coeff);
    }
}

std::vector<std::pair<std::size_t, double>> LPParser::parse_linear_expr(const std::string& expr) {
    std::vector<std::pair<std::size_t, double>> terms;

    std::regex term_regex(R"(([+-]?)\s*(\d*\.?\d+)?\s*([a-zA-Z_][a-zA-Z0-9_]*)?)");
    std::smatch match;
    std::string s = expr;
    std::size_t pos = 0;

    while (pos < s.size()) {
        while (pos < s.size() && std::isspace(s[pos])) ++pos;
        if (pos >= s.size()) break;

        double term_sign = 1.0;
        if (s[pos] == '+') { term_sign = 1.0; ++pos; }
        else if (s[pos] == '-') { term_sign = -1.0; ++pos; }

        while (pos < s.size() && std::isspace(s[pos])) ++pos;

        std::size_t old_pos = pos;

        double coeff = 1.0;
        std::size_t coeff_end = pos;
        while (coeff_end < s.size() && (std::isdigit(s[coeff_end]) || s[coeff_end] == '.')) ++coeff_end;
        if (coeff_end < s.size() && (s[coeff_end] == 'e' || s[coeff_end] == 'E')) {
            std::size_t e_next = coeff_end + 1;
            if (e_next < s.size() && (s[e_next] == '+' || s[e_next] == '-')) ++e_next;
            std::size_t e_digits = e_next;
            while (e_digits < s.size() && std::isdigit(s[e_digits])) ++e_digits;
            if (e_digits > e_next) coeff_end = e_digits;
        }
        if (coeff_end > pos) {
            coeff = std::stod(s.substr(pos, coeff_end - pos));
            pos = coeff_end;
        }

        while (pos < s.size() && std::isspace(s[pos])) ++pos;

        std::string var_name;
        std::size_t var_start = pos;
        while (pos < s.size() && (std::isalnum(s[pos]) || s[pos] == '_')) ++pos;
        if (pos > var_start) {
            var_name = s.substr(var_start, pos - var_start);
        }

        if (!var_name.empty()) {
            auto it = var_name_to_idx_.find(var_name);
            if (it == var_name_to_idx_.end()) {
                std::size_t idx = builder_.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                                         VarType::CONTINUOUS, keep_names_ ? var_name : "");
                var_name_to_idx_[var_name] = idx;
                it = var_name_to_idx_.find(var_name);
            }
            terms.emplace_back(it->second, term_sign * coeff);
        } else if (pos == old_pos) {
            ++pos;
        }
    }

    return terms;
}

std::vector<QuadTerm> LPParser::parse_quadratic_expr(const std::string& expr) {
    std::vector<QuadTerm> terms;

    enum class Kind { NUM, NAME, SEP, POWER };
    struct Tok {
        Kind kind;
        double num = 0.0;
        std::string name;
    };

    // Tokenize the expression. The standard LP Quadratic section writes each
    // term with its coefficient either before ("2 x * x") or after
    // ("x * x 2") the product; the trailing-coefficient form is what
    // HyperNova's own writer emits.
    std::vector<Tok> toks;
    {
        std::size_t pos = 0;
        const std::size_t n = expr.size();
        auto skip_sp = [&]() {
            while (pos < n && std::isspace(static_cast<unsigned char>(expr[pos]))) ++pos;
        };
        while (pos < n) {
            skip_sp();
            if (pos >= n) break;
            char c = expr[pos];
            if (c == '\\') {  // LP comment to end of line
                break;
            }
            if (c == '*' || c == '/') {
                toks.push_back({Kind::SEP, 0.0, ""});
                ++pos;
                continue;
            }
            if (c == '^') {
                toks.push_back({Kind::POWER, 0.0, ""});
                ++pos;
                continue;
            }
            if (c == '[' || c == ']' || c == '"' || c == ',') {
                ++pos;
                continue;
            }
            double sign = 1.0;
            if (c == '+' || c == '-') {
                if (c == '-') sign = -1.0;
                ++pos;
                skip_sp();
                if (pos >= n) break;
                c = expr[pos];
            }
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                std::size_t start = pos;
                while (pos < n && (std::isdigit(static_cast<unsigned char>(expr[pos])) ||
                                   expr[pos] == '.' || expr[pos] == 'e' || expr[pos] == 'E' ||
                                   expr[pos] == '+' || expr[pos] == '-')) {
                    ++pos;
                }
                toks.push_back({Kind::NUM, sign * std::stod(expr.substr(start, pos - start)), ""});
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::size_t start = pos;
                while (pos < n && (std::isalnum(static_cast<unsigned char>(expr[pos])) ||
                                   expr[pos] == '_')) {
                    ++pos;
                }
                toks.push_back({Kind::NAME, 0.0, expr.substr(start, pos - start)});
                continue;
            }
            ++pos;  // stray character
        }
    }

    auto resolve_var = [&](const std::string& name) -> std::size_t {
        auto it = var_name_to_idx_.find(name);
        if (it == var_name_to_idx_.end()) {
            std::size_t idx = builder_.add_variable(0.0, std::numeric_limits<double>::infinity(),
                                                     VarType::CONTINUOUS, keep_names_ ? name : "");
            var_name_to_idx_[name] = idx;
            it = var_name_to_idx_.find(name);
        }
        return it->second;
    };

    std::size_t i = 0;
    const std::size_t ntoks = toks.size();
    while (i < ntoks) {
        if (toks[i].kind == Kind::SEP || toks[i].kind == Kind::POWER) {
            ++i;
            continue;
        }

        // Coefficient before the product, if present.
        double coeff = 1.0;
        bool has_leading_coeff = false;
        if (toks[i].kind == Kind::NUM) {
            coeff = toks[i].num;
            has_leading_coeff = true;
            ++i;
            while (i < ntoks && (toks[i].kind == Kind::SEP || toks[i].kind == Kind::POWER)) ++i;
        }
        if (i >= ntoks || toks[i].kind != Kind::NAME) {
            if (toks[i].kind == Kind::NUM) ++i;  // stray leading number
            continue;
        }

        std::string var1 = toks[i].name;
        ++i;
        std::string var2 = var1;

        if (i < ntoks && toks[i].kind == Kind::POWER) {
            // "x0 ^ 2": a squared variable.
            ++i;
            if (i < ntoks && toks[i].kind == Kind::NUM) ++i;
        } else if (i < ntoks && toks[i].kind == Kind::SEP) {
            ++i;
            // An explicit second variable is optional ("x * x"); without it
            // the term is the square of the first variable.
            if (i < ntoks && toks[i].kind == Kind::NAME) {
                var2 = toks[i].name;
                ++i;
            }
        } else if (i < ntoks && toks[i].kind == Kind::NAME) {
            // Implicit product "x0 x1".
            var2 = toks[i].name;
            ++i;
        }

        // Coefficient after the product ("x * x 2"), the form emitted by
        // write_lp. Only honoured when no leading coefficient was given.
        if (!has_leading_coeff && i < ntoks && toks[i].kind == Kind::NUM) {
            coeff = toks[i].num;
            ++i;
        }

        terms.push_back({resolve_var(var1), resolve_var(var2), coeff});
    }

    return terms;
}

std::string LPParser::trim(const std::string& s) {
    std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> LPParser::split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(trim(item));
    }
    return result;
}

void write_lp(const Problem& problem, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filepath);
    }

    file << "\\* LP file written by HyperNova *\\" << "\n\n";

    file << (problem.obj_sense == ObjectiveSense::MINIMIZE ? "Minimize" : "Maximize") << "\n";
    file << " OBJ:";

    bool first = true;
    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        double coeff = problem.variables[j].objective_coeff;
        if (coeff != 0.0) {
            if (!first) file << (coeff > 0 ? " + " : " - ");
            else if (coeff < 0) file << "-";
            double abs_coeff = std::abs(coeff);
            if (abs_coeff != 1.0) file << abs_coeff;
            file << " " << problem.variables[j].name;
            first = false;
        }
    }
    if (first) file << " 0";
    file << "\n\n";

    file << "Subject To\n";
    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        const auto& con = problem.constraints[i];
        file << " " << con.name << ":";

        first = true;
        for (std::size_t j = 0; j < problem.variables.size(); ++j) {
            double coeff = problem.constraint_matrix.get(i, j);
            if (coeff != 0.0) {
                if (!first) file << (coeff > 0 ? " + " : " - ");
                else if (coeff < 0) file << "-";
                double abs_coeff = std::abs(coeff);
                if (abs_coeff != 1.0) file << abs_coeff;
                file << " " << problem.variables[j].name;
                first = false;
            }
        }
        if (first) file << " 0";

        if (con.sense == ConstraintSense::LE)      file << " <= " << con.rhs << "\n";
        else if (con.sense == ConstraintSense::GE) file << " >= " << con.rhs << "\n";
        else                                       file << " = "  << con.rhs << "\n";
    }

    file << "\nBounds\n";
    for (const auto& var : problem.variables) {
        if (var.lower_bound > -std::numeric_limits<double>::infinity() &&
            var.upper_bound < std::numeric_limits<double>::infinity()) {
            if (var.lower_bound == var.upper_bound) {
                file << " " << var.lower_bound << " <= " << var.name << " <= " << var.upper_bound << "\n";
            } else {
                file << " " << var.lower_bound << " <= " << var.name << " <= " << var.upper_bound << "\n";
            }
        } else if (var.lower_bound > -std::numeric_limits<double>::infinity()) {
            file << " " << var.lower_bound << " <= " << var.name << "\n";
        } else if (var.upper_bound < std::numeric_limits<double>::infinity()) {
            file << " " << var.name << " <= " << var.upper_bound << "\n";
        } else {
            file << " -inf <= " << var.name << " <= +inf\n";
        }
    }

    std::vector<std::string> generals, binaries;
    for (const auto& var : problem.variables) {
        if (var.type == VarType::INTEGER) generals.push_back(var.name);
        else if (var.type == VarType::BINARY) binaries.push_back(var.name);
    }

    if (!generals.empty()) {
        file << "\nGeneral\n";
        for (const auto& name : generals) file << " " << name << "\n";
    }

    if (!binaries.empty()) {
        file << "\nBinary\n";
        for (const auto& name : binaries) file << " " << name << "\n";
    }

    if (!problem.sos_constraints.empty()) {
        file << "\nSOS\n";
        for (const auto& sos : problem.sos_constraints) {
            file << " " << (sos.type == SOS::Type::SOS1 ? "S1" : "S2") << " " << sos.name;
            for (std::size_t k = 0; k < sos.variable_indices.size(); ++k) {
                file << " " << problem.variables[sos.variable_indices[k]].name << " " << sos.weights[k];
            }
            file << "\n";
        }
    }

    if (!problem.quadratic_terms.empty()) {
        file << "\nQuadratic\n";
        for (const auto& term : problem.quadratic_terms) {
            double coeff = (term.row == term.col) ? 0.5 * term.coeff : term.coeff;
            file << " " << problem.variables[term.row].name << " * " << problem.variables[term.col].name << " " << coeff << "\n";
        }
    }

    file << "\nEnd\n";
}

} // namespace hypernova::model