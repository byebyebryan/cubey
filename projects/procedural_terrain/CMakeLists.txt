if (BUILD_TESTING)
    add_executable(cubey_project_procedural_terrain_tests
        procedural_terrain_config_tests.cpp
        procedural_terrain_fields.cpp
    )
    target_include_directories(
        cubey_project_procedural_terrain_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_procedural_terrain_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME procedural_terrain_config_tests COMMAND cubey_project_procedural_terrain_tests)
endif()
