cubey_add_app_target(
    cubey_project_fire_3d
    OUTPUT_NAME fire_3d
    SHADER_DEFINE CUBEY_PYRO_3D_SHADER_DIR
    WINDOWED_SMOKE_TEST fire_3d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fire_3d: .*rendering 3D fire project"
    SOURCES
        ../sim/pyro_3d/pyro_3d_app.cpp
        ../sim/pyro_3d/pyro_3d_commands.cpp
        ../sim/pyro_3d/pyro_3d_gpu_resources.cpp
        ../sim/pyro_3d/pyro_3d_sources.cpp
        main.cpp
    SHADERS
        ../sim/pyro_3d/shaders/pyro_3d.vert
        ../sim/pyro_3d/shaders/pyro_3d_raymarch.frag
        ../sim/pyro_3d/shaders/pyro_3d_reset.comp
        ../sim/pyro_3d/shaders/pyro_3d_advect.comp
        ../sim/pyro_3d/shaders/pyro_3d_advect_correct.comp
        ../sim/pyro_3d/shaders/pyro_3d_combust.comp
        ../sim/pyro_3d/shaders/pyro_3d_divergence.comp
        ../sim/pyro_3d/shaders/pyro_3d_pressure.comp
        ../sim/pyro_3d/shaders/pyro_3d_projection.comp
        ../sim/pyro_3d/shaders/pyro_3d_shadow.comp
)

if (BUILD_TESTING)
    set(CUBEY_FIRE_3D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fire-3d-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        fire_3d_headless_writes_png
        cubey_project_fire_3d
        "${CUBEY_FIRE_3D_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 4
        --grid-width 32
        --grid-height 32
        --grid-depth 32
    )
endif()
