# Internal Evidence Matrix

| Claim          | Source           | Verification Method | Result              |
| -------------- | ---------------- | ------------------- | ------------------- |
| CLI exists     | pi/cli/main.cpp | .\bin\hypernova.exe --help | PASS |
| LP support     | lp_parser.cpp  | Executed examples/beginner/first_lp.lp | PASS |
| MILP support   | ranch_and_bound.cpp | Executed examples/beginner/first_milp.lp | PASS |
| inspect        | pi/cli/main.cpp | hypernova.exe inspect | PASS |
| diagnose       | alidation/certificate.hpp | hypernova.exe diagnose | PASS (Farkas proof generated) |
| REST solve     | console/server.py | HTTP request to /api/v1/solve | PASS |
| C++ API        | pi.hpp        | Compiled asic_solver.cpp against hypernova_api | PASS |
| GPU capability | gpu_backend.cpp| hypernova.exe capabilities reports cuda | PASS |
| Tests          | 	ests/         | ctest -N and ctest | PASS (21 suites) |
