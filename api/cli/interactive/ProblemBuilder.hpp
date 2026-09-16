#pragma once

#include <model/problem.hpp>
#include <string>
#include <vector>
#include <utility>
#include <limits>

namespace hypernova::cli {

struct CoefficientEntry {
    std::size_t index;
    double value;
};

struct ValidationResult {
    bool valid = true;
    std::string message;
};

struct VariableSpec {
    std::string name;
    model::VarType type = model::VarType::CONTINUOUS;
    double lower_bound = 0.0;
    double upper_bound = std::numeric_limits<double>::infinity();
    double objective_coeff = 0.0;
};

struct ConstraintSpec {
    std::string name;
    model::ConstraintSense sense = model::ConstraintSense::LE;
    double rhs = 0.0;
    std::vector<CoefficientEntry> coefficients;
};

struct QuadraticTermSpec {
    std::size_t row;
    std::size_t col;
    double coeff;
};

struct SOSMember {
    std::size_t index;
    double weight = 0.0;
};

struct SOSConstraintSpec {
    int type;
    std::vector<SOSMember> members;
};

struct ProblemSpec {
    std::string name = "Problem";
    model::ObjectiveSense obj_sense = model::ObjectiveSense::MINIMIZE;
    std::vector<VariableSpec> variables;
    std::vector<ConstraintSpec> constraints;
    std::vector<QuadraticTermSpec> quadratic_terms;
    std::vector<SOSConstraintSpec> sos_constraints;
};

class ProblemBuilder {
public:
    static model::Problem build(const ProblemSpec& spec);

    static ValidationResult validate_variable(const VariableSpec& var,
                                               std::size_t index);

    static ValidationResult validate_constraint(const ConstraintSpec& con,
                                                 std::size_t num_variables,
                                                 std::size_t index);

    static ValidationResult validate_quadratic_term(const QuadraticTermSpec& term,
                                                     std::size_t num_variables);

    static ValidationResult validate_sos_constraint(const SOSConstraintSpec& sos,
                                                     std::size_t num_variables);
};

} // namespace hypernova::cli
