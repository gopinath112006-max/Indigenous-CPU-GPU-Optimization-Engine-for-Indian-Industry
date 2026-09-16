cmake_minimum_required(VERSION 3.20)

add_library(hypernova_numerical
    sparse_matrix.cpp
    factorization.cpp
    refinement.cpp
    scaling.cpp
    tolerance.cpp
)

target_include_directories(hypernova_numerical PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/numerical>
)

target_link_libraries(hypernova_numerical PUBLIC Threads::Threads)

install(TARGETS hypernova_numerical
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES sparse_matrix.hpp factorization.hpp refinement.hpp scaling.hpp tolerance.hpp DESTINATION include/hypernova/numerical)