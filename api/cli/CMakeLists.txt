cmake_minimum_required(VERSION 3.20)

add_executable(hypernova
    main.cpp
    cli_commands.cpp
    interactive/PromptEngine.cpp
    interactive/ProblemBuilder.cpp
    interactive/SessionManager.cpp
    interactive/ExportManager.cpp
    interactive/InteractiveSession.cpp
)

target_include_directories(hypernova PRIVATE
    ${CMAKE_SOURCE_DIR}/api/cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hypernova PRIVATE hypernova_api hypernova_benchmark nlohmann_json::nlohmann_json)

install(TARGETS hypernova RUNTIME DESTINATION bin)