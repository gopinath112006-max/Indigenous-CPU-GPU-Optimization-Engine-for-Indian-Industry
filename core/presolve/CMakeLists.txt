cmake_minimum_required(VERSION 3.20)

add_library(hypernova_presolve
    presolve.cpp
)

target_include_directories(hypernova_presolve PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/presolve>
)

target_link_libraries(hypernova_presolve PUBLIC
    hypernova_model
    hypernova_numerical
)

install(TARGETS hypernova_presolve
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES presolve.hpp DESTINATION include/hypernova/presolve)