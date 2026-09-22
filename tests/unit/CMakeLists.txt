cmake_minimum_required(VERSION 3.20)

function(setup_test_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/core
        ${CMAKE_SOURCE_DIR}/api
        ${CMAKE_SOURCE_DIR}/api/cpp/include
    )
    target_link_libraries(${target_name} PRIVATE
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_executable(test_numerical
    test_numerical.cpp
)
target_link_libraries(test_numerical PRIVATE hypernova_numerical)
setup_test_target(test_numerical)

add_executable(test_model
    test_model.cpp
)
target_link_libraries(test_model PRIVATE hypernova_model nlohmann_json::nlohmann_json)
setup_test_target(test_model)

add_executable(test_presolve
    test_presolve.cpp
)
target_link_libraries(test_presolve PRIVATE hypernova_presolve)
setup_test_target(test_presolve)

add_executable(test_lp
    test_lp.cpp
)
target_link_libraries(test_lp PRIVATE hypernova_lp)
setup_test_target(test_lp)

add_executable(test_milp
    test_milp.cpp
)
target_link_libraries(test_milp PRIVATE hypernova_milp)
setup_test_target(test_milp)

add_executable(test_parallel_bnb
    test_parallel_bnb.cpp
)
target_link_libraries(test_parallel_bnb PRIVATE hypernova_milp)
setup_test_target(test_parallel_bnb)

add_executable(test_qp
    test_qp.cpp
)
target_link_libraries(test_qp PRIVATE hypernova_qp hypernova_milp)
setup_test_target(test_qp)

add_executable(test_validation
    test_validation.cpp
)
target_link_libraries(test_validation PRIVATE hypernova_validation nlohmann_json::nlohmann_json)
setup_test_target(test_validation)

add_executable(test_api
    test_api.cpp
)
target_link_libraries(test_api PRIVATE hypernova_api)
setup_test_target(test_api)

add_executable(test_gpu_backend
    test_gpu_backend.cpp
)
target_link_libraries(test_gpu_backend PRIVATE hypernova_execution hypernova_api hypernova_model)
setup_test_target(test_gpu_backend)

add_executable(test_roundtrip
    test_roundtrip.cpp
)
target_link_libraries(test_roundtrip PRIVATE
    hypernova_model
    hypernova_presolve
    hypernova_lp
    hypernova_validation
)
setup_test_target(test_roundtrip)

add_executable(test_industrial_cases
    test_industrial_cases.cpp
)
target_link_libraries(test_industrial_cases PRIVATE hypernova_api)
setup_test_target(test_industrial_cases)

add_executable(test_scalability_stress
    test_scalability_stress.cpp
)
target_link_libraries(test_scalability_stress PRIVATE hypernova_api)
setup_test_target(test_scalability_stress)

add_executable(test_rest_system
    test_rest_system.cpp
)
target_link_libraries(test_rest_system PRIVATE hypernova_api hypernova_model nlohmann_json::nlohmann_json)
setup_test_target(test_rest_system)




add_executable(test_randomized
    test_randomized.cpp
)
target_link_libraries(test_randomized PRIVATE hypernova_api hypernova_milp hypernova_model)
setup_test_target(test_randomized)
