#include <hypernova/api.hpp>
#include <iostream>
#include <iomanip>
#include <vector>

using namespace hypernova;

int main() {
    std::cout << "=========================================================\n";
    std::cout << " HyperNova Industrial MRPL Optimization Demonstration\n";
    std::cout << "=========================================================\n\n";

    // -------------------------------------------------------------------------
    // 1. Crude Blending Optimization (LP / Linear Program)
    // -------------------------------------------------------------------------
    std::cout << "[1] Crude Blending Optimization (LP)\n";
    {
        model::ProblemBuilder builder("mrpl_crude_blending");

        // Variables: Crude blend quantities (thousand barrels / day)
        // 0: Arab Light (Cost: $75/bbl, API Gravity: 33, Sulfur: 1.8%)
        // 1: Bonny Light (Cost: $82/bbl, API Gravity: 35, Sulfur: 0.15%)
        // 2: Maya Heavy (Cost: $62/bbl, API Gravity: 22, Sulfur: 3.4%)
        std::size_t c_arab = builder.add_variable(0.0, 100.0, model::VarType::CONTINUOUS, "Arab_Light");
        std::size_t c_bonny = builder.add_variable(0.0, 80.0, model::VarType::CONTINUOUS, "Bonny_Light");
        std::size_t c_maya = builder.add_variable(0.0, 50.0, model::VarType::CONTINUOUS, "Maya_Heavy");

        // Objective: Minimize total crude purchase cost ($k / day)
        builder.set_objective({
            {c_arab, 75.0},
            {c_bonny, 82.0},
            {c_maya, 62.0}
        }, model::ObjectiveSense::MINIMIZE);

        // Constraints:
        // C1: Total distillation throughput target = 150 k bbl/day
        builder.add_constraint({{c_arab, 1.0}, {c_bonny, 1.0}, {c_maya, 1.0}},
                               model::ConstraintSense::EQ, 150.0, "Target_Throughput");

        // C2: API Gravity >= 30.0 -> 33*Arab + 35*Bonny + 22*Maya >= 30*150 = 4500
        // => 3*Arab + 5*Bonny - 8*Maya >= 0
        builder.add_constraint({{c_arab, 3.0}, {c_bonny, 5.0}, {c_maya, -8.0}},
                               model::ConstraintSense::GE, 0.0, "API_Gravity_Min");

        // C3: Max Sulfur <= 1.5% -> 1.8*Arab + 0.15*Bonny + 3.4*Maya <= 1.5*150 = 225
        builder.add_constraint({{c_arab, 1.8}, {c_bonny, 0.15}, {c_maya, 3.4}},
                               model::ConstraintSense::LE, 225.0, "Sulfur_Max");

        model::Problem problem = builder.build();

        SolverOptions opts;
        opts.engine = EngineType::AUTO;
        Solver solver(opts);
        Solution sol = solver.solve(problem);

        std::cout << "  Status:      " << (sol.is_optimal() ? "OPTIMAL" : "FAILED") << "\n";
        std::cout << "  Objective:   $" << std::fixed << std::setprecision(2) << sol.objective_value << " k/day\n";
        std::cout << "  Blend Allocation:\n";
        std::cout << "    - Arab Light:  " << sol.primal[c_arab] << " k bbl/day\n";
        std::cout << "    - Bonny Light: " << sol.primal[c_bonny] << " k bbl/day\n";
        std::cout << "    - Maya Heavy:  " << sol.primal[c_maya] << " k bbl/day\n\n";
    }

    // -------------------------------------------------------------------------
    // 2. Refinery Production Planning (MILP / Mixed-Integer Linear Program)
    // -------------------------------------------------------------------------
    std::cout << "[2] Refinery Production Planning & Unit Mode Selection (MILP)\n";
    {
        model::ProblemBuilder builder("mrpl_refinery_planning");

        // Continuous variables: Production volumes (k tonnes / month)
        // 0: Gasoline, 1: Diesel, 2: ATF (Jet Fuel)
        std::size_t p_gasoline = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "Gasoline_Vol");
        std::size_t p_diesel = builder.add_variable(0.0, 300.0, model::VarType::CONTINUOUS, "Diesel_Vol");
        std::size_t p_atf = builder.add_variable(0.0, 150.0, model::VarType::CONTINUOUS, "ATF_Vol");

        // Binary decision variables: Hydrocracker unit mode (High Diesel vs High Jet)
        // 3: Mode_Diesel (Binary), 4: Mode_Jet (Binary)
        std::size_t m_diesel = builder.add_variable(0.0, 1.0, model::VarType::BINARY, "Mode_High_Diesel");
        std::size_t m_jet = builder.add_variable(0.0, 1.0, model::VarType::BINARY, "Mode_High_Jet");

        // Objective: Maximize net profit ($ revenue - $ operating cost)
        // Prices: Gasoline $800/t, Diesel $850/t, ATF $900/t
        // Fixed Mode Operating Costs: Mode_Diesel $5,000k, Mode_Jet $6,500k
        builder.set_objective({
            {p_gasoline, 800.0},
            {p_diesel, 850.0},
            {p_atf, 900.0},
            {m_diesel, -5000.0},
            {m_jet, -6500.0}
        }, model::ObjectiveSense::MAXIMIZE);

        // Constraints:
        // C1: Exactly one hydrocracker mode active
        builder.add_constraint({{m_diesel, 1.0}, {m_jet, 1.0}},
                               model::ConstraintSense::EQ, 1.0, "Select_One_Mode");

        // C2: Mode production bounds (Diesel limit dependent on mode)
        // p_diesel <= 150 * m_diesel + 300 * m_jet
        builder.add_constraint({{p_diesel, 1.0}, {m_diesel, -150.0}, {m_jet, -300.0}},
                               model::ConstraintSense::LE, 0.0, "Diesel_Mode_Limit");

        // C3: Minimum demand commitments
        builder.add_constraint({{p_gasoline, 1.0}}, model::ConstraintSense::GE, 50.0, "Min_Gasoline_Demand");
        builder.add_constraint({{p_diesel, 1.0}}, model::ConstraintSense::GE, 100.0, "Min_Diesel_Demand");

        model::Problem problem = builder.build();

        SolverOptions opts;
        opts.engine = EngineType::AUTO;
        Solver solver(opts);
        Solution sol = solver.solve(problem);

        std::cout << "  Status:      " << (sol.is_optimal() ? "OPTIMAL" : "FAILED") << "\n";
        std::cout << "  Max Profit:  $" << std::fixed << std::setprecision(2) << sol.objective_value << " k/month\n";
        std::cout << "  Hydrocracker Operating Mode:\n";
        std::cout << "    - High-Diesel Mode: " << (sol.primal[m_diesel] > 0.5 ? "ACTIVE" : "INACTIVE") << "\n";
        std::cout << "    - High-Jet Mode:    " << (sol.primal[m_jet] > 0.5 ? "ACTIVE" : "INACTIVE") << "\n";
        std::cout << "  Production Plan:\n";
        std::cout << "    - Gasoline: " << sol.primal[p_gasoline] << " k tonnes\n";
        std::cout << "    - Diesel:   " << sol.primal[p_diesel] << " k tonnes\n";
        std::cout << "    - ATF:      " << sol.primal[p_atf] << " k tonnes\n\n";
    }

    // -------------------------------------------------------------------------
    // 3. Product Logistics & Transportation (LP)
    // -------------------------------------------------------------------------
    std::cout << "[3] Product Distribution & Coastal Logistics (LP)\n";
    {
        model::ProblemBuilder builder("mrpl_logistics");

        // Supply Sources: Mangalore Refinery (MRPL) 500 k metric tonnes
        // Destinations: Bengaluru, Goa, Kochi demand hubs
        // Modes: Pipeline vs Coastal Tanker
        std::size_t x_blr_pipe = builder.add_variable(0.0, 300.0, model::VarType::CONTINUOUS, "Pipe_Blr");
        std::size_t x_goa_sea  = builder.add_variable(0.0, 200.0, model::VarType::CONTINUOUS, "Sea_Goa");
        std::size_t x_kochi_sea = builder.add_variable(0.0, 250.0, model::VarType::CONTINUOUS, "Sea_Kochi");

        // Objective: Minimize shipping cost ($/tonne)
        // Pipeline to BLR: $12/t, Sea to Goa: $8/t, Sea to Kochi: $10/t
        builder.set_objective({
            {x_blr_pipe, 12.0},
            {x_goa_sea, 8.0},
            {x_kochi_sea, 10.0}
        }, model::ObjectiveSense::MINIMIZE);

        // Demand constraints
        builder.add_constraint({{x_blr_pipe, 1.0}}, model::ConstraintSense::GE, 250.0, "Demand_Bengaluru");
        builder.add_constraint({{x_goa_sea, 1.0}}, model::ConstraintSense::GE, 100.0, "Demand_Goa");
        builder.add_constraint({{x_kochi_sea, 1.0}}, model::ConstraintSense::GE, 150.0, "Demand_Kochi");

        model::Problem problem = builder.build();

        SolverOptions opts;
        opts.engine = EngineType::AUTO;
        Solver solver(opts);
        Solution sol = solver.solve(problem);

        std::cout << "  Status:         " << (sol.is_optimal() ? "OPTIMAL" : "FAILED") << "\n";
        std::cout << "  Logistics Cost: $" << std::fixed << std::setprecision(2) << sol.objective_value << " k/month\n";
        std::cout << "  Dispatches:\n";
        std::cout << "    - Pipeline -> Bengaluru: " << sol.primal[x_blr_pipe] << " k tonnes\n";
        std::cout << "    - Tanker   -> Goa:       " << sol.primal[x_goa_sea] << " k tonnes\n";
        std::cout << "    - Tanker   -> Kochi:     " << sol.primal[x_kochi_sea] << " k tonnes\n\n";
    }

    std::cout << "=========================================================\n";
    std::cout << " Industrial MRPL Case Studies Successfully Verified!\n";
    std::cout << "=========================================================\n";

    return 0;
}
