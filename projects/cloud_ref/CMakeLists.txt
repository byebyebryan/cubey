cubey_add_app_target(
    cubey_project_cloud_ref
    OUTPUT_NAME cloud_ref
    SHADER_DEFINE CUBEY_CLOUD_REF_SHADER_DIR
    WINDOWED_SMOKE_TEST cloud_ref_reaches_glfw
    WINDOWED_SMOKE_PATTERN "cloud_ref: .*rendering reference cloud project"
    SOURCES
        cloud_ref_app.cpp
        cloud_ref_config.cpp
        main.cpp
    SHADERS
        shaders/cloud_ref.vert
        shaders/cloud_ref_composite.frag
        shaders/cloud_ref_march.comp
        shaders/cloud_ref_perlin_worley.comp
        shaders/cloud_ref_weather.comp
        shaders/cloud_ref_worley.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_ref_common.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_ref_noise_common.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_cloud_ref_tests
        cloud_ref_config.cpp
        cloud_ref_config_tests.cpp
    )
    target_include_directories(
        cubey_project_cloud_ref_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_cloud_ref_tests
        PRIVATE
            cubey::core
            cubey::render
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME cloud_ref_config_tests COMMAND cubey_project_cloud_ref_tests)

    set(CUBEY_CLOUD_REF_SURFACE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/cloud-ref-surface-headless.png"
    )
    cubey_add_png_smoke_test(
        cloud_ref_surface_headless_writes_png
        cubey_project_cloud_ref
        "${CUBEY_CLOUD_REF_SURFACE_SMOKE_OUTPUT}"
        --headless
        --frames
        2
        --cloud-camera-mode
        surface
    )
    cubey_add_png_stats_test(
        cloud_ref_surface_headless_writes_png
        "${CUBEY_CLOUD_REF_SURFACE_SMOKE_OUTPUT}"
        0.005
        0.005
    )

    set(CUBEY_CLOUD_REF_HIGH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/cloud-ref-high-headless.png"
    )
    cubey_add_png_smoke_test(
        cloud_ref_high_headless_writes_png
        cubey_project_cloud_ref
        "${CUBEY_CLOUD_REF_HIGH_SMOKE_OUTPUT}"
        --headless
        --frames
        2
        --cloud-camera-mode
        high
    )
    cubey_add_png_stats_test(
        cloud_ref_high_headless_writes_png
        "${CUBEY_CLOUD_REF_HIGH_SMOKE_OUTPUT}"
        0.005
        0.005
    )
endif()
