add_executable(cubey_example_particles
    main.cpp
    particles_app.cpp
)
set_target_properties(cubey_example_particles PROPERTIES OUTPUT_NAME particles)

target_compile_definitions(
    cubey_example_particles
    PRIVATE
        CUBEY_PARTICLES_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_particles
    PRIVATE
        cubey::cubey
        cubey::app
        cubey_project_warnings
)

cubey_add_glsl_shaders(
    cubey_example_particles
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
        shaders/particles.comp
        shaders/particles.vert
        shaders/particles.frag
)

if (BUILD_TESTING)
    cubey_add_windowed_smoke_test(
        particles_reaches_glfw
        cubey_example_particles
        "particles: .*rendering compute attractor particles"
    )
endif()
