cubey_add_app_target(
    cubey_example_spinning_cube
    OUTPUT_NAME spinning_cube
    SHADER_DEFINE CUBEY_SPINNING_CUBE_SHADER_DIR
    WINDOWED_SMOKE_TEST spinning_cube_reaches_glfw
    WINDOWED_SMOKE_PATTERN "spinning_cube: .*rendering indexed cube"
    SOURCES
        main.cpp
        spinning_cube_app.cpp
    SHADERS
        shaders/spinning_cube.vert
        shaders/spinning_cube.frag
)
