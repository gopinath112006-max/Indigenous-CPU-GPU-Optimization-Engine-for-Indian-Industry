cmake_minimum_required(VERSION 3.20)

add_library(hypernova_lp
    simplex.cpp
    interior_point.cpp
)

target_include_directories(hypernova_lp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/lp>
)

target_link_libraries(hypernova_lp PUBLIC
    hypernova_model
    hypernova_numerical
    hypernova_presolve
)

install(TARGETS hypernova_lp
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES simplex.hpp interior_point.hpp DESTINATION include/hypernova/lp)