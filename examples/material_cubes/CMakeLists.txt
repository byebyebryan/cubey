cubey_add_app_target(
    cubey_example_material_cubes
    OUTPUT_NAME material_cubes
    SHADER_DEFINE CUBEY_MATERIAL_CUBES_SHADER_DIR
    WINDOWED_SMOKE_TEST material_cubes_reaches_glfw
    WINDOWED_SMOKE_PATTERN "material_cubes: .*rendering material instance cubes"
    SOURCES
        main.cpp
        material_cubes_app.cpp
    SHADERS
        shaders/material_cubes.vert
        shaders/material_cubes.frag
)
