cmake_minimum_required(VERSION 3.20)

add_executable(industrial_demo industrial_demo.cpp)
target_link_libraries(industrial_demo PRIVATE hypernova_api nlohmann_json::nlohmann_json)

set(HYPERNOVA_BENCH_INDUSTRIAL_TIME_LIMIT "60"
    CACHE STRING "Industrial benchmark per-instance time limit (seconds)")

add_custom_target(bench_industrial
    COMMAND $<TARGET_FILE:hypernova-bench>
        --dir "${CMAKE_CURRENT_SOURCE_DIR}"
        --suite industrial
        --expect "${CMAKE_CURRENT_SOURCE_DIR}/reference.csv"
        --time-limit ${HYPERNOVA_BENCH_INDUSTRIAL_TIME_LIMIT}
        --csv "${CMAKE_CURRENT_BINARY_DIR}/industrial_auto.csv"
        --out "${CMAKE_CURRENT_BINARY_DIR}/industrial_auto.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS hypernova-bench industrial_demo
    USES_TERMINAL
    COMMENT "Industrial MRPL benchmark suite (AUTO engine, ${HYPERNOVA_BENCH_INDUSTRIAL_TIME_LIMIT} s/instance)"
    VERBATIM)