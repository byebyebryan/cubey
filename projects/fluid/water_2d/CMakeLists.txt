cubey_add_app_target(
    cubey_project_water_2d
    OUTPUT_NAME water_2d
    SHADER_DEFINE CUBEY_WATER_2D_SHADER_DIR
    WINDOWED_SMOKE_TEST water_2d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "water_2d: .*rendering 2D water project"
    SOURCES
        ../sim/water_2d/water_2d_app.cpp
        ../sim/water_2d/water_2d_commands.cpp
        ../sim/water_2d/water_2d_diagnostics.cpp
        ../sim/water_2d/water_2d_gpu_resources.cpp
        ../sim/water_2d/water_2d_ui.cpp
        main.cpp
    SHADERS
        ../sim/water_2d/shaders/water_2d.vert
        ../sim/water_2d/shaders/water_2d_render.frag
        ../sim/water_2d/shaders/water_2d_surface_density.vert
        ../sim/water_2d/shaders/water_2d_surface_density.frag
        ../sim/water_2d/shaders/water_2d_surface_smooth.frag
        ../sim/water_2d/shaders/water_2d_surface_composite.frag
        ../sim/water_2d/shaders/water_2d_reset.comp
        ../sim/water_2d/shaders/water_2d_clear_grid.comp
        ../sim/water_2d/shaders/water_2d_clear_bins.comp
        ../sim/water_2d/shaders/water_2d_build_bins.comp
        ../sim/water_2d/shaders/water_2d_emit_particles.comp
        ../sim/water_2d/shaders/water_2d_particle_to_grid.comp
        ../sim/water_2d/shaders/water_2d_force.comp
        ../sim/water_2d/shaders/water_2d_divergence.comp
        ../sim/water_2d/shaders/water_2d_pressure.comp
        ../sim/water_2d/shaders/water_2d_projection.comp
        ../sim/water_2d/shaders/water_2d_grid_to_particle.comp
        ../sim/water_2d/shaders/water_2d_advect_particles.comp
        ../sim/water_2d/shaders/water_2d_diagnostics.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_2d/shaders"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/common/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_2d/shaders/water_2d_contract.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/common/shaders/water_2d_grid.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/noise.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/random.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/procedural/operators.glsl"
)

target_link_libraries(
    cubey_project_water_2d
    PRIVATE
        cubey_project_fluid_common
)

if (BUILD_TESTING)
    add_executable(cubey_project_water_2d_tests
        ../sim/water_2d/water_2d_config_tests.cpp
    )
    target_include_directories(
        cubey_project_water_2d_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_2d"
    )
    target_compile_definitions(cubey_project_water_2d_tests
        PRIVATE
            CUBEY_WATER_2D_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_2d"
    )
    target_link_libraries(
        cubey_project_water_2d_tests
        PRIVATE
            cubey::cubey
            cubey_project_fluid_common
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME water_2d_config_tests COMMAND cubey_project_water_2d_tests)

    set(CUBEY_WATER_2D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/water-2d-headless.png"
    )
    cubey_add_png_smoke_test(
        water_2d_headless_writes_png
        cubey_project_water_2d
        "${CUBEY_WATER_2D_HEADLESS_SMOKE_OUTPUT}"
        --frames 8
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_WATER_2D_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/water-2d-headless.mp4"
        )
        cubey_add_video_smoke_test(
            water_2d_headless_writes_video
            cubey_project_water_2d
            "${CUBEY_WATER_2D_HEADLESS_VIDEO_SMOKE_OUTPUT}"
        )
    endif()
endif()
