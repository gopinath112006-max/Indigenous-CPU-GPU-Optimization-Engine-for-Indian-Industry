cmake_minimum_required(VERSION 3.20)

# Opt-in benchmark target (NOT part of the fast ctest gate; fit2p is a large LP).
#
#   cmake --build build --target bench_mittelmann
#
# Reference objectives (HiGHS-verified to full double precision) are in
# ./reference.csv.
set(HYPERNOVA_BENCH_MITTELMANN_TIME_LIMIT "120"
    CACHE STRING "Mittelmann benchmark per-instance time limit (seconds)")

add_custom_target(bench_mittelmann
    COMMAND $<TARGET_FILE:hypernova-bench>
        --dir "${CMAKE_CURRENT_SOURCE_DIR}"
        --suite mittelmann
        --expect "${CMAKE_CURRENT_SOURCE_DIR}/reference.csv"
        --time-limit ${HYPERNOVA_BENCH_MITTELMANN_TIME_LIMIT}
        --csv "${CMAKE_CURRENT_BINARY_DIR}/mittelmann_auto.csv"
        --out "${CMAKE_CURRENT_BINARY_DIR}/mittelmann_auto.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS hypernova-bench
    USES_TERMINAL
    COMMENT "Mittelmann LP benchmark (AUTO engine, ${HYPERNOVA_BENCH_MITTELMANN_TIME_LIMIT} s/instance)"
    VERBATIM)
