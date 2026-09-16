cmake_minimum_required(VERSION 3.20)

add_library(hypernova_api
    api.cpp
)

target_include_directories(hypernova_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/hypernova>
)

target_link_libraries(hypernova_api PUBLIC hypernova_core)
target_link_libraries(hypernova_api PRIVATE nlohmann_json::nlohmann_json)

install(DIRECTORY include/ DESTINATION include/hypernova FILES_MATCHING PATTERN "*.hpp")