cubey_add_app_target(
    cubey_example_fractal
    OUTPUT_NAME fractal
    SHADER_DEFINE CUBEY_FRACTAL_SHADER_DIR
    WINDOWED_SMOKE_TEST fractal_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fractal: .*rendering fullscreen fractal"
    SOURCES
        fractal_app.cpp
        main.cpp
    SHADERS
        shaders/fractal.vert
        shaders/fractal.frag
)

if (BUILD_TESTING)
    add_executable(cubey_example_fractal_tests
        fractal_view_tests.cpp
    )
    target_include_directories(cubey_example_fractal_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_example_fractal_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fractal_view_tests COMMAND cubey_example_fractal_tests)

    set(CUBEY_FRACTAL_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fractal-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        fractal_headless_writes_png
        cubey_example_fractal
        "${CUBEY_FRACTAL_HEADLESS_SMOKE_OUTPUT}"
        --headless
    )
endif()
