cubey_add_app_target(
    cubey_project_smoke_2d
    OUTPUT_NAME smoke_2d
    SHADER_DEFINE CUBEY_SMOKE_2D_SHADER_DIR
    WINDOWED_SMOKE_TEST smoke_2d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "smoke_2d: .*rendering 2D smoke project"
    SOURCES
        smoke_2d_app.cpp
        smoke_2d_commands.cpp
        smoke_2d_diagnostics.cpp
        smoke_2d_injectors.cpp
        smoke_2d_ui.cpp
        main.cpp
        smoke_2d_gpu_resources.cpp
    SHADERS
        shaders/smoke_2d.vert
        shaders/smoke_2d_render.frag
        shaders/smoke_2d_inject.comp
        shaders/smoke_2d_advect_predict.comp
        shaders/smoke_2d_advect_correct.comp
        shaders/smoke_2d_curl.comp
        shaders/smoke_2d_vorticity.comp
        shaders/smoke_2d_divergence.comp
        shaders/smoke_2d_pressure.comp
        shaders/smoke_2d_pressure_rbgs.comp
        shaders/smoke_2d_projection.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_smoke_2d_tests
        smoke_2d_injectors.cpp
        smoke_2d_config_tests.cpp
    )
    target_include_directories(cubey_project_smoke_2d_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(cubey_project_smoke_2d_tests
        PRIVATE
            CUBEY_SMOKE_2D_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_smoke_2d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME smoke_2d_config_tests COMMAND cubey_project_smoke_2d_tests)

    set(CUBEY_SMOKE_2D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/smoke-2d-headless.png"
    )
    cubey_add_png_smoke_test(
        smoke_2d_headless_writes_png
        cubey_project_smoke_2d
        "${CUBEY_SMOKE_2D_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 8
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_SMOKE_2D_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/smoke-2d-headless.mp4"
        )
        cubey_add_video_smoke_test(
            smoke_2d_headless_writes_video
            cubey_project_smoke_2d
            "${CUBEY_SMOKE_2D_HEADLESS_VIDEO_SMOKE_OUTPUT}"
        )
    endif()
endif()
