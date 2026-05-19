cubey_add_app_target(
    cubey_project_fluid_3d
    OUTPUT_NAME fluid_3d
    SHADER_DEFINE CUBEY_FLUID_3D_SHADER_DIR
    WINDOWED_SMOKE_TEST fluid_3d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fluid_3d: .*rendering 3D fluid project"
    SOURCES
        fluid_3d_app.cpp
        fluid_3d_commands.cpp
        fluid_3d_gpu_resources.cpp
        fluid_3d_injectors.cpp
        main.cpp
    SHADERS
        shaders/fluid_3d.vert
        shaders/fluid_3d_raymarch.frag
        shaders/fluid_3d_reset.comp
        shaders/fluid_3d_advect.comp
        shaders/fluid_3d_divergence.comp
        shaders/fluid_3d_pressure.comp
        shaders/fluid_3d_projection.comp
        shaders/fluid_3d_shadow.comp
)

if (BUILD_TESTING)
    add_executable(cubey_project_fluid_3d_tests
        fluid_3d_injectors.cpp
        fluid_3d_config_tests.cpp
    )
    target_include_directories(cubey_project_fluid_3d_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(cubey_project_fluid_3d_tests
        PRIVATE
            CUBEY_FLUID_3D_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_fluid_3d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fluid_3d_config_tests COMMAND cubey_project_fluid_3d_tests)

    set(CUBEY_FLUID_3D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-3d-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        fluid_3d_headless_writes_png
        cubey_project_fluid_3d
        "${CUBEY_FLUID_3D_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 4
        --grid-width 32
        --grid-height 32
        --grid-depth 32
    )
endif()
