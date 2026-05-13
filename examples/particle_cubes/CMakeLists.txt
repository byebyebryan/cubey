cubey_add_app_target(
    cubey_example_particle_cubes
    OUTPUT_NAME particle_cubes
    SHADER_DEFINE CUBEY_PARTICLE_CUBES_SHADER_DIR
    WINDOWED_SMOKE_TEST particle_cubes_reaches_glfw
    WINDOWED_SMOKE_PATTERN "particle_cubes: .*rendering compute-driven cube particles"
    SOURCES
        main.cpp
        particle_cubes_app.cpp
    SHADERS
        shaders/particle_cubes.comp
        shaders/particle_cubes.vert
        shaders/particle_cubes.frag
)
