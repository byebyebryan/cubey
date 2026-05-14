cubey_add_app_target(
    cubey_project_fluid_2d
    OUTPUT_NAME fluid_2d
    SHADER_DEFINE CUBEY_FLUID_2D_SHADER_DIR
    WINDOWED_SMOKE_TEST fluid_2d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fluid_2d: .*rendering 2D fluid project"
    SOURCES
        fluid_2d_app.cpp
        fluid_2d_commands.cpp
        main.cpp
        fluid_2d_gpu_resources.cpp
    SHADERS
        shaders/fluid_2d.vert
        shaders/fluid_2d_render.frag
        shaders/fluid_2d_inject.comp
        shaders/fluid_2d_advect.comp
        shaders/fluid_2d_divergence.comp
        shaders/fluid_2d_pressure.comp
        shaders/fluid_2d_projection.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_fluid_2d_tests
        fluid_2d_config_tests.cpp
    )
    target_include_directories(cubey_project_fluid_2d_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_project_fluid_2d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fluid_2d_config_tests COMMAND cubey_project_fluid_2d_tests)

    set(CUBEY_FLUID_2D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-2d-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        fluid_2d_headless_writes_png
        cubey_project_fluid_2d
        "${CUBEY_FLUID_2D_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 8
    )
endif()
