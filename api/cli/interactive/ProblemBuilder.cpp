#include "ProblemBuilder.hpp"
#include "PromptEngine.hpp"
#include <algorithm>
#include <cmath>

namespace hypernova::cli {

model::Problem ProblemBuilder::build(const ProblemSpec& spec) {
    model::ProblemBuilder builder(spec.name);
    builder.set_objective({}, spec.obj_sense);

    for (std::size_t i = 0; i < spec.variables.size(); ++i) {
        const auto& var = spec.variables[i];
        double lb = var.lower_bound;
        double ub = var.upper_bound;

        if (var.type == model::VarType::BINARY) {
            lb = 0.0;
            ub = 1.0;
        }

        builder.add_variable(lb, ub, var.type, var.name);
    }

    for (std::size_t i = 0; i < spec.variables.size(); ++i) {
        model::Problem& p = builder.get_problem();
        if (i < p.variables.size()) {
            p.variables[i].objective_coeff = spec.variables[i].objective_coeff;
        }
    }

    for (const auto& con : spec.constraints) {
        std::vector<std::pair<std::size_t, double>> coeffs;
        for (const auto& entry : con.coefficients) {
            coeffs.push_back({entry.index, entry.value});
        }
        builder.add_constraint(coeffs, con.sense, con.rhs, con.name);
    }

    for (const auto& term : spec.quadratic_terms) {
        builder.add_quadratic_term(term.row, term.col, term.coeff);
    }

    for (const auto& sos : spec.sos_constraints) {
        std::vector<std::size_t> vars;
        std::vector<double> weights;
        for (const auto& member : sos.members) {
            vars.push_back(member.index);
            weights.push_back(member.weight);
        }
        if (sos.type == 1) {
            builder.add_sos1(vars, weights);
        } else {
            builder.add_sos2(vars, weights);
        }
    }

    return builder.build();
}

ValidationResult ProblemBuilder::validate_variable(const VariableSpec& var,
                                                     std::size_t /*index*/) {
    if (var.name.empty()) {
        return {false, "Variable name cannot be empty"};
    }

    if (!PromptEngine::is_valid_identifier(var.name)) {
        return {false, "Invalid variable name: " + var.name};
    }

    if (var.type != model::VarType::CONTINUOUS &&
        var.type != model::VarType::INTEGER &&
        var.type != model::VarType::BINARY) {
        return {false, "Invalid variable type"};
    }

    if (var.type == model::VarType::BINARY) {
        if (var.lower_bound < 0.0 || var.upper_bound > 1.0) {
            return {false, "Binary variable bounds must be [0, 1]"};
        }
    }

    if (var.lower_bound > var.upper_bound) {
        return {false, "Lower bound exceeds upper bound"};
    }

    return {true, ""};
}

ValidationResult ProblemBuilder::validate_constraint(const ConstraintSpec& con,
                                                       std::size_t num_variables,
                                                       std::size_t /*index*/) {
    if (con.name.empty()) {
        return {false, "Constraint name cannot be empty"};
    }

    if (!PromptEngine::is_valid_identifier(con.name)) {
        return {false, "Invalid constraint name: " + con.name};
    }

    if (!std::isfinite(con.rhs)) {
        return {false, "RHS value must be finite"};
    }

    for (const auto& coeff : con.coefficients) {
        if (coeff.index >= num_variables) {
            return {false, "Coefficient index out of range: " + std::to_string(coeff.index)};
        }

        if (!std::isfinite(coeff.value)) {
            return {false, "Coefficient value must be finite"};
        }
    }

    return {true, ""};
}

ValidationResult ProblemBuilder::validate_quadratic_term(const QuadraticTermSpec& term,
                                                           std::size_t num_variables) {
    if (term.row >= num_variables) {
        return {false, "Row index out of range: " + std::to_string(term.row)};
    }

    if (term.col >= num_variables) {
        return {false, "Column index out of range: " + std::to_string(term.col)};
    }

    if (!std::isfinite(term.coeff)) {
        return {false, "Quadratic coefficient must be finite"};
    }

    return {true, ""};
}

ValidationResult ProblemBuilder::validate_sos_constraint(const SOSConstraintSpec& sos,
                                                           std::size_t num_variables) {
    if (sos.type != 1 && sos.type != 2) {
        return {false, "SOS type must be 1 or 2"};
    }

    if (sos.members.size() < 2) {
        return {false, "SOS constraint must have at least 2 members"};
    }

    for (const auto& member : sos.members) {
        if (member.index >= num_variables) {
            return {false, "SOS member index out of range: " + std::to_string(member.index)};
        }
    }

    std::vector<std::size_t> indices;
    for (const auto& member : sos.members) {
        indices.push_back(member.index);
    }
    std::sort(indices.begin(), indices.end());
    auto last = std::unique(indices.begin(), indices.end());
    if (last != indices.end()) {
        return {false, "SOS member indices must be unique"};
    }

    return {true, ""};
}

} // namespace hypernova::cli
