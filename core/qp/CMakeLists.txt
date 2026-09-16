cmake_minimum_required(VERSION 3.20)

add_library(hypernova_qp
    active_set.cpp
    interior_point_qp.cpp
)

target_include_directories(hypernova_qp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/qp>
)

target_link_libraries(hypernova_qp PUBLIC
    hypernova_model
    hypernova_numerical
    hypernova_lp
)

install(TARGETS hypernova_qp
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES active_set.hpp interior_point_qp.hpp DESTINATION include/hypernova/qp)