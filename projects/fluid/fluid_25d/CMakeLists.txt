cubey_add_app_target(
    cubey_project_fluid_25d
    OUTPUT_NAME fluid_25d
    SHADER_DEFINE CUBEY_FLUID_25D_SHADER_DIR
    WINDOWED_SMOKE_TEST fluid_25d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fluid_25d: .*rendering River V0"
    SOURCES
        ../sim/fluid_25d/fluid_25d_app.cpp
        ../sim/fluid_25d/fluid_25d_commands.cpp
        ../sim/fluid_25d/fluid_25d_diagnostics.cpp
        ../sim/fluid_25d/fluid_25d_gpu_resources.cpp
        ../sim/fluid_25d/fluid_25d_oracle.cpp
        main.cpp
    SHADERS
        ../sim/fluid_25d/shaders/fluid_25d.vert
        ../sim/fluid_25d/shaders/fluid_25d_render.frag
        ../sim/fluid_25d/shaders/fluid_25d_terrain.vert
        ../sim/fluid_25d/shaders/fluid_25d_terrain.frag
        ../sim/fluid_25d/shaders/fluid_25d_water.vert
        ../sim/fluid_25d/shaders/fluid_25d_water.frag
        ../sim/fluid_25d/shaders/fluid_25d_reset.comp
        ../sim/fluid_25d/shaders/fluid_25d_flux.comp
        ../sim/fluid_25d/shaders/fluid_25d_depth.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)

target_include_directories(
    cubey_project_fluid_25d
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
)

if (BUILD_TESTING)
    add_executable(cubey_project_fluid_25d_tests
        ../sim/fluid_25d/fluid_25d_diagnostics.cpp
        ../sim/fluid_25d/fluid_25d_oracle.cpp
        ../sim/fluid_25d/fluid_25d_tests.cpp
    )
    target_include_directories(
        cubey_project_fluid_25d_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../sim/fluid_25d"
    )
    target_link_libraries(
        cubey_project_fluid_25d_tests
        PRIVATE
            cubey::cubey
            cubey::host
            cubey_project_fluid_common
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fluid_25d_tests COMMAND cubey_project_fluid_25d_tests)

    set(CUBEY_FLUID_25D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-headless.png"
    )
    cubey_add_png_smoke_test(
        fluid_25d_headless_writes_png
        cubey_project_fluid_25d
        "${CUBEY_FLUID_25D_HEADLESS_SMOKE_OUTPUT}"
        --frames 8
        --width 512
        --height 256
    )
    cubey_add_png_stats_test(
        fluid_25d_headless_writes_png
        "${CUBEY_FLUID_25D_HEADLESS_SMOKE_OUTPUT}"
        0.18
        0.12
    )
    set(CUBEY_FLUID_25D_DIAGNOSTIC_HEADLESS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-diagnostics-headless.png"
    )
    cubey_add_png_smoke_test(
        fluid_25d_diagnostics_writes_png
        cubey_project_fluid_25d
        "${CUBEY_FLUID_25D_DIAGNOSTIC_HEADLESS_OUTPUT}"
        --frames 8
        --width 512
        --height 256
        --fluid25d-view diagnostics
        --debug-view terrain
    )
    cubey_add_png_stats_test(
        fluid_25d_diagnostics_writes_png
        "${CUBEY_FLUID_25D_DIAGNOSTIC_HEADLESS_OUTPUT}"
        0.20
        0.20
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_lake_at_rest
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-lake-oracle.png"
        --frames 12
        --grid-width 12
        --grid-height 8
        --fluid25d-scenario lake-at-rest
        --fluid25d-gpu-oracle-validation
    )
    cubey_add_png_smoke_test(
        fluid_25d_gpu_oracle_river
        cubey_project_fluid_25d
        "${CMAKE_CURRENT_BINARY_DIR}/fluid-25d-river-oracle.png"
        --frames 24
        --grid-width 24
        --grid-height 9
        --fluid25d-scenario river-catchment
        --fluid25d-gpu-oracle-validation
    )
endif()
