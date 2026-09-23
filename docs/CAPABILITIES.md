# HyperNova Capability Matrix

| Capability          | Status        | Verification       |
| ------------------- | ------------- | ------------------ |
| LP                  | Implemented   | Netlib tests/examples |
| MILP                | Implemented   | MIPLIB tests/examples |
| QP                  | Implemented   | Active-Set tests   |
| MIQP                | Implemented   | B&B tests          |
| LP parser           | Implemented   | CLI solve / parser tests |
| MPS parser          | Implemented   | CLI solve / parser tests |
| Branch & Bound      | Implemented   | 	est_parallel_bnb|
| Branch & Cut        | Implemented   | 	est_milp        |
| Presolve            | Implemented   | 	est_presolve    |
| Parallel B&B        | Implemented   | Thread pool tests  |
| CUDA infrastructure | Implemented   | cuda_driver.cpp  |
| PTX                 | Implemented   | gpu_backend.cpp  |
| REST API            | Implemented   | console/server.py|
| C API               | Implemented   | 	est_api.cpp     |
| C++ API             | Implemented   | hypernova::Solver|
