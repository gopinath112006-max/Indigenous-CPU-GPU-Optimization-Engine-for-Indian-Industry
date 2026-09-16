cmake_minimum_required(VERSION 3.20)

function(setup_bench_test target_name)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/api/cpp/include
        ${CMAKE_SOURCE_DIR}/benchmarks
    )
    target_link_libraries(${target_name} PRIVATE
        hypernova_benchmark
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_executable(test_benchmark_report
    test_benchmark_report.cpp
)
setup_bench_test(test_benchmark_report)

add_executable(test_benchmark_runner
    test_benchmark_runner.cpp
)
setup_bench_test(test_benchmark_runner)