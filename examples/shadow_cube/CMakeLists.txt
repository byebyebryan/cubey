cubey_add_app_target(
    cubey_example_shadow_cube
    OUTPUT_NAME shadow_cube
    SHADER_DEFINE CUBEY_SHADOW_CUBE_SHADER_DIR
    WINDOWED_SMOKE_TEST shadow_cube_reaches_glfw
    WINDOWED_SMOKE_PATTERN "shadow_cube: .*rendering directional shadow cube"
    SOURCES
        main.cpp
        shadow_cube_app.cpp
    SHADERS
        shaders/shadow_depth.vert
        shaders/shadow_cube.vert
        shaders/shadow_cube.frag
        shaders/shadow_present.vert
        shaders/shadow_present.frag
)
