cmake_minimum_required(VERSION 3.20)

add_library(hypernova_api
    api.cpp
)

target_include_directories(hypernova_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/core>
    $<INSTALL_INTERFACE:include/hypernova>
)

target_link_libraries(hypernova_api PUBLIC hypernova_core PRIVATE nlohmann_json::nlohmann_json)

add_library(hypernova_c_api SHARED
    ../c_api/hypernova_c_api.cpp
)

target_include_directories(hypernova_c_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../c_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/core>
)

target_link_libraries(hypernova_c_api PUBLIC hypernova_api)

install(FILES ../c_api/hypernova_c_api.h DESTINATION include/hypernova)