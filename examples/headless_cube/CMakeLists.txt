cubey_add_app_target(
    cubey_example_headless_cube
    OUTPUT_NAME headless_cube
    SHADER_DEFINE CUBEY_HEADLESS_CUBE_SHADER_DIR
    SOURCES
        headless_cube_app.cpp
        main.cpp
    SHADERS
        shaders/headless_cube.vert
        shaders/headless_cube.frag
)

if (BUILD_TESTING)
    set(CUBEY_HEADLESS_CUBE_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/headless-cube-smoke.png"
    )
    cubey_add_png_smoke_test(
        headless_cube_writes_png
        cubey_example_headless_cube
        "${CUBEY_HEADLESS_CUBE_SMOKE_OUTPUT}"
    )
endif()
