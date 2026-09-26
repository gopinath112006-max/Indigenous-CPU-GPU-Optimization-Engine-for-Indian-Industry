# The Complete Beginner's Guide to HyperNova

## 1. What is HyperNova?
HyperNova is a mathematical optimization solver designed to solve Linear Programming (LP) and Mixed-Integer Linear Programming (MILP) problems. 
It parses models and uses robust algorithms (Simplex, Branch-and-Bound, IPM) to mathematically prove the optimal solution.

## 2. Prerequisites
- **Git**
- **CMake** (3.20+)
- **C++ Compiler** (GCC/MinGW, MSVC, or Clang supporting C++20)

## 3. Building HyperNova
HyperNova has no external mathematical solver dependency. It uses nlohmann_json (for REST) and GoogleTest (for tests).

`bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
`
*(The executable is written to build/bin/hypernova.exe on Windows or build/bin/hypernova on Linux/macOS).*

## 4. Running Tests
Verify your build:
`bash
cd build
ctest -R "test_lp|test_milp|test_api" --output-on-failure
`
*(The targeted test suites should pass. Note: the full Netlib regression suite takes significantly longer and may hit execution timeouts).*

## 5. Checking Capabilities
Check your hardware support:
`bash
.\build\bin\hypernova.exe capabilities
`

## 6. Your First LP
Create first_lp.lp:
`text
Maximize
 obj: 3 x1 + 2 x2
Subject To
 c1: x1 + x2 <= 4
 c2: x1 <= 2
Bounds
 0 <= x1
 0 <= x2
End
`
Solve it:
`bash
.\build\bin\hypernova.exe solve first_lp.lp
`
*(The exact mathematical optimum is x1=2, x2=2 yielding an objective of 10.0).*

## 7. Inspecting a Model
`bash
.\build\bin\hypernova.exe inspect first_lp.lp
`
*(Outputs the number of variables, constraints, and problem type).*

## 8. Diagnosing a Model
If your model is infeasible:
`bash
.\build\bin\hypernova.exe diagnose infeasible.lp
`
*(Outputs a mathematical Farkas certificate proving impossibility).*

## 9. Understanding Solver Results
- **OPTIMAL**: The solver mathematically proved no better solution exists within tolerances.
- **SUBOPTIMAL**: A feasible solution may exist even when the solver does not report OPTIMAL. HyperNova uses SUBOPTIMAL for a valid solution where optimality has not been established (e.g. time limit reached).
- **INFEASIBLE**: The constraints contradict each other.

## 10. REST API
Launch the server:
`bash
python console/server.py --port 8080
`
Submit an LP using JSON (POST /api/v1/solve). The JSON requires:
`json
{
  "model_content": "Maximize\n obj: 2 x1 + 3 x2...",
  "format": "lp",
  "engine": "auto"
}
`

## 11. C++ API
Include <hypernova/api.hpp> and link the hypernova_api CMake target.
`cpp
#include <hypernova/api.hpp>
// ...
hypernova::Problem prob;
auto x1 = prob.add_variable(0.0, 10.0, hypernova::model::VarType::INTEGER, "x1");
// ...
hypernova::Solver solver;
hypernova::Solution result = solver.solve(prob);
`
