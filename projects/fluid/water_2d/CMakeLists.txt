cubey_add_app_target(
    cubey_project_water_2d
    OUTPUT_NAME water_2d
    SHADER_DEFINE CUBEY_WATER_2D_SHADER_DIR
    WINDOWED_SMOKE_TEST water_2d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "water_2d: .*rendering 2D water project"
    SOURCES
        water_2d_app.cpp
        water_2d_commands.cpp
        water_2d_gpu_resources.cpp
        main.cpp
    SHADERS
        shaders/water_2d.vert
        shaders/water_2d_render.frag
        shaders/water_2d_reset.comp
        shaders/water_2d_force.comp
        shaders/water_2d_advect_velocity.comp
        shaders/water_2d_advect_phi.comp
        shaders/water_2d_reinitialize_phi.comp
        shaders/water_2d_divergence.comp
        shaders/water_2d_pressure.comp
        shaders/water_2d_projection.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_water_2d_tests
        water_2d_config_tests.cpp
    )
    target_include_directories(cubey_project_water_2d_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(cubey_project_water_2d_tests
        PRIVATE
            CUBEY_WATER_2D_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_water_2d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME water_2d_config_tests COMMAND cubey_project_water_2d_tests)

    set(CUBEY_WATER_2D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/water-2d-headless.png"
    )
    cubey_add_png_smoke_test(
        water_2d_headless_writes_png
        cubey_project_water_2d
        "${CUBEY_WATER_2D_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 8
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_WATER_2D_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/water-2d-headless.mp4"
        )
        cubey_add_video_smoke_test(
            water_2d_headless_writes_video
            cubey_project_water_2d
            "${CUBEY_WATER_2D_HEADLESS_VIDEO_SMOKE_OUTPUT}"
        )
    endif()
endif()
