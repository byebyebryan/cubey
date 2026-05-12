add_executable(cubey_project_fluid_2d
    fluid_2d_app.cpp
    fluid_2d_commands.cpp
    main.cpp
    fluid_2d_gpu_resources.cpp
)
set_target_properties(cubey_project_fluid_2d PROPERTIES OUTPUT_NAME fluid_2d)

target_compile_definitions(
    cubey_project_fluid_2d
    PRIVATE
        CUBEY_FLUID_2D_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_project_fluid_2d
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
)

cubey_add_glsl_shaders(
    cubey_project_fluid_2d
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
        shaders/fluid_2d.vert
        shaders/fluid_2d_render.frag
        shaders/fluid_2d_inject.comp
        shaders/fluid_2d_advect.comp
        shaders/fluid_2d_divergence.comp
        shaders/fluid_2d_pressure.comp
        shaders/fluid_2d_projection.comp
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

    cubey_add_windowed_smoke_test(
        fluid_2d_reaches_glfw
        cubey_project_fluid_2d
        "fluid_2d: .*rendering 2D fluid project"
    )

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
