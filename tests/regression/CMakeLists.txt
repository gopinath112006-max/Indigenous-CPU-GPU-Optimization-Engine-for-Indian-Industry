cmake_minimum_required(VERSION 3.20)

# Regression tests: end-to-end guards of frozen solver behaviour.
# They exercise the public API against the shipped model files and the
# Netlib baseline subset; HYPERNOVA_SRC_DIR lets the fixtures resolve
# repo-relative paths from any working directory.

add_executable(test_regression_smoke
    test_regression_smoke.cpp
)
target_include_directories(test_regression_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/api/cpp/include
)
target_compile_definitions(test_regression_smoke PRIVATE
    HYPERNOVA_SRC_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_regression_smoke PRIVATE
    hypernova_api
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_regression_smoke COMMAND test_regression_smoke)