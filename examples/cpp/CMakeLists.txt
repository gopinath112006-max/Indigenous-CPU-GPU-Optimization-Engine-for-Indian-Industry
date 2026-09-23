cmake_minimum_required(VERSION 3.20)
project(HyperNovaExamples)
add_executable(basic_solver basic_solver.cpp)
target_link_libraries(basic_solver PRIVATE hypernova_api)
