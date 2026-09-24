#include <gtest/gtest.h>
#include <model/problem.hpp>
#include <milp/branch_and_bound.hpp>

using namespace hypernova;

void verify_solution(const milp::BranchAndBoundResult& sol, model::ProblemStatus exp_status, double exp_obj = 0.0, const std::vector<double>& exp_x = {}) {
    EXPECT_EQ(sol.status, exp_status) << "Expected status " << static_cast<int>(exp_status) << " got " << static_cast<int>(sol.status);
    if (exp_status == model::ProblemStatus::OPTIMAL) {
        EXPECT_NEAR(sol.objective_value, exp_obj, 1e-5);
        // exp_x only contains the CONTINUOUS variables, which are added AFTER the dummy_int.
        ASSERT_EQ(sol.primal.size(), exp_x.size() + 1);
        for (size_t i = 0; i < exp_x.size(); ++i) {
            EXPECT_NEAR(sol.primal[i + 1], exp_x[i], 1e-5) << "Mismatch at variable " << (i + 1);
        }
    }
}

// Helper to create solvers forced into B&B
milp::BranchAndBoundSolver create_bnb_solver() {
    milp::BranchAndBoundOptions opts;
    return milp::BranchAndBoundSolver(numerical::ToleranceConfig{}, opts);
}

// Case 1: x >= 5
TEST(BoundShifting, ShiftPositiveLowerBound) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, 5.0, 1e20, 1.0});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 2, {{0, 1, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, 5.0, {5.0});
}

// Case 2: x >= -5
TEST(BoundShifting, ShiftNegativeLowerBound) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, -5.0, 1e20, 1.0});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 2, {{0, 1, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, -5.0, {-5.0});
}

// Case 3: -5 <= x <= 10
TEST(BoundShifting, ShiftBothBounds) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MAXIMIZE; 
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, -5.0, 10.0, 1.0});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 2, {{0, 1, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, 10.0, {10.0});
}

// Case 4: x == 5
TEST(BoundShifting, ShiftFixedVariable) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, 5.0, 5.0, 1.0});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 2, {{0, 1, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, 5.0, {5.0});
}

// Case 5: integer x >= 3
TEST(BoundShifting, ShiftIntegerVariable) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"x", model::VarType::INTEGER, 3.0, 1e20, 2.5});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 1, {{0, 0, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    
    EXPECT_EQ(sol.status, model::ProblemStatus::OPTIMAL);
    EXPECT_NEAR(sol.objective_value, 7.5, 1e-5);
    ASSERT_EQ(sol.primal.size(), 1u);
    EXPECT_NEAR(sol.primal[0], 3.0, 1e-5);
}

// Case 6: multiple variables with different lower bounds
TEST(BoundShifting, ShiftMultipleVariables) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, 2.0, 1e20, 1.0});
    prob.variables.push_back({"y", model::VarType::CONTINUOUS, -3.0, 1e20, 1.0});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 3, {{0, 1, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, -1.0, {2.0, -3.0});
}

// Case 7: mixed shifted + unshifted variables
TEST(BoundShifting, MixedShiftedUnshifted) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, 4.0, 1e20, 1.0});
    prob.variables.push_back({"y", model::VarType::CONTINUOUS, 0.0, 1e20, 1.0});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 3, {{0, 1, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, 4.0, {4.0, 0.0});
}

// Case 8: minimization
TEST(BoundShifting, MinimizationComplex) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, 2.0, 1e20, 1.0});
    prob.variables.push_back({"y", model::VarType::CONTINUOUS, 3.0, 1e20, 2.0});
    prob.constraints.push_back({"c1", model::ConstraintSense::GE, 10.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 3, {
        {0, 1, 1.0}, {0, 2, 1.0}
    });
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, 13.0, {7.0, 3.0}); 
}

// Case 9: maximization
TEST(BoundShifting, MaximizationComplex) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MAXIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, 2.0, 1e20, 1.0});
    prob.variables.push_back({"y", model::VarType::CONTINUOUS, 3.0, 1e20, 2.0});
    prob.constraints.push_back({"c1", model::ConstraintSense::LE, 10.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 3, {
        {0, 1, 1.0}, {0, 2, 1.0}
    });
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    verify_solution(sol, model::ProblemStatus::OPTIMAL, 18.0, {2.0, 8.0}); 
}

// Case 10: infeasible shifted bounds
TEST(BoundShifting, InfeasibleShiftedBounds) {
    model::Problem prob;
    prob.obj_sense = model::ObjectiveSense::MINIMIZE;
    prob.variables.push_back({"dummy_int", model::VarType::INTEGER, 0.0, 1.0, 0.0});
    prob.variables.push_back({"x", model::VarType::CONTINUOUS, 5.0, 3.0, 1.0});
    prob.constraints.push_back({"dummy", model::ConstraintSense::GE, -100.0});
    prob.constraint_matrix = numerical::SparseMatrix::from_triplets(1, 2, {{0, 1, 1.0}});
    auto solver = create_bnb_solver();
    auto sol = solver.solve(prob);
    EXPECT_EQ(sol.status, model::ProblemStatus::INFEASIBLE);
}
