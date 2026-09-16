cmake_minimum_required(VERSION 3.20)

# GTest is fetched/initialized at the top-level CMakeLists so that benchmark
# tests (configured before this directory) can link GTest targets safely.
if(NOT TARGET GTest::gtest)
    option(HYPERNOVA_FETCH_GTEST "Download GTest via FetchContent if not found" ON)
    if(HYPERNOVA_FETCH_GTEST)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        FetchContent_MakeAvailable(googletest)
    else()
        find_package(GTest QUIET)
    endif()
endif()

if(NOT GTest_FOUND AND NOT TARGET GTest::gtest)
    message(WARNING "GTest not found, tests will not be built")
    return()
endif()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(regression)