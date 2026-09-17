cmake_minimum_required(VERSION 3.20)

add_executable(scale_benchmark scale_benchmark.cpp)
target_link_libraries(scale_benchmark PRIVATE hypernova_api)
