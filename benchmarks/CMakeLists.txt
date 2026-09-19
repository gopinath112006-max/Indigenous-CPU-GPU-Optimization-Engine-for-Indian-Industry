cmake_minimum_required(VERSION 3.20)

# Benchmark harness shared across all suites: report model, reference tables,
# and the API-driven suite runner.
add_library(hypernova_benchmark
    benchmark_report.cpp
    netlib_reference.cpp
    benchmark_runner.cpp
)

target_include_directories(hypernova_benchmark PUBLIC
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/api/cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hypernova_benchmark PUBLIC
    hypernova_api
    nlohmann_json::nlohmann_json
)
# benchmark_runner.cpp calls the validation layer directly.
target_link_libraries(hypernova_benchmark PRIVATE
    hypernova_validation
)

install(TARGETS hypernova_benchmark
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Standalone CLI runner: hypernova-bench
add_executable(hypernova-bench
    benchmark_main.cpp
)
target_link_libraries(hypernova-bench PRIVATE hypernova_benchmark)
install(TARGETS hypernova-bench RUNTIME DESTINATION bin)

if(HYPERNOVA_BUILD_TESTS)
    add_subdirectory(miplib)
    add_subdirectory(netlib)
    add_subdirectory(qplib)
    add_subdirectory(mittelmann)
    add_subdirectory(industrial-cases)
    add_subdirectory(generated)
    add_subdirectory(tests)

    add_custom_target(bench_all
        DEPENDS bench_netlib bench_miplib bench_qplib bench_mittelmann bench_industrial
        COMMENT "Runs all HyperNova benchmark suites (Netlib, MIPLIB, QPLIB, Mittelmann, Industrial)"
    )
endif()