cmake_minimum_required(VERSION 3.20)

add_library(hypernova_validation
    solution_verifier.cpp
    solve_report.cpp
    iis.cpp
    certificate.cpp
    diagnostics.cpp
)

target_include_directories(hypernova_validation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/validation>
)

target_link_libraries(hypernova_validation PUBLIC
    hypernova_model
    hypernova_numerical
)
target_link_libraries(hypernova_validation PRIVATE
    nlohmann_json::nlohmann_json
    hypernova_lp
)

install(TARGETS hypernova_validation
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES solution_verifier.hpp solve_report.hpp iis.hpp certificate.hpp diagnostics.hpp DESTINATION include/hypernova/validation)