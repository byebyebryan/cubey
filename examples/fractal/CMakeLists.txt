add_executable(cubey_example_fractal
    fractal_app.cpp
    main.cpp
)
set_target_properties(cubey_example_fractal PROPERTIES OUTPUT_NAME fractal)

target_compile_definitions(
    cubey_example_fractal
    PRIVATE
        CUBEY_FRACTAL_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_fractal
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
)

cubey_add_glsl_shaders(
    cubey_example_fractal
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
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

    cubey_add_windowed_smoke_test(
        fractal_reaches_glfw
        cubey_example_fractal
        "fractal: .*rendering fullscreen fractal"
    )

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
