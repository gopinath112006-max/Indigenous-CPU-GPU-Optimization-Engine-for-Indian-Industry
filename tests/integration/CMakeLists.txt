cmake_minimum_required(VERSION 3.20)

function(setup_test_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/api/cpp/include
    )
    target_link_libraries(${target_name} PRIVATE
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_executable(test_integration
    test_integration.cpp
)
target_link_libraries(test_integration PRIVATE hypernova_core)
setup_test_target(test_integration)

add_executable(test_edge_cases
    test_edge_cases.cpp
)
target_link_libraries(test_edge_cases PRIVATE hypernova_core)
setup_test_target(test_edge_cases)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME test_python_api
        COMMAND ${Python3_EXECUTABLE} -c "import sys; sys.path.append('${CMAKE_SOURCE_DIR}/python'); import hypernova; p = hypernova.Problem(); print('Python API OK')"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
