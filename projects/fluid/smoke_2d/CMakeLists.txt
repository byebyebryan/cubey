cubey_add_app_target(
    cubey_project_smoke_2d
    OUTPUT_NAME smoke_2d
    SHADER_DEFINE CUBEY_SMOKE_2D_SHADER_DIR
    WINDOWED_SMOKE_TEST smoke_2d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "smoke_2d: .*rendering 2D smoke project"
    SOURCES
        ../sim/smoke_2d/smoke_2d_app.cpp
        ../sim/smoke_2d/smoke_2d_commands.cpp
        ../sim/smoke_2d/smoke_2d_diagnostics.cpp
        ../sim/smoke_2d/smoke_2d_injectors.cpp
        ../sim/smoke_2d/smoke_2d_ui.cpp
        main.cpp
        ../sim/smoke_2d/smoke_2d_gpu_resources.cpp
    SHADERS
        ../sim/smoke_2d/shaders/smoke_2d.vert
        ../sim/smoke_2d/shaders/smoke_2d_render.frag
        ../sim/smoke_2d/shaders/smoke_2d_inject.comp
        ../sim/smoke_2d/shaders/smoke_2d_advect_predict.comp
        ../sim/smoke_2d/shaders/smoke_2d_advect_correct.comp
        ../sim/smoke_2d/shaders/smoke_2d_curl.comp
        ../sim/smoke_2d/shaders/smoke_2d_vorticity.comp
        ../sim/smoke_2d/shaders/smoke_2d_divergence.comp
        ../sim/smoke_2d/shaders/smoke_2d_pressure.comp
        ../sim/smoke_2d/shaders/smoke_2d_pressure_rbgs.comp
        ../sim/smoke_2d/shaders/smoke_2d_projection.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)

target_include_directories(
    cubey_project_smoke_2d
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/smoke_2d"
)

if (BUILD_TESTING)
    add_executable(cubey_project_smoke_2d_tests
        ../sim/smoke_2d/smoke_2d_injectors.cpp
        ../sim/smoke_2d/smoke_2d_config_tests.cpp
    )
    target_include_directories(
        cubey_project_smoke_2d_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../sim/smoke_2d"
    )
    target_compile_definitions(cubey_project_smoke_2d_tests
        PRIVATE
            CUBEY_SMOKE_2D_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../sim/smoke_2d"
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
