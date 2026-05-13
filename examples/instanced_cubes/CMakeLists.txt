cubey_add_app_target(
    cubey_example_instanced_cubes
    OUTPUT_NAME instanced_cubes
    SHADER_DEFINE CUBEY_INSTANCED_CUBES_SHADER_DIR
    WINDOWED_SMOKE_TEST instanced_cubes_reaches_glfw
    WINDOWED_SMOKE_PATTERN "instanced_cubes: .*rendering instanced cube grid"
    SOURCES
        main.cpp
        instanced_cubes_app.cpp
    SHADERS
        shaders/instanced_cubes.vert
        shaders/instanced_cubes.frag
)
