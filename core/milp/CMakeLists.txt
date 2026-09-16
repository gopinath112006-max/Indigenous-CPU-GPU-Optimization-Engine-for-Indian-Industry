cmake_minimum_required(VERSION 3.20)

add_library(hypernova_milp
    branch_and_bound.cpp
    parallel_branch_and_bound.cpp
    cutting_planes.cpp
    heuristics.cpp
    node_selection.cpp
    node_presolve.cpp
    symmetry.cpp
)

target_include_directories(hypernova_milp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/milp>
)

target_link_libraries(hypernova_milp PUBLIC
    hypernova_model
    hypernova_numerical
    hypernova_lp
    hypernova_qp
    hypernova_presolve
    hypernova_execution
)

install(TARGETS hypernova_milp
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES branch_and_bound.hpp cutting_planes.hpp heuristics.hpp node_selection.hpp node_presolve.hpp symmetry.hpp DESTINATION include/hypernova/milp)