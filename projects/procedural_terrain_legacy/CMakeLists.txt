cubey_add_app_target(
    cubey_project_procedural_terrain_legacy
    OUTPUT_NAME procedural_terrain_legacy
    SHADER_DEFINE CUBEY_PROCEDURAL_TERRAIN_SHADER_DIR
    WINDOWED_SMOKE_TEST procedural_terrain_legacy_reaches_glfw
    WINDOWED_SMOKE_PATTERN "procedural_terrain_legacy: .*rendering legacy procedural terrain"
    SOURCES
        main.cpp
        procedural_terrain_app.cpp
        procedural_terrain_fields.cpp
        procedural_terrain_mesh.cpp
        procedural_terrain_ui.cpp
    SHADERS
        shaders/procedural_terrain.vert
        shaders/procedural_terrain.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/random.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/operators.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_procedural_terrain_legacy_tests
        procedural_terrain_config_tests.cpp
        procedural_terrain_fields.cpp
        procedural_terrain_mesh.cpp
    )
    target_include_directories(
        cubey_project_procedural_terrain_legacy_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_procedural_terrain_legacy_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME procedural_terrain_legacy_config_tests COMMAND cubey_project_procedural_terrain_legacy_tests)

    set(CUBEY_PROCEDURAL_TERRAIN_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/procedural-terrain-legacy-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        procedural_terrain_legacy_headless_writes_png
        cubey_project_procedural_terrain_legacy
        "${CUBEY_PROCEDURAL_TERRAIN_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --grid-width 65
        --grid-height 65
    )

    cubey_label_tests(
        legacy
        procedural_terrain_legacy_reaches_glfw
        procedural_terrain_legacy_config_tests
        procedural_terrain_legacy_headless_writes_png
    )
endif()
