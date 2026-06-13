if (BUILD_TESTING)
    add_executable(cubey_project_terrain_lab_tests
        terrain_lab_config_tests.cpp
        terrain_lab_fields.cpp
    )
    target_include_directories(
        cubey_project_terrain_lab_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_terrain_lab_tests
        PRIVATE
            cubey::core
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME terrain_lab_config_tests COMMAND cubey_project_terrain_lab_tests)
endif()
