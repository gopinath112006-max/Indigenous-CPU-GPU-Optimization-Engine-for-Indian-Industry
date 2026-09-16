#pragma once

#include "sparse_matrix.hpp"
#include <vector>
#include <string>
#include <cstddef>
#include <limits>
#include <memory>
#include <variant>

namespace hypernova::model {

using hypernova::numerical::SparseMatrix;
using hypernova::numerical::StorageOrder;

enum class VarType {
    CONTINUOUS,
    INTEGER,
    BINARY,
    SEMI_CONTINUOUS,
    SEMI_INTEGER
};

enum class ConstraintSense {
    LE,   // <=
    GE,   // >=
    EQ    // ==
};

enum class ObjectiveSense {
    MINIMIZE,
    MAXIMIZE
};

enum class ProblemStatus {
    UNKNOWN,
    OPTIMAL,
    INFEASIBLE,
    UNBOUNDED,
    SUBOPTIMAL,
    TIME_LIMIT,
    ITER_LIMIT,
    NUMERICAL_ERROR,
    INTERRUPTED
};

struct Variable {
    std::string name;
    VarType type = VarType::CONTINUOUS;
    double lower_bound = 0.0;
    double upper_bound = std::numeric_limits<double>::infinity();
    double objective_coeff = 0.0;
    std::size_t index = 0;
};

struct Constraint {
    std::string name;
    ConstraintSense sense = ConstraintSense::LE;
    double rhs = 0.0;
    double lower_bound = -std::numeric_limits<double>::infinity();
    double upper_bound = std::numeric_limits<double>::infinity();
    std::size_t index = 0;
};

struct SOS {
    enum class Type { SOS1, SOS2 };
    Type type = Type::SOS1;
    std::vector<std::size_t> variable_indices;
    std::vector<double> weights;
    std::string name;
};

struct QuadTerm {
    std::size_t row;
    std::size_t col;
    double coeff;
};

struct Problem {
    std::string name;
    ObjectiveSense obj_sense = ObjectiveSense::MINIMIZE;
    double obj_offset = 0.0;

    std::vector<Variable> variables;
    std::vector<Constraint> constraints;
    std::vector<SOS> sos_constraints;

    SparseMatrix constraint_matrix;
    std::vector<QuadTerm> quadratic_terms;

    std::size_t num_continuous_vars() const {
        std::size_t count = 0;
        for (const auto& v : variables) {
            if (v.type == VarType::CONTINUOUS) ++count;
        }
        return count;
    }

    std::size_t num_integer_vars() const {
        std::size_t count = 0;
        for (const auto& v : variables) {
            if (v.type == VarType::INTEGER || v.type == VarType::BINARY) ++count;
        }
        return count;
    }

    std::size_t num_binary_vars() const {
        std::size_t count = 0;
        for (const auto& v : variables) {
            if (v.type == VarType::BINARY) ++count;
        }
        return count;
    }

    bool is_lp() const {
        return num_integer_vars() == 0 && quadratic_terms.empty();
    }

    bool is_milp() const {
        return num_integer_vars() > 0 && quadratic_terms.empty();
    }

    bool is_qp() const {
        return num_integer_vars() == 0 && !quadratic_terms.empty();
    }

    bool is_miqp() const {
        return num_integer_vars() > 0 && !quadratic_terms.empty();
    }

    std::vector<double> get_lb() const {
        std::vector<double> lb(variables.size());
        for (std::size_t i = 0; i < variables.size(); ++i) {
            lb[i] = variables[i].lower_bound;
        }
        return lb;
    }

    std::vector<double> get_ub() const {
        std::vector<double> ub(variables.size());
        for (std::size_t i = 0; i < variables.size(); ++i) {
            ub[i] = variables[i].upper_bound;
        }
        return ub;
    }

    std::vector<double> get_obj() const {
        std::vector<double> obj(variables.size());
        for (std::size_t i = 0; i < variables.size(); ++i) {
            obj[i] = variables[i].objective_coeff;
        }
        return obj;
    }

    std::vector<double> get_rhs() const {
        std::vector<double> rhs(constraints.size());
        for (std::size_t i = 0; i < constraints.size(); ++i) {
            rhs[i] = constraints[i].rhs;
        }
        return rhs;
    }

    std::vector<ConstraintSense> get_senses() const {
        std::vector<ConstraintSense> senses(constraints.size());
        for (std::size_t i = 0; i < constraints.size(); ++i) {
            senses[i] = constraints[i].sense;
        }
        return senses;
    }
};

struct Solution {
    ProblemStatus status = ProblemStatus::UNKNOWN;
    double objective_value = 0.0;
    double best_bound = 0.0;
    double gap = 0.0;

    std::vector<double> primal;
    std::vector<double> dual;
    std::vector<double> reduced_costs;
    std::vector<int> basis_status;

    std::size_t simplex_iterations = 0;
    std::size_t ipm_iterations = 0;
    std::size_t bb_nodes = 0;
    double solve_time_ms = 0.0;

    bool is_optimal() const { return status == ProblemStatus::OPTIMAL; }
    bool is_feasible() const { return status == ProblemStatus::OPTIMAL || status == ProblemStatus::SUBOPTIMAL; }
};

struct Basis {
    std::vector<int> var_status;
    std::vector<int> con_status;

    static Basis from_solution(const Solution& sol, const Problem& prob);
};

class ProblemBuilder {
public:
    ProblemBuilder() = default;
    explicit ProblemBuilder(const std::string& name) { prob_.name = name; }

    std::size_t add_variable(double lb = 0.0, double ub = std::numeric_limits<double>::infinity(),
                              VarType type = VarType::CONTINUOUS, const std::string& name = "") {
        Variable var;
        var.index = prob_.variables.size();
        var.lower_bound = lb;
        var.upper_bound = ub;
        var.type = type;
        var.name = name.empty() ? "x" + std::to_string(var.index) : name;
        prob_.variables.push_back(var);
        return var.index;
    }

    std::size_t add_constraint(const std::vector<std::pair<std::size_t, double>>& coeffs,
                                ConstraintSense sense, double rhs,
                                const std::string& name = "") {
        Constraint con;
        con.index = prob_.constraints.size();
        con.sense = sense;
        con.rhs = rhs;
        con.name = name.empty() ? "c" + std::to_string(con.index) : name;
        prob_.constraints.push_back(con);

        for (const auto& [var_idx, coeff] : coeffs) {
            prob_.constraint_matrix.mutable_values().push_back(coeff);
            prob_.constraint_matrix.mutable_col_indices().push_back(var_idx);
        }
        prob_.constraint_matrix.mutable_row_ptr().push_back(prob_.constraint_matrix.values().size());

        return con.index;
    }

    void set_objective(const std::vector<std::pair<std::size_t, double>>& coeffs,
                        ObjectiveSense sense = ObjectiveSense::MINIMIZE) {
        prob_.obj_sense = sense;
        for (const auto& [var_idx, coeff] : coeffs) {
            if (var_idx < prob_.variables.size()) {
                prob_.variables[var_idx].objective_coeff = coeff;
            }
        }
    }

    void add_quadratic_term(std::size_t row, std::size_t col, double coeff) {
        prob_.quadratic_terms.push_back({row, col, coeff});
    }

    void add_sos1(const std::vector<std::size_t>& vars, const std::vector<double>& weights, const std::string& name = "") {
        SOS sos;
        sos.type = SOS::Type::SOS1;
        sos.variable_indices = vars;
        sos.weights = weights;
        sos.name = name;
        prob_.sos_constraints.push_back(sos);
    }

    void add_sos2(const std::vector<std::size_t>& vars, const std::vector<double>& weights, const std::string& name = "") {
        SOS sos;
        sos.type = SOS::Type::SOS2;
        sos.variable_indices = vars;
        sos.weights = weights;
        sos.name = name;
        prob_.sos_constraints.push_back(sos);
    }

    Problem build() {
        prob_.constraint_matrix.mutable_rows() = prob_.constraints.size();
        prob_.constraint_matrix.mutable_cols() = prob_.variables.size();

        auto& rp = prob_.constraint_matrix.mutable_row_ptr();
        if (!rp.empty()) {
            rp.insert(rp.begin(), 0);
        } else if (!prob_.constraints.empty()) {
            rp.resize(prob_.constraints.size() + 1, prob_.constraint_matrix.values().size());
        }

        prob_.constraint_matrix.sort_indices();
        prob_.constraint_matrix.sum_duplicates();
        return std::move(prob_);
    }

    Problem& get_problem() {
        return prob_;
    }
    const Problem& get_problem() const {
        return prob_;
    }

private:
    Problem prob_;
};

} // namespace hypernova::model