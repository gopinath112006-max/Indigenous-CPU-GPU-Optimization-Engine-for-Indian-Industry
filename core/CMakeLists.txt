cmake_minimum_required(VERSION 3.20)

add_subdirectory(model)
add_subdirectory(numerical)
add_subdirectory(presolve)
add_subdirectory(lp)
add_subdirectory(milp)
add_subdirectory(qp)
add_subdirectory(execution)
add_subdirectory(validation)

add_library(hypernova_core INTERFACE)
target_include_directories(hypernova_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(hypernova_core INTERFACE
    hypernova_model
    hypernova_numerical
    hypernova_presolve
    hypernova_lp
    hypernova_milp
    hypernova_qp
    hypernova_execution
    hypernova_validation
)