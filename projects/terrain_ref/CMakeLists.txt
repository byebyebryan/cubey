cubey_add_app_target(
    cubey_project_terrain_ref
    OUTPUT_NAME terrain_ref
    SHADER_DEFINE CUBEY_TERRAIN_REF_SHADER_DIR
    WINDOWED_SMOKE_TEST terrain_ref_reaches_glfw
    WINDOWED_SMOKE_PATTERN "terrain_ref: .*rendering terrain reference"
    SOURCES
        main.cpp
        shadertoy_mountain_reference.cpp
        terrain_engine_reference.cpp
        terrain_ref_app.cpp
        terrain_ref_config.cpp
        terrain_ref_mesh.cpp
    SHADERS
        shaders/terrain_ref.vert
        shaders/terrain_ref.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_engine_reference.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadertoy_mountain_reference.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_ref_tests
        terrain_engine_reference.cpp
        shadertoy_mountain_reference.cpp
        terrain_ref_config.cpp
        terrain_ref_mesh.cpp
        terrain_ref_tests.cpp
    )
    target_include_directories(
        cubey_project_terrain_ref_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_terrain_ref_tests
        PRIVATE
            cubey::core
            cubey::render
            cubey_project_warnings
    )
    add_test(NAME terrain_ref_tests COMMAND cubey_project_terrain_ref_tests)

    set(CUBEY_TERRAIN_REF_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-ref-headless.png"
    )
    cubey_add_png_smoke_test(
        terrain_ref_headless_writes_png
        cubey_project_terrain_ref
        "${CUBEY_TERRAIN_REF_HEADLESS_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --terrain-camera-preset
        oblique
    )
    cubey_add_png_stats_test(
        terrain_ref_headless_writes_png
        "${CUBEY_TERRAIN_REF_HEADLESS_OUTPUT}"
        0.00001
        0.00005
    )
endif()
