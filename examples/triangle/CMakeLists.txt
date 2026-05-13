cubey_add_app_target(
    cubey_example_triangle
    OUTPUT_NAME triangle
    SHADER_DEFINE CUBEY_TRIANGLE_SHADER_DIR
    WINDOWED_SMOKE_TEST triangle_reaches_glfw
    WINDOWED_SMOKE_PATTERN "triangle: .*rendering dynamic triangle"
    SOURCES
        main.cpp
        triangle_app.cpp
    SHADERS
        shaders/triangle.vert
        shaders/triangle.frag
)
