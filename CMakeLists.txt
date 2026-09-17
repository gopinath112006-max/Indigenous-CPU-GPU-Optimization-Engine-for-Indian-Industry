cmake_minimum_required(VERSION 3.20)
project(HyperNova VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -fPIC)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(HYPERNOVA_BUILD_TESTS "Build tests" ON)
option(HYPERNOVA_BUILD_CLI "Build CLI tool" ON)
option(HYPERNOVA_ENABLE_CUDA "Enable CUDA backend" OFF)
option(HYPERNOVA_ENABLE_HIP "Enable HIP backend" OFF)
option(HYPERNOVA_ENABLE_SYCL "Enable SYCL backend" OFF)

# Fetch GTest up front so every test directory (including benchmarks/tests,
# which is configured before tests/) can link against it.
if(HYPERNOVA_BUILD_TESTS)
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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Must come before add_subdirectory() of any directory that calls add_test(),
# otherwise benchmark tests never get registered with CTest.
enable_testing()

add_subdirectory(core)
add_subdirectory(api)
add_subdirectory(benchmarks)

if(HYPERNOVA_BUILD_CLI)
    add_subdirectory(api/cli)
endif()

if(HYPERNOVA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# MinGW runtime DLLs: pin the exact libstdc++/libgcc/libwinpthread the
# toolchain linked against next to the binaries. Windows searches the
# application directory before PATH, so executables and tests keep loading a
# matching C++ runtime even when PATH resolves libstdc++-6.dll from another
# toolchain first (e.g. Git for Windows' own mingw64\bin, which ships an
# MSVCRT-flavored libstdc++ with a different GCC ABI: mixed-ABI loads crash
# with segfaults in std::string/locale code or fail with STATUS_ENTRYPOINT_NOT_FOUND
# (0xc0000139) on symbols the older DLL does not export).
# ---------------------------------------------------------------------------
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(HYPERNOVA_RUNTIME_DLLS
        "${CMAKE_CXX_COMPILER}/../libstdc++-6.dll"
        "${CMAKE_CXX_COMPILER}/../libgcc_s_seh-1.dll"
        "${CMAKE_CXX_COMPILER}/../libwinpthread-1.dll"
    )
    foreach(dll ${HYPERNOVA_RUNTIME_DLLS})
        get_filename_component(dll_abs "${dll}" ABSOLUTE)
        if(EXISTS "${dll_abs}")
            file(COPY "${dll_abs}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        else()
            message(STATUS "MinGW runtime DLL not found next to compiler, skipping: ${dll_abs}")
        endif()
    endforeach()
endif()

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/HyperNovaConfig.cmake.in
    ${CMAKE_BINARY_DIR}/HyperNovaConfig.cmake
    @ONLY
)

install(DIRECTORY api/cpp/include/ DESTINATION include/hypernova FILES_MATCHING PATTERN "*.hpp")
install(TARGETS
    hypernova_model
    hypernova_numerical
    hypernova_presolve
    hypernova_lp
    hypernova_milp
    hypernova_qp
    hypernova_execution
    hypernova_validation
    hypernova_core
    hypernova_api
    EXPORT HyperNovaTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(EXPORT HyperNovaTargets FILE HyperNovaTargets.cmake DESTINATION lib/cmake/HyperNova)