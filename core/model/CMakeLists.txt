cmake_minimum_required(VERSION 3.20)

add_library(hypernova_model
    problem.cpp
    mps_parser.cpp
    lp_parser.cpp
)

target_include_directories(hypernova_model PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/model>
)

target_link_libraries(hypernova_model PUBLIC
    hypernova_numerical
    Threads::Threads
)

install(TARGETS hypernova_model
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES problem.hpp mps_parser.hpp lp_parser.hpp DESTINATION include/hypernova/model)