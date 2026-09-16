#include "InteractiveSession.hpp"
#include <iostream>
#include <algorithm>
#include <limits>

namespace hypernova::cli {

InteractiveSession::InteractiveSession(const InteractiveConfig& config)
    : config_(config) {}

int InteractiveSession::run(int /*argc*/, char** /*argv*/) {
    prompt_.print_header("HyperNova Interactive Problem Builder");

    if (!config_.resume_session.empty()) {
        auto loaded = SessionManager::load_session(config_.resume_session);
        if (!loaded.has_value()) {
            prompt_.print_error("Failed to load session: " + config_.resume_session);
            return 1;
        }
        session_ = *loaded;
        prompt_.print_success("Resumed session: " + config_.resume_session);
    } else {
        session_.created_at = SessionManager::timestamp_now();
        session_.solver_options.engine = config_.engine;
        session_.solver_options.time_limit_seconds = config_.time_limit;
        session_.solver_options.mip_gap = config_.mip_gap;
        session_.solver_options.threads = config_.threads;
    }

    int result = 0;

    if (session_.variables.empty()) {
        result = collect_metadata();
        if (result != 0) return result;
    }

    if (session_.variables.empty()) {
        result = collect_variables();
        if (result != 0) return result;
    }

    if (session_.constraints.empty()) {
        result = collect_constraints();
        if (result != 0) return result;
    }

    if (config_.advanced && session_.quadratic_terms.empty()) {
        if (prompt_.prompt_bool("Add quadratic objective terms?", false)) {
            result = collect_quadratic_terms();
            if (result != 0) return result;
        }
    }

    if (config_.advanced && session_.sos_constraints.empty()) {
        if (prompt_.prompt_bool("Add Special Ordered Sets (SOS)?", false)) {
            result = collect_sos_constraints();
            if (result != 0) return result;
        }
    }

    if (session_.solver_options.engine == "auto") {
        if (prompt_.prompt_bool("Configure solver options?", false)) {
            result = collect_solver_options();
            if (result != 0) return result;
        }
    }

    display_problem_summary();

    session_.updated_at = SessionManager::timestamp_now();

    if (!config_.save_session.empty()) {
        if (SessionManager::save_session(session_, config_.save_session)) {
            prompt_.print_success("Session saved: " + config_.save_session);
        } else {
            prompt_.print_warning("Failed to save session");
        }
    } else {
        std::string session_path = SessionManager::generate_session_path(
            session_.problem_name);
        if (SessionManager::save_session(session_, session_path)) {
            prompt_.print_success("Session saved: " + session_path);
        }
    }

    result = solve_and_display();
    if (result != 0) return result;

    result = post_solve_actions();
    return result;
}

int InteractiveSession::collect_metadata() {
    prompt_.print_section("Problem Metadata");

    session_.problem_name = prompt_.prompt_string("Problem name", "MyProblem", false);

    std::string sense = prompt_.prompt_choice("Objective sense",
                                               {"min", "max"}, "min");
    session_.obj_sense = (sense == "max") ?
        model::ObjectiveSense::MAXIMIZE :
        model::ObjectiveSense::MINIMIZE;

    return 0;
}

int InteractiveSession::collect_variables() {
    prompt_.print_section("Variables");

    int nvars = prompt_.prompt_int("Number of variables", 1, 1, 100000);

    session_.variables.clear();
    session_.variables.reserve(nvars);

    for (int i = 0; i < nvars; ++i) {
        prompt_.print_info("Variable " + std::to_string(i) + ":");

        VariableSpec var;
        var.name = prompt_.prompt_string("  Name", "x" + std::to_string(i), false);

        std::string type_str = prompt_.prompt_choice("  Type", {"c", "i", "b"}, "c");
        if (type_str == "i") var.type = model::VarType::INTEGER;
        else if (type_str == "b") var.type = model::VarType::BINARY;
        else var.type = model::VarType::CONTINUOUS;

        if (var.type == model::VarType::BINARY) {
            var.lower_bound = 0.0;
            var.upper_bound = 1.0;
            prompt_.print_info("  Bounds: [0, 1] (binary)");
        } else {
            var.lower_bound = prompt_.prompt_double("  Lower bound", 0.0);
            var.upper_bound = prompt_.prompt_double("  Upper bound",
                std::numeric_limits<double>::infinity());
        }

        var.objective_coeff = prompt_.prompt_double("  Objective coefficient", 0.0);

        auto validation = ProblemBuilder::validate_variable(var, i);
        if (!validation.valid) {
            prompt_.print_error(validation.message);
            --i;
            continue;
        }

        session_.variables.push_back(var);
    }

    return 0;
}

int InteractiveSession::collect_constraints() {
    prompt_.print_section("Constraints");

    int ncons = prompt_.prompt_int("Number of constraints", 0, 0, 100000);

    session_.constraints.clear();
    session_.constraints.reserve(ncons);

    std::size_t nvars = session_.variables.size();

    for (int i = 0; i < ncons; ++i) {
        prompt_.print_info("Constraint " + std::to_string(i) + ":");

        ConstraintSpec con;
        con.name = prompt_.prompt_string("  Name", "c" + std::to_string(i), false);

        std::string sense = prompt_.prompt_choice("  Sense", {"le", "ge", "eq"}, "le");
        if (sense == "le") con.sense = model::ConstraintSense::LE;
        else if (sense == "ge") con.sense = model::ConstraintSense::GE;
        else con.sense = model::ConstraintSense::EQ;

        con.rhs = prompt_.prompt_double("  RHS", 0.0);

        con.coefficients = prompt_.prompt_sparse_coefficients(nvars, "Coefficients");

        auto validation = ProblemBuilder::validate_constraint(con, nvars, i);
        if (!validation.valid) {
            prompt_.print_error(validation.message);
            --i;
            continue;
        }

        session_.constraints.push_back(con);
    }

    return 0;
}

int InteractiveSession::collect_quadratic_terms() {
    prompt_.print_section("Quadratic Terms");

    int nterms = prompt_.prompt_int("Number of quadratic terms", 0, 0, 100000);

    session_.quadratic_terms.clear();
    session_.quadratic_terms.reserve(nterms);

    std::size_t nvars = session_.variables.size();

    for (int i = 0; i < nterms; ++i) {
        prompt_.print_info("Quadratic term " + std::to_string(i) + ":");

        QuadraticTermSpec term;
        term.row = static_cast<std::size_t>(
            prompt_.prompt_int("  Row index", 0, 0, static_cast<int>(nvars) - 1));
        term.col = static_cast<std::size_t>(
            prompt_.prompt_int("  Column index", 0, 0, static_cast<int>(nvars) - 1));
        term.coeff = prompt_.prompt_double("  Coefficient", 1.0);

        auto validation = ProblemBuilder::validate_quadratic_term(term, nvars);
        if (!validation.valid) {
            prompt_.print_error(validation.message);
            --i;
            continue;
        }

        session_.quadratic_terms.push_back(term);
    }

    return 0;
}

int InteractiveSession::collect_sos_constraints() {
    prompt_.print_section("SOS Constraints");

    int nsos = prompt_.prompt_int("Number of SOS constraints", 0, 0, 10000);

    session_.sos_constraints.clear();
    session_.sos_constraints.reserve(nsos);

    std::size_t nvars = session_.variables.size();

    for (int i = 0; i < nsos; ++i) {
        prompt_.print_info("SOS constraint " + std::to_string(i) + ":");

        SOSConstraintSpec sos;
        sos.type = prompt_.prompt_int("  Type (1 or 2)", 1, 1, 2);

        int nmembers = prompt_.prompt_int("  Number of members", 2, 2, static_cast<int>(nvars));

        sos.members.clear();
        for (int j = 0; j < nmembers; ++j) {
            SOSMember member;
            member.index = static_cast<std::size_t>(
                prompt_.prompt_int("  Member " + std::to_string(j) + " index",
                                   0, 0, static_cast<int>(nvars) - 1));
            member.weight = prompt_.prompt_double("  Weight", static_cast<double>(j));
            sos.members.push_back(member);
        }

        auto validation = ProblemBuilder::validate_sos_constraint(sos, nvars);
        if (!validation.valid) {
            prompt_.print_error(validation.message);
            --i;
            continue;
        }

        session_.sos_constraints.push_back(sos);
    }

    return 0;
}

int InteractiveSession::collect_solver_options() {
    prompt_.print_section("Solver Options");

    session_.solver_options.engine = prompt_.prompt_choice(
        "Engine", {"auto", "simplex", "dual-simplex", "ipm", "bnb"}, "auto");

    session_.solver_options.time_limit_seconds = prompt_.prompt_double(
        "Time limit (seconds)", 3600.0, 0.0, 86400.0);

    session_.solver_options.mip_gap = prompt_.prompt_double(
        "MIP gap tolerance", 1e-4, 0.0, 1.0);

    session_.solver_options.threads = prompt_.prompt_int(
        "Threads (0=auto)", 0, 0, 256);

    return 0;
}

int InteractiveSession::solve_and_display() {
    prompt_.print_section("Solving");

    std::cerr << "[DEBUG] solve_and_display: session_.constraints.size() = " << session_.constraints.size() << std::endl;
    model::Problem problem = build_problem();

    std::cerr << "[DEBUG] solve_and_display: problem.constraints.size() = " << problem.constraints.size() << std::endl;
    std::cerr << "[DEBUG] solve_and_display: problem.variables.size() = " << problem.variables.size() << std::endl;
    prompt_.print_info("Variables: " + std::to_string(problem.variables.size()));
    prompt_.print_info("Constraints: " + std::to_string(problem.constraints.size()));
    prompt_.print_info("Integer variables: " + std::to_string(problem.num_integer_vars()));
    std::cout << std::endl;

    hypernova::SolverOptions solver_opts;
    if (session_.solver_options.engine == "simplex") {
        solver_opts.engine = hypernova::EngineType::PRIMAL_SIMPLEX;
    } else if (session_.solver_options.engine == "dual-simplex") {
        solver_opts.engine = hypernova::EngineType::DUAL_SIMPLEX;
    } else if (session_.solver_options.engine == "ipm") {
        solver_opts.engine = hypernova::EngineType::INTERIOR_POINT;
    } else if (session_.solver_options.engine == "bnb") {
        solver_opts.engine = hypernova::EngineType::BRANCH_AND_BOUND;
    } else {
        solver_opts.engine = hypernova::EngineType::AUTO;
    }

    solver_opts.time_limit_seconds = session_.solver_options.time_limit_seconds;
    solver_opts.mip_gap_tolerance = session_.solver_options.mip_gap;
    solver_opts.thread_count = session_.solver_options.threads;

    hypernova::Solver solver(solver_opts);

    std::cerr << "[DEBUG] solve_and_display: Calling solver.solve()" << std::endl;
    try {
        auto start = std::chrono::high_resolution_clock::now();
        hypernova::Solution solution = solver.solve(problem);
        auto end = std::chrono::high_resolution_clock::now();
        std::cerr << "[DEBUG] solve_and_display: solver.solve() returned, status = " << static_cast<int>(solution.status) << std::endl;

        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        display_solution(problem, solution.objective_value, solution.primal);

        prompt_.print_blank();
        prompt_.print_info("Solve time: " + std::to_string(elapsed_ms) + " ms");
    } catch (const std::exception& e) {
        prompt_.print_error("Solver failed: " + std::string(e.what()));
        return 1;
    }

    return 0;
}

int InteractiveSession::post_solve_actions() {
    prompt_.print_section("Export");

    std::string format = config_.output_format;
    if (format.empty()) {
        if (prompt_.prompt_bool("Export problem to file?", false)) {
            format = prompt_.prompt_choice("Format", {"mps", "lp", "json"}, "mps");
        }
    }

    if (!format.empty()) {
        model::Problem problem = build_problem();

        std::string filepath = config_.output_file;
        if (filepath.empty()) {
            filepath = ExportManager::default_output_path(session_.problem_name, format);
            filepath = prompt_.prompt_string("Output file", filepath, false);
        }

        bool success = false;
        if (format == "mps") {
            success = ExportManager::export_mps(problem, filepath);
        } else if (format == "lp") {
            success = ExportManager::export_lp(problem, filepath);
        } else if (format == "json") {
            success = ExportManager::export_json(problem, filepath);
        }

        if (success) {
            prompt_.print_success("Exported to " + filepath);
        } else {
            prompt_.print_error("Failed to export to " + filepath);
        }
    }

    return 0;
}

void InteractiveSession::display_problem_summary() {
    prompt_.print_section("Problem Summary");

    prompt_.print_info("Name: " + session_.problem_name);
    prompt_.print_info("Objective: " +
        std::string(session_.obj_sense == model::ObjectiveSense::MAXIMIZE ?
                    "Maximize" : "Minimize"));
    prompt_.print_info("Variables: " + std::to_string(session_.variables.size()));
    prompt_.print_info("Constraints: " + std::to_string(session_.constraints.size()));

    std::size_t nint = 0;
    for (const auto& var : session_.variables) {
        if (var.type == model::VarType::INTEGER || var.type == model::VarType::BINARY) {
            ++nint;
        }
    }
    prompt_.print_info("Integer variables: " + std::to_string(nint));

    if (!session_.quadratic_terms.empty()) {
        prompt_.print_info("Quadratic terms: " + std::to_string(session_.quadratic_terms.size()));
    }

    prompt_.print_separator();
}

void InteractiveSession::display_solution(const model::Problem& problem,
                                           double objective_value,
                                           const std::vector<double>& primal) {
    prompt_.print_section("Solution");

    prompt_.print_info("Status: SOLVED");
    prompt_.print_info("Objective: " + std::to_string(objective_value));

    if (!primal.empty()) {
        prompt_.print_blank();
        prompt_.print_info("Variable values:");
        for (std::size_t i = 0; i < primal.size() && i < problem.variables.size(); ++i) {
            prompt_.print_info("  " + problem.variables[i].name +
                              " = " + std::to_string(primal[i]));
        }
    } else {
        prompt_.print_warning("No primal values returned");
    }

    std::cout << std::flush;
}

model::Problem InteractiveSession::build_problem() {
    ProblemSpec spec;
    spec.name = session_.problem_name;
    spec.obj_sense = session_.obj_sense;
    spec.variables = session_.variables;
    spec.constraints = session_.constraints;
    spec.quadratic_terms = session_.quadratic_terms;
    spec.sos_constraints = session_.sos_constraints;

    return ProblemBuilder::build(spec);
}

} // namespace hypernova::cli

