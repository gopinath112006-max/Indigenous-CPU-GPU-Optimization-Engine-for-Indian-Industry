cmake_minimum_required(VERSION 3.20)

# Opt-in benchmark target (NOT part of the fast ctest gate). QPLIB instances are
# MIQPs; only a subset is convex, the rest are genuinely indefinite, so this is a
# "parse + time-limited attempt" suite rather than a pass/fail gate.
#
#   cmake --build build --target bench_qplib
#
# Reference objectives (QPLIB best-known) live in ./reference.csv.
set(HYPERNOVA_BENCH_QPLIB_TIME_LIMIT "60"
    CACHE STRING "QPLIB benchmark per-instance time limit (seconds)")

add_custom_target(bench_qplib
    COMMAND $<TARGET_FILE:hypernova-bench>
        --dir "${CMAKE_CURRENT_SOURCE_DIR}"
        --suite qplib
        --expect "${CMAKE_CURRENT_SOURCE_DIR}/reference.csv"
        --time-limit ${HYPERNOVA_BENCH_QPLIB_TIME_LIMIT}
        --csv "${CMAKE_CURRENT_BINARY_DIR}/qplib_bnb.csv"
        --out "${CMAKE_CURRENT_BINARY_DIR}/qplib_bnb.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS hypernova-bench
    USES_TERMINAL
    COMMENT "QPLIB MIQP benchmark (AUTO -> B&B, ${HYPERNOVA_BENCH_QPLIB_TIME_LIMIT} s/instance)"
    VERBATIM)
