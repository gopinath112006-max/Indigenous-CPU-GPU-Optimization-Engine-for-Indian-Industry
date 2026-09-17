cmake_minimum_required(VERSION 3.20)

# Opt-in benchmark target (deliberately NOT part of the fast ctest gate, since
# some instances are documented TIME_LIMIT failures):
#
#   cmake --build build --target bench_netlib
#
# Drives the shared hypernova-bench harness over the Netlib LP subset using the
# built-in reference table (no --expect needed).
set(HYPERNOVA_BENCH_NETLIB_TIME_LIMIT "60"
    CACHE STRING "Netlib benchmark per-instance time limit (seconds)")

add_custom_target(bench_netlib
    COMMAND $<TARGET_FILE:hypernova-bench>
        --dir "${CMAKE_CURRENT_SOURCE_DIR}"
        --suite netlib
        --time-limit ${HYPERNOVA_BENCH_NETLIB_TIME_LIMIT}
        --csv "${CMAKE_CURRENT_BINARY_DIR}/netlib_auto.csv"
        --out "${CMAKE_CURRENT_BINARY_DIR}/netlib_auto.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS hypernova-bench
    USES_TERMINAL
    COMMENT "Netlib LP benchmark (AUTO engine, ${HYPERNOVA_BENCH_NETLIB_TIME_LIMIT} s/instance)"
    VERBATIM)
