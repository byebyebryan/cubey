cubey_add_app_target(
    cubey_example_particles
    OUTPUT_NAME particles
    SHADER_DEFINE CUBEY_PARTICLES_SHADER_DIR
    WINDOWED_SMOKE_TEST particles_reaches_glfw
    WINDOWED_SMOKE_PATTERN "particles: .*rendering compute attractor particles"
    SOURCES
        main.cpp
        particles_app.cpp
    SHADERS
        shaders/particles.comp
        shaders/particles.vert
        shaders/particles.frag
)
