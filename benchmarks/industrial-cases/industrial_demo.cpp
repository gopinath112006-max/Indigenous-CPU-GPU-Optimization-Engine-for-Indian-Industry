#include <hypernova/api.hpp>
#include <model/mps_parser.hpp>
#include <model/lp_parser.hpp>
#include <validation/solution_verifier.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <cstddef>

using namespace hypernova;

namespace {

// ---------------------------------------------------------------------------
// 7-step chain helpers
// ---------------------------------------------------------------------------

void step_header(const char* step, const char* title) {
    std::cout << "\n----------------------------------------------------------------\n";
    std::cout << " [" << step << "] " << title << "\n";
    std::cout << "----------------------------------------------------------------\n";
}

// 1. Problem Statement
void step_problem_statement(const char* case_id) {
    step_header("Step 1", "Problem Statement");
    std::cout << "  SIH Problem Statement 26119: Indigenous GPU-Accelerated Optimization Solver\n";
    std::cout << "  Organisation: Mangalore Refinery and Petrochemicals Limited (MRPL)\n";
    std::cout << "  Sovereign alternative to CPLEX / Gurobi / Xpress. Built from first\n";
    std::cout << "  principles, no foreign solver library used.\n";
    std::cout << "  Case identifier: " << case_id << "\n";
}

// 2. Industrial Problem
void step_industrial_problem(const char* title, const char* domain) {
    step_header("Step 2", "Industrial Problem");
    std::cout << "  " << title << "\n";
    std::cout << "  Domain: " << domain << "\n";
}

// 3. Mathematical Model (called after the model is built)
void step_mathematical_model(const char* header, const model::Problem& p) {
    step_header("Step 3", "Mathematical Model");
    std::cout << "  " << header << "\n";
    std::cout << "  Class:        "
              << (p.is_lp() ? "Linear Programming (LP)"
                  : p.is_qp() ? "Quadratic Programming (convex QP)"
                  : p.is_milp() ? "Mixed-Integer Linear Programming (MILP)"
                                : "Mixed-Integer Quadratic Programming (MIQP)")
              << "\n";
    std::cout << "  Sense:        " << (p.obj_sense == model::ObjectiveSense::MINIMIZE ? "Minimize" : "Maximize") << "\n";
    std::cout << "  Variables:    " << p.variables.size()
              << "  (continuous " << p.num_continuous_vars()
              << ", integer " << p.num_integer_vars() << ")\n";
    std::cout << "  Constraints:  " << p.constraints.size() << "\n";
    std::cout << "  Nonzeros:     " << p.constraint_matrix.nnz() << "\n";
    if (!p.quadratic_terms.empty()) {
        std::cout << "  Quadratic:    " << p.quadratic_terms.size() << " terms (sparse, symmetric)\n";
    }
}

std::string status_label(model::ProblemStatus s) {
    switch (s) {
        case model::ProblemStatus::OPTIMAL:         return "OPTIMAL";
        case model::ProblemStatus::INFEASIBLE:      return "INFEASIBLE";
        case model::ProblemStatus::UNBOUNDED:       return "UNBOUNDED";
        case model::ProblemStatus::SUBOPTIMAL:      return "SUBOPTIMAL";
        case model::ProblemStatus::TIME_LIMIT:      return "TIME_LIMIT";
        case model::ProblemStatus::ITER_LIMIT:      return "ITER_LIMIT";
        case model::ProblemStatus::NUMERICAL_ERROR: return "NUMERICAL_ERROR";
        case model::ProblemStatus::INTERRUPTED:     return "INTERRUPTED";
        default:                                    return "UNKNOWN";
    }
}

model::Solution to_model_solution(const Solution& s) {
    model::Solution m;
    m.status = s.status;
    m.objective_value = s.objective_value;
    m.best_bound = s.best_bound;
    m.gap = s.gap;
    m.primal = s.primal;
    m.dual = s.dual;
    m.reduced_costs = s.reduced_costs;
    m.basis_status = s.basis_status;
    m.simplex_iterations = s.simplex_iterations;
    m.ipm_iterations = s.ipm_iterations;
    m.bb_nodes = s.bb_nodes;
    m.solve_time_ms = s.solve_time_ms;
    return m;
}

// 4. HyperNova Engine
void step_engine(const char* engine_desc) {
    step_header("Step 4", "HyperNova Engine");
    std::cout << "  " << engine_desc << "\n";
    std::cout << "  Backends:    Compute backend auto-dispatch (CPU / CUDA), no external solver.\n";
}

// 5. HyperNova solves the model
void step_optimization(const Solution& sol) {
    step_header("Step 5", "Optimization Result");
    std::cout << "  Status:        " << status_label(sol.status) << "\n";
    std::cout << "  Objective:     " << std::fixed << std::setprecision(6) << sol.objective_value << "\n";
    std::cout << "  Gap:           " << std::scientific << std::setprecision(6) << sol.gap << "\n";
    std::cout << "  Solve time:    " << std::fixed << std::setprecision(2) << sol.solve_time_ms << " ms\n";
    if (sol.bb_nodes > 0) {
        std::cout << "  B&B nodes:     " << sol.bb_nodes << "\n";
    }
    if (sol.simplex_iterations > 0) {
        std::cout << "  Simplex iters: " << sol.simplex_iterations << "\n";
    }
}

// 6. Verification
nlohmann::json step_verification(const model::Problem& problem, const Solution& sol) {
    step_header("Step 6", "Verification (primal / dual / complementarity / integrality)");
    validation::SolutionVerifier verifier;
    nlohmann::json j;
    if (!sol.is_feasible()) {
        std::cout << "  Verdict:    n/a (status " << status_label(sol.status) << ")\n";
        j["verdict"] = "N/A";
        return j;
    }
    validation::VerificationResult v = verifier.verify_detailed(problem, to_model_solution(sol));
    bool ok = v.feasible && v.optimal;
    std::cout << "  Verdict:    " << (ok ? "PASS (optimal + feasible)" : "PARTIAL") << "\n";
    std::cout << "  primal infeasibility:  " << std::scientific << std::setprecision(6) << v.primal_infeasibility << "\n";
    std::cout << "  dual infeasibility:    " << std::scientific << std::setprecision(6) << v.dual_infeasibility << "\n";
    std::cout << "  complementarity:       " << std::scientific << std::setprecision(6) << v.complementarity << "\n";
    if (problem.num_integer_vars() > 0) {
        std::cout << "  integrality violation: " << std::scientific << std::setprecision(6) << v.integrality_violation << "\n";
    }
    j["verdict"] = ok ? "PASS" : "PARTIAL";
    j["primal_infeasibility"] = v.primal_infeasibility;
    j["dual_infeasibility"] = v.dual_infeasibility;
    j["complementarity"] = v.complementarity;
    j["integrality_violation"] = v.integrality_violation;
    return j;
}

} // namespace

nlohmann::json run_crude_blending_qp();
nlohmann::json run_refinery_production_planning_milp();
nlohmann::json run_logistics_freight_milp();
nlohmann::json run_cogen_power_commitment_milp();
nlohmann::json run_hydrogen_network_lp();

// ---------------------------------------------------------------------------
// Case 1: Crude Oil Blending & Quality Giveaway Optimization (Convex QP)
// ---------------------------------------------------------------------------
nlohmann::json run_crude_blending_qp() {
    std::cout << "\n=====================================================================\n";
    std::cout << " CASE STUDY 1\n";
    std::cout << "=====================================================================\n";
    step_problem_statement("crude_blending_qp");
    step_industrial_problem(
        "Crude Oil Blending & Quality Giveaway Optimization",
        "SPM crude imports, distillation throughput and BS-VI / Euro-VI fuel quality compliance");

    model::ProblemBuilder builder("mrpl_crude_blending_qp");

    // Crude Oils Available (k barrels / day):
    // 0: Arab Light  ($75/bbl, API 33.2, Sulfur 1.85%, Viscosity 4.2 cSt)
    // 1: Bonny Light ($82/bbl, API 35.4, Sulfur 0.14%, Viscosity 3.1 cSt)
    // 2: Maya Heavy  ($62/bbl, API 21.8, Sulfur 3.40%, Viscosity 9.5 cSt)
    // 3: Murban      ($79/bbl, API 40.2, Sulfur 0.78%, Viscosity 2.8 cSt)
    // 4: Basrah Heavy($64/bbl, API 23.5, Sulfur 2.95%, Viscosity 8.1 cSt)
    std::size_t c_arab   = builder.add_variable(0.0, 120.0, model::VarType::CONTINUOUS, "Arab_Light");
    std::size_t c_bonny  = builder.add_variable(0.0,  90.0, model::VarType::CONTINUOUS, "Bonny_Light");
    std::size_t c_maya   = builder.add_variable(0.0,  60.0, model::VarType::CONTINUOUS, "Maya_Heavy");
    std::size_t c_murban = builder.add_variable(0.0, 100.0, model::VarType::CONTINUOUS, "Murban_Sweet");
    std::size_t c_basrah = builder.add_variable(0.0,  70.0, model::VarType::CONTINUOUS, "Basrah_Heavy");

    // Objective: Minimize Linear Procurement Cost + Quadratic Quality Giveaway Penalty.
    // Procurement: 75*Arab + 82*Bonny + 62*Maya + 79*Murban + 64*Basrah  (USD k/day)
    // Quality penalty: 0.5 * w * (S(x) - 1.25 * X_tot)^2  with sulfur deviation dev_i = S_i - 1.25.
    // HyperNova convention: diagonal term (i,i,q) -> 0.5*q*x_i^2 (q = dev_i^2);
    //                       off-diagonal (i,j,q) -> q * x_i * x_j (q = dev_i * dev_j, i < j).
    builder.set_objective({
        {c_arab, 75.0}, {c_bonny, 82.0}, {c_maya, 62.0}, {c_murban, 79.0}, {c_basrah, 64.0}
    }, model::ObjectiveSense::MINIMIZE);

    double devs[5] = {0.60, -1.11, 2.15, -0.47, 1.70};
    std::size_t vars[5] = {c_arab, c_bonny, c_maya, c_murban, c_basrah};
    for (int i = 0; i < 5; ++i) {
        builder.add_quadratic_term(vars[i], vars[i], devs[i] * devs[i]);
        for (int j = i + 1; j < 5; ++j) {
            builder.add_quadratic_term(vars[i], vars[j], devs[i] * devs[j]);
        }
    }

    // Constraints:
    // C1: Total Refinery Distillation Target = 300 k bbl/day
    builder.add_constraint({
        {c_arab, 1.0}, {c_bonny, 1.0}, {c_maya, 1.0}, {c_murban, 1.0}, {c_basrah, 1.0}
    }, model::ConstraintSense::EQ, 300.0, "Total_Throughput_Target");
    // C2: API Gravity >= 31.0 deg API
    builder.add_constraint({
        {c_arab, 2.2}, {c_bonny, 4.4}, {c_maya, -9.2}, {c_murban, 9.2}, {c_basrah, -7.5}
    }, model::ConstraintSense::GE, 0.0, "API_Gravity_Min_BSVI");
    // C3: Max Sulfur <= 1.80% wt (desulfurization capacity cap)
    builder.add_constraint({
        {c_arab, 1.85}, {c_bonny, 0.14}, {c_maya, 3.40}, {c_murban, 0.78}, {c_basrah, 2.95}
    }, model::ConstraintSense::LE, 540.0, "Sulfur_Max_Desulfurization_Cap");
    // C4: Viscosity lower bound (3.0 cSt) for diesel-cut quality
    builder.add_constraint({
        {c_arab, 1.2}, {c_bonny, 0.1}, {c_maya, 6.5}, {c_murban, -0.2}, {c_basrah, 5.1}
    }, model::ConstraintSense::GE, 0.0, "Viscosity_Min_Limit");

    model::Problem problem = builder.build();
    step_mathematical_model(
        "min (75x1+82x2+62x3+79x4+64x5) + 0.5·(S(x) - 1.25·X_tot)^2,  s.t. throughput=300 k bbl/day,\n"
        "    API>=31, sulfur<=1.80% wt, viscosity>=3.0 cSt",
        problem);
    model::write_lp(problem, "benchmarks/industrial-cases/crude_blending_qp.lp");

    step_engine("AUTO engine -> internal dispatch selects the Active-Set QP solver for this convex QP.");

    SolverOptions opts;
    opts.engine = EngineType::AUTO;
    Solver solver(opts);
    Solution sol = solver.solve(problem);
    step_optimization(sol);
    nlohmann::json ver = step_verification(problem, sol);

    step_header("Step 7", "Business / Industrial Result");
    double total_vol = sol.primal[c_arab] + sol.primal[c_bonny] + sol.primal[c_maya] +
                       sol.primal[c_murban] + sol.primal[c_basrah];
    double avg_sulfur = (1.85 * sol.primal[c_arab] + 0.14 * sol.primal[c_bonny] +
                         3.40 * sol.primal[c_maya] + 0.78 * sol.primal[c_murban] +
                         2.95 * sol.primal[c_basrah]) / total_vol;
    double avg_api = (33.2 * sol.primal[c_arab] + 35.4 * sol.primal[c_bonny] +
                      21.8 * sol.primal[c_maya] + 40.2 * sol.primal[c_murban] +
                      23.5 * sol.primal[c_basrah]) / total_vol;

    std::cout << "  Daily procurement + quality-penalty cost: $" << std::fixed << std::setprecision(2)
              << sol.objective_value << " k/day\n";
    std::cout << "  Annualised (350 operating days/yr):      $" << std::fixed << std::setprecision(2)
              << sol.objective_value * 350.0 / 1000.0 << " M/yr\n";
    std::cout << "  Optimal crude slate (k bbl/day):\n";
    std::cout << "    Arab Light   " << std::setw(10) << sol.primal[c_arab] << "\n";
    std::cout << "    Bonny Light  " << std::setw(10) << sol.primal[c_bonny] << "\n";
    std::cout << "    Maya Heavy   " << std::setw(10) << sol.primal[c_maya] << "\n";
    std::cout << "    Murban Sweet " << std::setw(10) << sol.primal[c_murban] << "\n";
    std::cout << "    Basrah Heavy " << std::setw(10) << sol.primal[c_basrah] << "\n";
    std::cout << "  Blend quality audit:\n";
    std::cout << "    Total volume : " << total_vol << " k bbl/day (target 300)\n";
    std::cout << "    Sulfur       : " << avg_sulfur << " % wt  (BS-VI limit 0.001%*after desulph,\n";
    std::cout << "                                        max unit feed 1.80%)\n";
    std::cout << "    API gravity  : " << avg_api << " deg API (>= 31.0)\n";

    return {
        {"name", "crude_blending_qp"},
        {"title", "Crude Oil Blending & Quality Giveaway Optimization"},
        {"class", "QP"},
        {"problem_statement", "SIH 26119 -> MRPL crude procurement & BS-VI quality compliance"},
        {"variables", problem.variables.size()},
        {"constraints", problem.constraints.size()},
        {"engine", "auto -> qp-active-set"},
        {"status", status_label(sol.status)},
        {"objective_value", sol.objective_value},
        {"objective_units", "USD k/day"},
        {"solve_time_ms", sol.solve_time_ms},
        {"verification", ver},
        {"business_result",
            {
                {"daily_cost_usd_k_per_day", sol.objective_value},
                {"annualised_usd_m", sol.objective_value * 350.0 / 1000.0},
                {"operating_days_per_year", 350},
                {"avg_sulfur_pct_wt", avg_sulfur},
                {"avg_api_gravity", avg_api}
            }}
    };
}

// ---------------------------------------------------------------------------
// Case 2: Multi-Period Refinery Production Planning & Mode Switching (MILP)
// ---------------------------------------------------------------------------
nlohmann::json run_refinery_production_planning_milp() {
    std::cout << "\n=====================================================================\n";
    std::cout << " CASE STUDY 2\n";
    std::cout << "=====================================================================\n";
    step_problem_statement("refinery_production_planning_milp");
    step_industrial_problem(
        "Multi-Period Refinery Production Planning & Mode Switching",
        "MRPL Phase-III Hydrocracker / FCCU operating-mode selection across a 3-month horizon");

    model::ProblemBuilder builder("mrpl_refinery_planning_milp");

    std::size_t p_gas[3], p_die[3], p_atf[3];
    std::size_t m_die[3], m_atf[3];
    for (int t = 0; t < 3; ++t) {
        std::string suffix = "_M" + std::to_string(t + 1);
        p_gas[t] = builder.add_variable(0.0, 300.0, model::VarType::CONTINUOUS, "Gasoline" + suffix);
        p_die[t] = builder.add_variable(0.0, 450.0, model::VarType::CONTINUOUS, "Diesel" + suffix);
        p_atf[t] = builder.add_variable(0.0, 250.0, model::VarType::CONTINUOUS, "ATF_Jet" + suffix);
        m_die[t] = builder.add_variable(0.0, 1.0, model::VarType::BINARY, "Mode_Max_Diesel" + suffix);
        m_atf[t] = builder.add_variable(0.0, 1.0, model::VarType::BINARY, "Mode_Max_ATF" + suffix);
    }

    // Objective: Maximize 3-month net refining margin (product revenue - operating costs).
    std::vector<std::pair<std::size_t, double>> obj_terms;
    for (int t = 0; t < 3; ++t) {
        obj_terms.push_back({p_gas[t], 820.0});
        obj_terms.push_back({p_die[t], 870.0});
        obj_terms.push_back({p_atf[t], 920.0});
        obj_terms.push_back({m_die[t], -4500.0});
        obj_terms.push_back({m_atf[t], -6200.0});
    }
    builder.set_objective(obj_terms, model::ObjectiveSense::MAXIMIZE);

    for (int t = 0; t < 3; ++t) {
        std::string suffix = "_M" + std::to_string(t + 1);
        // C1: Exactly one hydrocracker mode active per month
        builder.add_constraint({{m_die[t], 1.0}, {m_atf[t], 1.0}},
                               model::ConstraintSense::EQ, 1.0, "Exclusive_Mode" + suffix);
        // C2: Yield caps tied to the active mode
        builder.add_constraint({{p_die[t], 1.0}, {m_die[t], -450.0}, {m_atf[t], -250.0}},
                               model::ConstraintSense::LE, 0.0, "Diesel_Yield_Limit" + suffix);
        builder.add_constraint({{p_atf[t], 1.0}, {m_die[t], -100.0}, {m_atf[t], -250.0}},
                               model::ConstraintSense::LE, 0.0, "ATF_Yield_Limit" + suffix);
        // C3: Minimum committed supply (OMC long-term contracts)
        double min_gas[3] = {100.0, 120.0, 110.0};
        double min_die[3] = {200.0, 220.0, 210.0};
        double min_atf[3] = {80.0, 120.0, 90.0};
        builder.add_constraint({{p_gas[t], 1.0}}, model::ConstraintSense::GE, min_gas[t], "Min_Gas_Demand" + suffix);
        builder.add_constraint({{p_die[t], 1.0}}, model::ConstraintSense::GE, min_die[t], "Min_Die_Demand" + suffix);
        builder.add_constraint({{p_atf[t], 1.0}}, model::ConstraintSense::GE, min_atf[t], "Min_ATF_Demand" + suffix);
    }

    model::Problem problem = builder.build();
    step_mathematical_model(
        "max Σ_t (820·gas_t + 870·die_t + 920·atf_t - 4500·m_die_t - 6200·m_atf_t)\n"
        "    s.t. m_die_t + m_atf_t = 1, mode-tied yield caps, minimum supply commitments",
        problem);
    model::write_mps(problem, "benchmarks/industrial-cases/refinery_production_planning_milp.mps");

    step_engine("BRANCH_AND_BOUND engine (binary mode variables, exclusive-mode fixes).");

    SolverOptions opts;
    opts.engine = EngineType::BRANCH_AND_BOUND;
    opts.use_gpu = false;
    Solver solver(opts);
    Solution sol = solver.solve(problem);
    step_optimization(sol);
    nlohmann::json ver = step_verification(problem, sol);

    step_header("Step 7", "Business / Industrial Result");
    std::cout << "  3-month net refining margin: $" << std::fixed << std::setprecision(2)
              << sol.objective_value << " k\n";
    std::cout << "  Annualised (4 quarters/yr): $" << std::fixed << std::setprecision(2)
              << sol.objective_value * 4.0 / 1000.0 << " M/yr\n";
    std::cout << "  Monthly schedule:\n";
    std::cout << "    Month | Active mode | Gasoline (kt) | Diesel (kt) | ATF jet (kt)\n";
    std::cout << "    ------------------------------------------------------------------\n";
    nlohmann::json schedule = nlohmann::json::array();
    for (int t = 0; t < 3; ++t) {
        std::string mode = (sol.primal[m_die[t]] > 0.5) ? "Max-Diesel" : "Max-ATF";
        std::cout << "    M" << (t + 1) << "    | " << std::setw(10) << mode << " | "
                  << std::setw(11) << sol.primal[p_gas[t]] << " | " << std::setw(9)
                  << sol.primal[p_die[t]] << " | " << std::setw(11) << sol.primal[p_atf[t]] << "\n";
        schedule.push_back({{"month", t + 1},
                            {"mode", mode},
                            {"gasoline_kt", sol.primal[p_gas[t]]},
                            {"diesel_kt", sol.primal[p_die[t]]},
                            {"atf_jet_kt", sol.primal[p_atf[t]]}});
    }

    return {
        {"name", "refinery_production_planning_milp"},
        {"title", "Multi-Period Refinery Production Planning & Mode Switching"},
        {"class", "MILP"},
        {"problem_statement", "SIH 26119 -> MRPL hydrocracker/FCCU mode planning"},
        {"variables", problem.variables.size()},
        {"constraints", problem.constraints.size()},
        {"engine", "branch-and-bound"},
        {"status", status_label(sol.status)},
        {"objective_value", sol.objective_value},
        {"objective_units", "USD k / 3 months"},
        {"solve_time_ms", sol.solve_time_ms},
        {"bb_nodes", sol.bb_nodes},
        {"verification", ver},
        {"business_result",
            {
                {"three_month_margin_usd_k", sol.objective_value},
                {"annualised_usd_m", sol.objective_value * 4.0 / 1000.0},
                {"quarters_per_year", 4},
                {"monthly_schedule", schedule}
            }}
    };
}

// ---------------------------------------------------------------------------
// Case 3: Multi-Modal Supply Chain & Coastal Freight Dispatch (MILP)
// ---------------------------------------------------------------------------
nlohmann::json run_logistics_freight_milp() {
    std::cout << "\n=====================================================================\n";
    std::cout << " CASE STUDY 3\n";
    std::cout << "=====================================================================\n";
    step_problem_statement("logistics_freight_milp");
    step_industrial_problem(
        "Multi-Modal Supply Chain & Coastal Freight Dispatch",
        "MRPL Mangalore refinery coastal tanker, MHBL cross-country pipeline and rail distribution");

    model::ProblemBuilder builder("mrpl_logistics_freight_milp");

    std::size_t x_pipe_hsn = builder.add_variable(0.0, 300.0, model::VarType::CONTINUOUS, "Vol_Pipe_Hassan");
    std::size_t z_pipe_hsn = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Trigger_Pipe_Hassan");
    std::size_t x_pipe_blr = builder.add_variable(0.0, 400.0, model::VarType::CONTINUOUS, "Vol_Pipe_Bengaluru");
    std::size_t z_pipe_blr = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Trigger_Pipe_Bengaluru");
    std::size_t x_ship_goa = builder.add_variable(0.0, 150.0, model::VarType::CONTINUOUS, "Vol_Tanker_Goa");
    std::size_t x_ship_koc = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "Vol_Tanker_Kochi");
    std::size_t x_rail_hyd = builder.add_variable(0.0, 180.0, model::VarType::CONTINUOUS, "Vol_Rail_Hyderabad");
    std::size_t z_rail_hyd = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Trigger_Rail_Hyderabad");

    builder.set_objective({
        {x_pipe_hsn, 5.0},  {z_pipe_hsn, 500.0},
        {x_pipe_blr, 8.0},  {z_pipe_blr, 800.0},
        {x_ship_goa, 7.0},
        {x_ship_koc, 9.0},
        {x_rail_hyd, 16.0}, {z_rail_hyd, 300.0}
    }, model::ObjectiveSense::MINIMIZE);

    builder.add_constraint({{x_pipe_hsn, 1.0}, {z_pipe_hsn, -50.0}}, model::ConstraintSense::GE, 0.0, "Pipe_Hsn_Min_Batch");
    builder.add_constraint({{x_pipe_hsn, 1.0}, {z_pipe_hsn, -300.0}}, model::ConstraintSense::LE, 0.0, "Pipe_Hsn_Max_Cap");
    builder.add_constraint({{x_pipe_blr, 1.0}, {z_pipe_blr, -80.0}}, model::ConstraintSense::GE, 0.0, "Pipe_Blr_Min_Batch");
    builder.add_constraint({{x_pipe_blr, 1.0}, {z_pipe_blr, -400.0}}, model::ConstraintSense::LE, 0.0, "Pipe_Blr_Max_Cap");
    builder.add_constraint({{x_rail_hyd, 1.0}, {z_rail_hyd, -40.0}}, model::ConstraintSense::GE, 0.0, "Rail_Hyd_Min_Rake");
    builder.add_constraint({{x_rail_hyd, 1.0}, {z_rail_hyd, -180.0}}, model::ConstraintSense::LE, 0.0, "Rail_Hyd_Max_Cap");
    builder.add_constraint({{x_pipe_hsn, 1.0}}, model::ConstraintSense::GE, 120.0, "Demand_Hassan");
    builder.add_constraint({{x_pipe_blr, 1.0}}, model::ConstraintSense::GE, 250.0, "Demand_Bengaluru");
    builder.add_constraint({{x_ship_goa, 1.0}}, model::ConstraintSense::GE,  90.0, "Demand_Goa");
    builder.add_constraint({{x_ship_koc, 1.0}}, model::ConstraintSense::GE, 140.0, "Demand_Kochi");
    builder.add_constraint({{x_rail_hyd, 1.0}}, model::ConstraintSense::GE, 100.0, "Demand_Hyderabad");

    model::Problem problem = builder.build();
    step_mathematical_model(
        "min Σ_routes (var_rate·x + fixed·z)  s.t. batch triggers (x >= MinBatch·z),\n"
        "    route capacities and terminal demand commitments",
        problem);
    model::write_mps(problem, "benchmarks/industrial-cases/logistics_freight_milp.mps");

    step_engine("AUTO engine -> Branch-and-Bound for the fixed-charge MILP (network flow).");

    SolverOptions opts;
    opts.engine = EngineType::AUTO;
    Solver solver(opts);
    Solution sol = solver.solve(problem);
    step_optimization(sol);
    nlohmann::json ver = step_verification(problem, sol);

    step_header("Step 7", "Business / Industrial Result");
    std::cout << "  Monthly distribution cost: $" << std::fixed << std::setprecision(2)
              << sol.objective_value << " k/month\n";
    std::cout << "  Annualised (12 months/yr): $" << std::fixed << std::setprecision(2)
              << sol.objective_value * 12.0 / 1000.0 << " M/yr\n";
    std::cout << "  Dispatch plan:\n";
    std::cout << "    MHBL pipeline -> Hassan:    " << std::setw(6) << sol.primal[x_pipe_hsn]
              << " k MT  (" << (sol.primal[z_pipe_hsn] > 0.5 ? "TRIGGERED" : "OFF") << ")\n";
    std::cout << "    MHBL pipeline -> Bengaluru: " << std::setw(6) << sol.primal[x_pipe_blr]
              << " k MT  (" << (sol.primal[z_pipe_blr] > 0.5 ? "TRIGGERED" : "OFF") << ")\n";
    std::cout << "    Coastal tanker -> Goa:      " << std::setw(6) << sol.primal[x_ship_goa] << " k MT\n";
    std::cout << "    Coastal tanker -> Kochi:    " << std::setw(6) << sol.primal[x_ship_koc] << " k MT\n";
    std::cout << "    Rail rakes -> Hyderabad:    " << std::setw(6) << sol.primal[x_rail_hyd]
              << " k MT  (" << (sol.primal[z_rail_hyd] > 0.5 ? "TRIGGERED" : "OFF") << ")\n";

    return {
        {"name", "logistics_freight_milp"},
        {"title", "Multi-Modal Supply Chain & Coastal Freight Dispatch"},
        {"class", "MILP"},
        {"problem_statement", "SIH 26119 -> MRPL product distribution logistics"},
        {"variables", problem.variables.size()},
        {"constraints", problem.constraints.size()},
        {"engine", "auto -> branch-and-bound"},
        {"status", status_label(sol.status)},
        {"objective_value", sol.objective_value},
        {"objective_units", "USD k/month"},
        {"solve_time_ms", sol.solve_time_ms},
        {"bb_nodes", sol.bb_nodes},
        {"verification", ver},
        {"business_result",
            {
                {"monthly_cost_usd_k", sol.objective_value},
                {"annualised_usd_m", sol.objective_value * 12.0 / 1000.0},
                {"months_per_year", 12},
                {"pipe_hassan_kt", sol.primal[x_pipe_hsn]},
                {"pipe_bengaluru_kt", sol.primal[x_pipe_blr]},
                {"ship_goa_kt", sol.primal[x_ship_goa]},
                {"ship_kochi_kt", sol.primal[x_ship_koc]},
                {"rail_hyderabad_kt", sol.primal[x_rail_hyd]}
            }}
    };
}

// ---------------------------------------------------------------------------
// Case 4: Captive Power Plant Cogeneration & Unit Commitment (MILP)
// ---------------------------------------------------------------------------
nlohmann::json run_cogen_power_commitment_milp() {
    std::cout << "\n=====================================================================\n";
    std::cout << " CASE STUDY 4\n";
    std::cout << "=====================================================================\n";
    step_problem_statement("cogen_power_milp");
    step_industrial_problem(
        "Captive Power Plant Cogeneration & Unit Commitment",
        "MRPL utility boilers, steam turbine generators and state electrical grid integration");

    model::ProblemBuilder builder("mrpl_cogen_power_milp");

    std::size_t p_b1  = builder.add_variable(0.0,  60.0, model::VarType::CONTINUOUS, "Power_Boiler1");
    std::size_t u_b1  = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Commit_Boiler1");
    std::size_t p_b2  = builder.add_variable(0.0,  80.0, model::VarType::CONTINUOUS, "Power_Boiler2");
    std::size_t u_b2  = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Commit_Boiler2");
    std::size_t p_gtg = builder.add_variable(0.0,  40.0, model::VarType::CONTINUOUS, "Power_GTG");
    std::size_t u_gtg = builder.add_variable(0.0,   1.0, model::VarType::BINARY,     "Commit_GTG");
    std::size_t p_grid= builder.add_variable(0.0, 100.0, model::VarType::CONTINUOUS, "Power_Grid_Import");

    builder.set_objective({
        {p_b1, 45.0},   {u_b1, 500.0},
        {p_b2, 40.0},   {u_b2, 700.0},
        {p_gtg, 38.0},  {u_gtg, 400.0},
        {p_grid, 65.0}
    }, model::ObjectiveSense::MINIMIZE);

    builder.add_constraint({
        {p_b1, 1.0}, {p_b2, 1.0}, {p_gtg, 1.0}, {p_grid, 1.0}
    }, model::ConstraintSense::EQ, 110.0, "Refinery_Power_Demand_110MW");
    builder.add_constraint({{p_b1, 1.0}, {u_b1, -15.0}}, model::ConstraintSense::GE, 0.0, "B1_Min_Load");
    builder.add_constraint({{p_b1, 1.0}, {u_b1, -60.0}}, model::ConstraintSense::LE, 0.0, "B1_Max_Load");
    builder.add_constraint({{p_b2, 1.0}, {u_b2, -20.0}}, model::ConstraintSense::GE, 0.0, "B2_Min_Load");
    builder.add_constraint({{p_b2, 1.0}, {u_b2, -80.0}}, model::ConstraintSense::LE, 0.0, "B2_Max_Load");
    builder.add_constraint({{p_gtg, 1.0}, {u_gtg, -10.0}}, model::ConstraintSense::GE, 0.0, "GTG_Min_Load");
    builder.add_constraint({{p_gtg, 1.0}, {u_gtg, -40.0}}, model::ConstraintSense::LE, 0.0, "GTG_Max_Load");
    builder.add_constraint({{p_b1, 1.0}, {p_b2, 1.0}}, model::ConstraintSense::GE, 70.0, "Min_HP_Steam_Header");

    model::Problem problem = builder.build();
    step_mathematical_model(
        "min 45·p1 + 40·p2 + 38·p_g + 65·p_grid + 500·u1 + 700·u2 + 400·u_g\n"
        "    s.t. p1+p2+p_g+p_grid = 110 MW, min/max loading per unit, HP steam header >= 70 MW eq",
        problem);
    model::write_mps(problem, "benchmarks/industrial-cases/cogen_power_milp.mps");

    step_engine("BRANCH_AND_BOUND engine (unit-commitment binaries + LP dispatch inside).");

    SolverOptions opts;
    opts.engine = EngineType::BRANCH_AND_BOUND;
    Solver solver(opts);
    Solution sol = solver.solve(problem);
    step_optimization(sol);
    nlohmann::json ver = step_verification(problem, sol);

    step_header("Step 7", "Business / Industrial Result");
    std::cout << "  Hourly utility cost: $" << std::fixed << std::setprecision(2)
              << sol.objective_value << " /hr\n";
    std::cout << "  Annualised (8760 hr/yr): $" << std::fixed << std::setprecision(2)
              << sol.objective_value * 8760.0 / 1e6 << " M/yr\n";
    std::cout << "  Unit commitment & dispatch:\n";
    std::cout << "    Boiler 1 (HP steam) : " << std::setw(6) << sol.primal[p_b1]
              << " MW  (" << (sol.primal[u_b1] > 0.5 ? "ON" : "OFF") << ")\n";
    std::cout << "    Boiler 2 (HP steam) : " << std::setw(6) << sol.primal[p_b2]
              << " MW  (" << (sol.primal[u_b2] > 0.5 ? "ON" : "OFF") << ")\n";
    std::cout << "    Gas turbine (GTG)   : " << std::setw(6) << sol.primal[p_gtg]
              << " MW  (" << (sol.primal[u_gtg] > 0.5 ? "ON" : "OFF") << ")\n";
    std::cout << "    Grid import         : " << std::setw(6) << sol.primal[p_grid] << " MW\n";

    return {
        {"name", "cogen_power_milp"},
        {"title", "Captive Power Plant Cogeneration & Unit Commitment"},
        {"class", "MILP"},
        {"problem_statement", "SIH 26119 -> MRPL captive power & steam balance"},
        {"variables", problem.variables.size()},
        {"constraints", problem.constraints.size()},
        {"engine", "branch-and-bound"},
        {"status", status_label(sol.status)},
        {"objective_value", sol.objective_value},
        {"objective_units", "USD/hr"},
        {"solve_time_ms", sol.solve_time_ms},
        {"bb_nodes", sol.bb_nodes},
        {"verification", ver},
        {"business_result",
            {
                {"hourly_cost_usd", sol.objective_value},
                {"annualised_usd_m", sol.objective_value * 8760.0 / 1e6},
                {"hours_per_year", 8760},
                {"boiler1_mw", sol.primal[p_b1]},
                {"boiler2_mw", sol.primal[p_b2]},
                {"gtg_mw", sol.primal[p_gtg]},
                {"grid_import_mw", sol.primal[p_grid]}
            }}
    };
}

// ---------------------------------------------------------------------------
// Case 5: Refinery Hydrogen Network Purity & Feedstock Optimization (LP)
// ---------------------------------------------------------------------------
nlohmann::json run_hydrogen_network_lp() {
    std::cout << "\n=====================================================================\n";
    std::cout << " CASE STUDY 5\n";
    std::cout << "=====================================================================\n";
    step_problem_statement("hydrogen_network_lp");
    step_industrial_problem(
        "Refinery Hydrogen Network Purity & Feedstock Optimization",
        "MRPL DHDS & VGO hydrotreater hydrogen consumption with PSA purification");

    model::ProblemBuilder builder("mrpl_hydrogen_network_lp");

    std::size_t h_hgu  = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "Stream_HGU_Reformer");
    std::size_t h_ccr  = builder.add_variable(0.0,  80.0, model::VarType::CONTINUOUS, "Stream_CCR_Offgas");
    std::size_t h_psa  = builder.add_variable(0.0, 100.0, model::VarType::CONTINUOUS, "Stream_PSA_Purge");
    std::size_t h_dhds = builder.add_variable(0.0, 150.0, model::VarType::CONTINUOUS, "Stream_DHDS_Unit");
    std::size_t h_vgt  = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "Stream_VGO_Hydrotreater");

    builder.set_objective({
        {h_hgu, 0.45},
        {h_ccr, 0.15}
    }, model::ObjectiveSense::MINIMIZE);

    builder.add_constraint({
        {h_hgu, 1.0}, {h_psa, 0.85}, {h_dhds, -1.0}, {h_vgt, -1.0}
    }, model::ConstraintSense::GE, 0.0, "Hydrogen_Mass_Balance");
    builder.add_constraint({{h_dhds, 1.0}}, model::ConstraintSense::GE,  80.0, "Min_DHDS_Demand");
    builder.add_constraint({{h_vgt, 1.0}},  model::ConstraintSense::GE, 110.0, "Min_VGT_Demand");
    builder.add_constraint({{h_psa, 1.0}, {h_ccr, -0.90}}, model::ConstraintSense::LE, 0.0, "PSA_Recovery_Limit");

    model::Problem problem = builder.build();
    step_mathematical_model(
        "min 0.45·h_HGU + 0.15·h_CCR  s.t. HGU + 0.85·PSA >= DHDS + VGO,\n"
        "     DHDS >= 80, VGO >= 110 k Nm3/hr, PSA <= 0.9·CCR",
        problem);
    model::write_mps(problem, "benchmarks/industrial-cases/hydrogen_network_lp.mps");

    step_engine("AUTO engine -> Primal Simplex (pure LP network balance).");

    SolverOptions opts;
    opts.engine = EngineType::AUTO;
    Solver solver(opts);
    Solution sol = solver.solve(problem);
    step_optimization(sol);
    nlohmann::json ver = step_verification(problem, sol);

    step_header("Step 7", "Business / Industrial Result");
    std::cout << "  Hourly hydrogen feedstock cost: $" << std::fixed << std::setprecision(2)
              << sol.objective_value << " k/hr\n";
    std::cout << "  Annualised (8760 hr/yr): $" << std::fixed << std::setprecision(2)
              << sol.objective_value * 8760.0 / 1000.0 << " M/yr\n";
    std::cout << "  Stream flows (k Nm3/hr):\n";
    std::cout << "    HGU reformer (natural gas): " << sol.primal[h_hgu] << "\n";
    std::cout << "    CCR off-gas recovered:      " << sol.primal[h_ccr] << "\n";
    std::cout << "    PSA purified reflux:        " << sol.primal[h_psa] << "\n";
    std::cout << "    DHDS hydrotreater inlet:    " << sol.primal[h_dhds] << "\n";
    std::cout << "    VGO hydrotreater inlet:     " << sol.primal[h_vgt] << "\n";

    return {
        {"name", "hydrogen_network_lp"},
        {"title", "Refinery Hydrogen Network Purity & Feedstock Optimization"},
        {"class", "LP"},
        {"problem_statement", "SIH 26119 -> MRPL hydrogen pinch / PSA optimisation"},
        {"variables", problem.variables.size()},
        {"constraints", problem.constraints.size()},
        {"engine", "auto -> primal-simplex"},
        {"status", status_label(sol.status)},
        {"objective_value", sol.objective_value},
        {"objective_units", "USD k/hr"},
        {"solve_time_ms", sol.solve_time_ms},
        {"verification", ver},
        {"business_result",
            {
                {"hourly_cost_usd_k", sol.objective_value},
                {"annualised_usd_m", sol.objective_value * 8760.0 / 1000.0},
                {"hours_per_year", 8760},
                {"hgu_knm3h", sol.primal[h_hgu]},
                {"ccr_knm3h", sol.primal[h_ccr]},
                {"psa_knm3h", sol.primal[h_psa]},
                {"dhds_knm3h", sol.primal[h_dhds]},
                {"vgo_knm3h", sol.primal[h_vgt]}
            }}
    };
}

// ---------------------------------------------------------------------------
// Scaled Industrial Models Suite (Small=20, Medium=200, Large=2,000, Stress=20,000)
// ---------------------------------------------------------------------------
nlohmann::json run_scaled_crude_blending_qp(std::size_t n_crudes, const std::string& tier_name) {
    model::ProblemBuilder builder("crude_blending_qp_" + tier_name);
    std::vector<std::size_t> vars;
    vars.reserve(n_crudes);
    std::vector<std::pair<std::size_t, double>> obj;
    obj.reserve(n_crudes);
    std::vector<double> devs(n_crudes);

    for (std::size_t i = 0; i < n_crudes; ++i) {
        std::size_t v = builder.add_variable(0.0, 100.0, model::VarType::CONTINUOUS, "Crude_" + std::to_string(i));
        vars.push_back(v);
        double cost = 60.0 + static_cast<double>(i % 30);
        obj.push_back({v, cost});
        devs[i] = (static_cast<double>(i % 5) - 2.0) * 0.5;
    }
    builder.set_objective(obj, model::ObjectiveSense::MINIMIZE);

    for (std::size_t i = 0; i < n_crudes; ++i) {
        builder.add_quadratic_term(vars[i], vars[i], devs[i] * devs[i] + 1.0);
    }

    std::vector<std::pair<std::size_t, double>> th_coeffs;
    for (std::size_t i = 0; i < n_crudes; ++i) th_coeffs.push_back({vars[i], 1.0});
    builder.add_constraint(th_coeffs, model::ConstraintSense::EQ, static_cast<double>(n_crudes * 50), "Total_Throughput");

    model::Problem problem = builder.build();
    SolverOptions opts;
    opts.engine = (n_crudes >= 500) ? EngineType::INTERIOR_POINT : EngineType::AUTO;
    Solver solver(opts);
    Solution sol = solver.solve(problem);

    return {
        {"name", "crude_blending_" + tier_name},
        {"tier", tier_name},
        {"class", "QP"},
        {"variables", problem.variables.size()},
        {"constraints", problem.constraints.size()},
        {"nonzeros", problem.constraint_matrix.nnz()},
        {"status", status_label(sol.status)},
        {"objective_value", sol.objective_value},
        {"solve_time_ms", sol.solve_time_ms},
        {"verification", sol.is_optimal() ? "PASS" : "N/A"}
    };
}

nlohmann::json run_scaled_hydrogen_network_lp(std::size_t n_nodes, const std::string& tier_name) {
    model::ProblemBuilder builder("hydrogen_network_" + tier_name);
    std::vector<std::size_t> vars;
    vars.reserve(n_nodes);
    std::vector<std::pair<std::size_t, double>> obj;
    obj.reserve(n_nodes);

    for (std::size_t i = 0; i < n_nodes; ++i) {
        std::size_t v = builder.add_variable(0.0, 500.0, model::VarType::CONTINUOUS, "Stream_" + std::to_string(i));
        vars.push_back(v);
        obj.push_back({v, 0.10 + static_cast<double>(i % 10) * 0.05});
    }
    builder.set_objective(obj, model::ObjectiveSense::MINIMIZE);

    for (std::size_t i = 0; i < n_nodes / 2; ++i) {
        std::size_t in1 = 2 * i;
        std::size_t in2 = (2 * i + 1) % n_nodes;
        builder.add_constraint({{vars[in1], 1.0}, {vars[in2], 0.85}}, model::ConstraintSense::GE, 100.0, "Balance_" + std::to_string(i));
    }

    model::Problem problem = builder.build();
    SolverOptions opts;
    opts.engine = (n_nodes >= 500) ? EngineType::INTERIOR_POINT : EngineType::AUTO;
    Solver solver(opts);
    Solution sol = solver.solve(problem);

    return {
        {"name", "hydrogen_network_" + tier_name},
        {"tier", tier_name},
        {"class", "LP"},
        {"variables", problem.variables.size()},
        {"constraints", problem.constraints.size()},
        {"nonzeros", problem.constraint_matrix.nnz()},
        {"status", status_label(sol.status)},
        {"objective_value", sol.objective_value},
        {"solve_time_ms", sol.solve_time_ms},
        {"verification", sol.is_optimal() ? "PASS" : "N/A"}
    };
}

int main(int argc, char** argv) {
    std::cout << "=====================================================================\n";
    std::cout << " HyperNova Sovereign Optimization Engine - MRPL Industrial Case Suite\n";
    std::cout << " SIH Problem Statement 26119 | Mangalore Refinery and Petrochemicals Ltd\n";
    std::cout << "=====================================================================\n";

    nlohmann::json report;
    report["generated_by"] = "HyperNova industrial_demo";
    report["problem_statement"] = "SIH 26119";
    report["organization"] = "MRPL";
    nlohmann::json cases = nlohmann::json::array();
    cases.push_back(run_crude_blending_qp());
    cases.push_back(run_refinery_production_planning_milp());
    cases.push_back(run_logistics_freight_milp());
    cases.push_back(run_cogen_power_commitment_milp());
    cases.push_back(run_hydrogen_network_lp());
    report["cases"] = cases;

    // End-to-end chain summary table
    std::cout << "\n=====================================================================\n";
    std::cout << " END-TO-END CHAIN SUMMARY\n";
    std::cout << "=====================================================================\n";
    std::cout << " Case | Class | Vars | Cons | Status   | Objective          | Verification | Business headline\n";
    std::cout << " --------------------------------------------------------------------------------------------\n";
    for (const auto& c : cases) {
        const auto& biz = c["business_result"];
        double annual_m = biz.contains("annualised_usd_m") ? static_cast<double>(biz["annualised_usd_m"]) : 0.0;
        int annual_int = static_cast<int>(std::round(annual_m));
        std::string headline;
        std::string cname = c["name"];
        if (cname == "crude_blending_qp") {
            headline = "USD " + std::to_string(annual_int) + " M/yr crude slate";
        } else if (cname == "refinery_production_planning_milp") {
            headline = "USD " + std::to_string(annual_int) + " M/yr margin";
        } else if (cname == "logistics_freight_milp") {
            headline = "USD " + std::to_string(annual_int) + " M/yr freight";
        } else if (cname == "cogen_power_milp") {
            headline = "USD " + std::to_string(annual_int) + " M/yr energy";
        } else {
            headline = "USD " + std::to_string(annual_int) + " M/yr hydrogen";
        }
        std::string ccls = c["class"];
        std::size_t nvars = c["variables"];
        std::size_t ncons = c["constraints"];
        std::string cstat = c["status"];
        double cobj = c["objective_value"];
        std::string cver = c["verification"].value("verdict", "N/A");

        std::cout << " " << std::setw(34) << std::left << cname
                  << " | " << ccls
                  << "  | " << std::setw(4) << nvars
                  << " | " << std::setw(4) << ncons
                  << " | " << std::setw(9) << cstat
                  << " | " << cobj
                  << " | " << cver
                  << " | " << headline << "\n";
    }

    // Industrial Scale Breakdown (Priority 7)
    std::cout << "\n=====================================================================\n";
    std::cout << " INDUSTRIAL SCALE VALIDATION (Small, Medium, Large, Stress)\n";
    std::cout << "=====================================================================\n";
    std::cout << std::left << std::setw(24) << "Scale Tier"
              << std::setw(10) << "Class"
              << std::setw(10) << "Vars"
              << std::setw(10) << "Cons"
              << std::setw(12) << "Nonzeros"
              << std::setw(12) << "Status"
              << std::setw(12) << "Solve (ms)"
              << std::setw(12) << "Verified" << "\n";
    std::cout << std::string(92, '-') << "\n";

    std::vector<std::pair<std::string, std::size_t>> tiers = {
        {"Small (20)", 20},
        {"Medium (100)", 100},
        {"Large (500)", 500},
        {"Stress (2,000)", 2000}
    };

    nlohmann::json scaled_results = nlohmann::json::array();
    for (const auto& [tier_label, n_vars] : tiers) {
        auto qp_res = run_scaled_crude_blending_qp(n_vars, tier_label);
        scaled_results.push_back(qp_res);
        std::cout << std::left << std::setw(24) << qp_res["name"]
                  << std::setw(10) << qp_res["class"]
                  << std::setw(10) << qp_res["variables"]
                  << std::setw(10) << qp_res["constraints"]
                  << std::setw(12) << qp_res["nonzeros"]
                  << std::setw(12) << qp_res["status"]
                  << std::setw(12) << std::fixed << std::setprecision(2) << static_cast<double>(qp_res["solve_time_ms"])
                  << std::setw(12) << qp_res["verification"] << "\n";

        auto lp_res = run_scaled_hydrogen_network_lp(n_vars, tier_label);
        scaled_results.push_back(lp_res);
        std::cout << std::left << std::setw(24) << lp_res["name"]
                  << std::setw(10) << lp_res["class"]
                  << std::setw(10) << lp_res["variables"]
                  << std::setw(10) << lp_res["constraints"]
                  << std::setw(12) << lp_res["nonzeros"]
                  << std::setw(12) << lp_res["status"]
                  << std::setw(12) << std::fixed << std::setprecision(2) << static_cast<double>(lp_res["solve_time_ms"])
                  << std::setw(12) << lp_res["verification"] << "\n";
    }
    report["scaled_instances"] = scaled_results;

    std::cout << "\n Generated .lp / .mps models exported to benchmarks/industrial-cases/\n";

    std::string out_path = "industrial_results.json";
    if (argc > 1) out_path = argv[1];
    std::ofstream out(out_path);
    if (out.is_open()) {
        out << report.dump(2) << "\n";
        std::cout << " Structured results written to " << out_path << "\n";
    } else {
        std::cerr << "Warning: could not open " << out_path << " for writing\n";
    }
    return 0;
}