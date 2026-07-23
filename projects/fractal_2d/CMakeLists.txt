cubey_add_app_target(
    cubey_project_fractal_2d
    OUTPUT_NAME fractal_2d
    SHADER_DEFINE CUBEY_FRACTAL_2D_SHADER_DIR
    WINDOWED_SMOKE_TEST fractal_2d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fractal_2d: .*rendering fullscreen fractal"
    SOURCES
        fractal_2d_app.cpp
        main.cpp
    SHADERS
        shaders/fractal.vert
        shaders/fractal.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)

if (BUILD_TESTING)
    add_executable(cubey_project_fractal_2d_tests
        fractal_2d_view_tests.cpp
    )
    target_include_directories(cubey_project_fractal_2d_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_project_fractal_2d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fractal_2d_view_tests COMMAND cubey_project_fractal_2d_tests)

    set(CUBEY_FRACTAL_2D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fractal-2d-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        fractal_2d_headless_writes_png
        cubey_project_fractal_2d
        "${CUBEY_FRACTAL_2D_HEADLESS_SMOKE_OUTPUT}"
    )
endif()
