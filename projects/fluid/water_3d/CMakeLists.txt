cubey_add_app_target(
    cubey_project_water_3d
    OUTPUT_NAME water_3d
    SHADER_DEFINE CUBEY_WATER_3D_SHADER_DIR
    WINDOWED_SMOKE_TEST water_3d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "water_3d: .*rendering 3D water project"
    WINDOWED_SMOKE_ARGS
        --grid-width 32
        --grid-height 32
        --grid-depth 32
    SOURCES
        ../sim/water_3d/water_3d_app.cpp
        ../sim/water_3d/water_3d_commands.cpp
        ../sim/water_3d/water_3d_gpu_resources.cpp
        main.cpp
    SHADERS
        ../sim/water_3d/shaders/water_3d.vert
        ../sim/water_3d/shaders/water_3d_render.frag
        ../sim/water_3d/shaders/water_3d_fullscreen.vert
        ../sim/water_3d/shaders/water_3d_scene.frag
        ../sim/water_3d/shaders/water_3d_whitewater.vert
        ../sim/water_3d/shaders/water_3d_whitewater.frag
        ../sim/water_3d/shaders/water_3d_surface_particle.vert
        ../sim/water_3d/shaders/water_3d_surface_depth.frag
        ../sim/water_3d/shaders/water_3d_surface_thickness.frag
        ../sim/water_3d/shaders/water_3d_surface_pack.frag
        ../sim/water_3d/shaders/water_3d_surface_repair.frag
        ../sim/water_3d/shaders/water_3d_surface_smooth.frag
        ../sim/water_3d/shaders/water_3d_surface_composite.frag
        ../sim/water_3d/shaders/water_3d_reset.comp
        ../sim/water_3d/shaders/water_3d_clear_grid.comp
        ../sim/water_3d/shaders/water_3d_clear_bins.comp
        ../sim/water_3d/shaders/water_3d_build_bins.comp
        ../sim/water_3d/shaders/water_3d_active_face_dispatch_args.comp
        ../sim/water_3d/shaders/water_3d_copy_particle_sort_source.comp
        ../sim/water_3d/shaders/water_3d_scan_offsets.comp
        ../sim/water_3d/shaders/water_3d_scan_add_offsets.comp
        ../sim/water_3d/shaders/water_3d_scatter_sorted_particles.comp
        ../sim/water_3d/shaders/water_3d_particle_to_grid.comp
        ../sim/water_3d/shaders/water_3d_force.comp
        ../sim/water_3d/shaders/water_3d_divergence.comp
        ../sim/water_3d/shaders/water_3d_pressure.comp
        ../sim/water_3d/shaders/water_3d_projection.comp
        ../sim/water_3d/shaders/water_3d_extrapolate_velocity.comp
        ../sim/water_3d/shaders/water_3d_grid_to_particle.comp
        ../sim/water_3d/shaders/water_3d_advect_particles.comp
        ../sim/water_3d/shaders/water_3d_whitewater_clear.comp
        ../sim/water_3d/shaders/water_3d_whitewater_advect.comp
        ../sim/water_3d/shaders/water_3d_whitewater_emit.comp
        ../sim/water_3d/shaders/water_3d_whitewater_active_indices.comp
        ../sim/water_3d/shaders/water_3d_whitewater_draw_args.comp
        ../sim/water_3d/shaders/water_3d_diagnostics.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_3d/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_3d/shaders/water_3d_contract.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_3d/shaders/water_3d_surface_common.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_3d/shaders/water_3d_surface_helpers.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
)

if (CUBEY_HDR_SAMPLE_ASSETS_DIR)
    target_compile_definitions(
        cubey_project_water_3d
        PRIVATE
            CUBEY_HDR_SAMPLE_ASSETS_DIR="${CUBEY_HDR_SAMPLE_ASSETS_DIR}"
    )
endif()

if (BUILD_TESTING)
    add_executable(cubey_project_water_3d_tests
        ../sim/water_3d/water_3d_config_tests.cpp
    )
    target_include_directories(
        cubey_project_water_3d_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_3d"
    )
    target_compile_definitions(cubey_project_water_3d_tests
        PRIVATE
            CUBEY_WATER_3D_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../sim/water_3d"
    )
    target_link_libraries(
        cubey_project_water_3d_tests
        PRIVATE
            cubey::cubey
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME water_3d_config_tests COMMAND cubey_project_water_3d_tests)

    set(CUBEY_WATER_3D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/water-3d-headless.png"
    )
    cubey_add_png_smoke_test(
        water_3d_headless_writes_png
        cubey_project_water_3d
        "${CUBEY_WATER_3D_HEADLESS_SMOKE_OUTPUT}"
        --headless
        --frames 4
        --grid-width 32
        --grid-height 32
        --grid-depth 32
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_WATER_3D_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/water-3d-headless.mp4"
        )
        cubey_add_video_smoke_test(
            water_3d_headless_writes_video
            cubey_project_water_3d
            "${CUBEY_WATER_3D_HEADLESS_VIDEO_SMOKE_OUTPUT}"
            --grid-width 32
            --grid-height 32
            --grid-depth 32
        )
    endif()
endif()
