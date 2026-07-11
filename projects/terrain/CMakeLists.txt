add_library(cubey_project_terrain_runtime STATIC
    terrain_source.cpp
)
target_include_directories(
    cubey_project_terrain_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    cubey_project_terrain_runtime
    PUBLIC
        cubey::core
    PRIVATE
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_source_tests
        terrain_source_tests.cpp
    )
    target_link_libraries(
        cubey_project_terrain_source_tests
        PRIVATE
            cubey_project_terrain_runtime
            cubey_project_warnings
    )
    add_test(NAME terrain_source_tests COMMAND cubey_project_terrain_source_tests)
    cubey_label_tests(terrain terrain_source_tests)
endif()
