cmake_minimum_required(VERSION 3.20)

# Opt-in benchmark target (NOT part of the fast ctest gate: the shipped MIPLIB
# subset contains hard instances that are expected TIME_LIMIT at a modest budget).
#
#   cmake --build build --target bench_miplib
#
# Reference objectives (published MIPLIB optima) live in ./reference.csv.
set(HYPERNOVA_BENCH_MIPLIB_TIME_LIMIT "60"
    CACHE STRING "MIPLIB benchmark per-instance time limit (seconds)")

add_custom_target(bench_miplib
    COMMAND $<TARGET_FILE:hypernova-bench>
        --dir "${CMAKE_CURRENT_SOURCE_DIR}"
        --suite miplib
        --expect "${CMAKE_CURRENT_SOURCE_DIR}/reference.csv"
        --time-limit ${HYPERNOVA_BENCH_MIPLIB_TIME_LIMIT}
        --csv "${CMAKE_CURRENT_BINARY_DIR}/miplib_bnb.csv"
        --out "${CMAKE_CURRENT_BINARY_DIR}/miplib_bnb.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS hypernova-bench
    USES_TERMINAL
    COMMENT "MIPLIB MIP benchmark (AUTO -> B&B, ${HYPERNOVA_BENCH_MIPLIB_TIME_LIMIT} s/instance)"
    VERBATIM)
