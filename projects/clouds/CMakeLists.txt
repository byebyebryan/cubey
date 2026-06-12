cubey_add_app_target(
    cubey_project_clouds
    OUTPUT_NAME clouds
    SHADER_DEFINE CUBEY_CLOUDS_SHADER_DIR
    WINDOWED_SMOKE_TEST clouds_reaches_glfw
    WINDOWED_SMOKE_PATTERN "clouds: .*rendering planet-aware clouds project"
    SOURCES
        clouds_app.cpp
        clouds_config.cpp
        main.cpp
    SHADERS
        shaders/clouds.vert
        shaders/clouds.frag
        shaders/clouds_composite.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/clouds_model.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_clouds_tests
        clouds_config.cpp
        clouds_config_tests.cpp
    )
    target_include_directories(
        cubey_project_clouds_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_clouds_tests
        PRIVATE
            cubey::core
            cubey::render
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME clouds_config_tests COMMAND cubey_project_clouds_tests)

    set(CUBEY_CLOUDS_SURFACE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/clouds-surface-headless.png"
    )
    cubey_add_png_smoke_test(
        clouds_surface_headless_writes_png
        cubey_project_clouds
        "${CUBEY_CLOUDS_SURFACE_SMOKE_OUTPUT}"
        --headless
        --frames
        2
        --cloud-camera-mode
        surface
    )
    cubey_add_png_stats_test(
        clouds_surface_headless_writes_png
        "${CUBEY_CLOUDS_SURFACE_SMOKE_OUTPUT}"
        0.005
        0.005
    )

    set(CUBEY_CLOUDS_HIGH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/clouds-high-headless.png"
    )
    cubey_add_png_smoke_test(
        clouds_high_headless_writes_png
        cubey_project_clouds
        "${CUBEY_CLOUDS_HIGH_SMOKE_OUTPUT}"
        --headless
        --frames
        2
        --cloud-camera-mode
        high
    )
    cubey_add_png_stats_test(
        clouds_high_headless_writes_png
        "${CUBEY_CLOUDS_HIGH_SMOKE_OUTPUT}"
        0.005
        0.005
    )

    set(CUBEY_CLOUDS_ORBIT_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/clouds-orbit-headless.png"
    )
    cubey_add_png_smoke_test(
        clouds_orbit_headless_writes_png
        cubey_project_clouds
        "${CUBEY_CLOUDS_ORBIT_SMOKE_OUTPUT}"
        --headless
        --frames
        2
        --cloud-camera-mode
        orbit
    )
    cubey_add_png_stats_test(
        clouds_orbit_headless_writes_png
        "${CUBEY_CLOUDS_ORBIT_SMOKE_OUTPUT}"
        0.005
        0.005
    )
endif()
