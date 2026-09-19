#pragma once

#include "problem.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cmath>
#include <limits>

namespace hypernova::model {

inline std::string var_type_to_string(VarType t) {
    switch (t) {
        case VarType::INTEGER: return "INTEGER";
        case VarType::BINARY: return "BINARY";
        case VarType::SEMI_CONTINUOUS: return "SEMI_CONTINUOUS";
        case VarType::SEMI_INTEGER: return "SEMI_INTEGER";
        default: return "CONTINUOUS";
    }
}

inline VarType string_to_var_type(const std::string& s) {
    if (s == "INTEGER") return VarType::INTEGER;
    if (s == "BINARY") return VarType::BINARY;
    if (s == "SEMI_CONTINUOUS") return VarType::SEMI_CONTINUOUS;
    if (s == "SEMI_INTEGER") return VarType::SEMI_INTEGER;
    return VarType::CONTINUOUS;
}

inline std::string sense_to_string(ConstraintSense s) {
    switch (s) {
        case ConstraintSense::GE: return "GE";
        case ConstraintSense::EQ: return "EQ";
        default: return "LE";
    }
}

inline ConstraintSense string_to_sense(const std::string& s) {
    if (s == "GE" || s == ">=") return ConstraintSense::GE;
    if (s == "EQ" || s == "=" || s == "==") return ConstraintSense::EQ;
    return ConstraintSense::LE;
}

inline std::string obj_sense_to_string(ObjectiveSense s) {
    return (s == ObjectiveSense::MAXIMIZE) ? "MAXIMIZE" : "MINIMIZE";
}

inline ObjectiveSense string_to_obj_sense(const std::string& s) {
    return (s == "MAXIMIZE" || s == "MAX") ? ObjectiveSense::MAXIMIZE : ObjectiveSense::MINIMIZE;
}

inline double json_double_val(double val) {
    if (std::isinf(val)) {
        return (val > 0) ? 1e30 : -1e30;
    }
    return val;
}

inline nlohmann::json problem_to_json(const Problem& prob) {
    nlohmann::json j;
    j["name"] = prob.name;
    j["obj_sense"] = obj_sense_to_string(prob.obj_sense);
    j["obj_offset"] = prob.obj_offset;

    nlohmann::json vars = nlohmann::json::array();
    for (const auto& v : prob.variables) {
        nlohmann::json vj;
        vj["index"] = v.index;
        vj["name"] = v.name;
        vj["type"] = var_type_to_string(v.type);
        vj["lower_bound"] = json_double_val(v.lower_bound);
        vj["upper_bound"] = json_double_val(v.upper_bound);
        vj["objective_coeff"] = v.objective_coeff;
        vars.push_back(vj);
    }
    j["variables"] = vars;

    nlohmann::json cons = nlohmann::json::array();
    for (const auto& c : prob.constraints) {
        nlohmann::json cj;
        cj["index"] = c.index;
        cj["name"] = c.name;
        cj["sense"] = sense_to_string(c.sense);
        cj["rhs"] = c.rhs;
        cj["lower_bound"] = json_double_val(c.lower_bound);
        cj["upper_bound"] = json_double_val(c.upper_bound);
        cons.push_back(cj);
    }
    j["constraints"] = cons;

    const auto& A = prob.constraint_matrix;
    j["matrix"] = {
        {"rows", A.rows()},
        {"cols", A.cols()},
        {"row_ptr", A.row_ptr()},
        {"col_indices", A.col_indices()},
        {"values", A.values()}
    };

    nlohmann::json quads = nlohmann::json::array();
    for (const auto& q : prob.quadratic_terms) {
        quads.push_back({
            {"row", q.row},
            {"col", q.col},
            {"coeff", q.coeff}
        });
    }
    j["quadratic_terms"] = quads;

    nlohmann::json sos_list = nlohmann::json::array();
    for (const auto& s : prob.sos_constraints) {
        sos_list.push_back({
            {"name", s.name},
            {"type", (s.type == SOS::Type::SOS1) ? "SOS1" : "SOS2"},
            {"variable_indices", s.variable_indices},
            {"weights", s.weights}
        });
    }
    j["sos_constraints"] = sos_list;

    return j;
}

inline Problem problem_from_json(const nlohmann::json& j) {
    Problem prob;
    prob.name = j.value("name", "model");
    prob.obj_sense = string_to_obj_sense(j.value("obj_sense", "MINIMIZE"));
    prob.obj_offset = j.value("obj_offset", 0.0);

    if (j.contains("variables")) {
        for (const auto& vj : j["variables"]) {
            Variable v;
            v.index = vj.value("index", prob.variables.size());
            v.name = vj.value("name", "x" + std::to_string(v.index));
            v.type = string_to_var_type(vj.value("type", "CONTINUOUS"));
            double lb = vj.value("lower_bound", 0.0);
            double ub = vj.value("upper_bound", 1e30);
            v.lower_bound = (lb <= -1e29) ? -std::numeric_limits<double>::infinity() : lb;
            v.upper_bound = (ub >= 1e29) ? std::numeric_limits<double>::infinity() : ub;
            v.objective_coeff = vj.value("objective_coeff", 0.0);
            prob.variables.push_back(v);
        }
    }

    if (j.contains("constraints")) {
        for (const auto& cj : j["constraints"]) {
            Constraint c;
            c.index = cj.value("index", prob.constraints.size());
            c.name = cj.value("name", "c" + std::to_string(c.index));
            c.sense = string_to_sense(cj.value("sense", "LE"));
            c.rhs = cj.value("rhs", 0.0);
            double lb = cj.value("lower_bound", -1e30);
            double ub = cj.value("upper_bound", 1e30);
            c.lower_bound = (lb <= -1e29) ? -std::numeric_limits<double>::infinity() : lb;
            c.upper_bound = (ub >= 1e29) ? std::numeric_limits<double>::infinity() : ub;
            prob.constraints.push_back(c);
        }
    }

    if (j.contains("matrix")) {
        const auto& mj = j["matrix"];
        std::size_t rows = mj.value("rows", prob.constraints.size());
        std::size_t cols = mj.value("cols", prob.variables.size());
        std::vector<std::size_t> row_ptr = mj.value("row_ptr", std::vector<std::size_t>{});
        std::vector<std::size_t> col_indices = mj.value("col_indices", std::vector<std::size_t>{});
        std::vector<double> values = mj.value("values", std::vector<double>{});

        SparseMatrix mat(rows, cols, StorageOrder::CSR);
        mat.mutable_row_ptr() = row_ptr;
        mat.mutable_col_indices() = col_indices;
        mat.mutable_values() = values;
        prob.constraint_matrix = std::move(mat);
    }

    if (j.contains("quadratic_terms")) {
        for (const auto& qj : j["quadratic_terms"]) {
            prob.quadratic_terms.push_back({
                qj.value("row", std::size_t(0)),
                qj.value("col", std::size_t(0)),
                qj.value("coeff", 0.0)
            });
        }
    }

    if (j.contains("sos_constraints")) {
        for (const auto& sj : j["sos_constraints"]) {
            SOS s;
            s.name = sj.value("name", "");
            std::string stype = sj.value("type", "SOS1");
            s.type = (stype == "SOS2") ? SOS::Type::SOS2 : SOS::Type::SOS1;
            s.variable_indices = sj.value("variable_indices", std::vector<std::size_t>{});
            s.weights = sj.value("weights", std::vector<double>{});
            prob.sos_constraints.push_back(s);
        }
    }

    return prob;
}

} // namespace hypernova::model
