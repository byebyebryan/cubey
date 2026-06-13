cubey_add_app_target(
    cubey_project_terrain_lab
    OUTPUT_NAME terrain_lab
    SHADER_DEFINE CUBEY_TERRAIN_LAB_SHADER_DIR
    WINDOWED_SMOKE_TEST terrain_lab_reaches_glfw
    WINDOWED_SMOKE_PATTERN "terrain_lab: .*rendering terrain lab"
    SOURCES
        main.cpp
        terrain_lab_app.cpp
        terrain_lab_fields.cpp
        terrain_lab_mesh.cpp
    SHADERS
        shaders/terrain_lab.vert
        shaders/terrain_lab.frag
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_lab_tests
        terrain_lab_config_tests.cpp
        terrain_lab_fields.cpp
        terrain_lab_mesh.cpp
    )
    target_include_directories(
        cubey_project_terrain_lab_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_terrain_lab_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME terrain_lab_config_tests COMMAND cubey_project_terrain_lab_tests)
endif()
