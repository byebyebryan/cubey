cubey_add_app_target(
    cubey_project_fire_3d
    OUTPUT_NAME fire_3d
    SHADER_DEFINE CUBEY_PYRO_3D_SHADER_DIR
    WINDOWED_SMOKE_TEST fire_3d_reaches_glfw
    WINDOWED_SMOKE_PATTERN "fire_3d: .*rendering 3D fire project"
    SOURCES
        ${CUBEY_PYRO_3D_SOURCES}
        main.cpp
    SHADERS
        ${CUBEY_PYRO_3D_SHADERS}
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/common/shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/../sim/common/shaders/fluid_ray_box.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/environment_lighting.glsl"
        ${CUBEY_PYRO_3D_TERRAIN_SHADER_DEPENDS}
)

if (BUILD_TESTING)
    set(
        CUBEY_FIRE_3D_TERRAIN_SMOKE_ASSET
        "${CMAKE_SOURCE_DIR}/tests/assets/terrain/backdrop-smoke"
    )
    set(CUBEY_FIRE_3D_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fire-3d-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        fire_3d_headless_writes_png
        cubey_project_fire_3d
        "${CUBEY_FIRE_3D_HEADLESS_SMOKE_OUTPUT}"
        --frames 4
        --grid-width 32
        --grid-height 32
        --grid-depth 32
    )
    set(CUBEY_FIRE_3D_TERRAIN_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fire-3d-terrain-headless-smoke.png"
    )
    cubey_add_png_smoke_test(
        fire_3d_terrain_headless_writes_png
        cubey_project_fire_3d
        "${CUBEY_FIRE_3D_TERRAIN_SMOKE_OUTPUT}"
        --frames 4
        --grid-width 32
        --grid-height 32
        --grid-depth 32
        --terrain-heightfield
        "${CUBEY_FIRE_3D_TERRAIN_SMOKE_ASSET}"
    )
    cubey_add_png_stats_test(
        fire_3d_terrain_headless_writes_png
        "${CUBEY_FIRE_3D_TERRAIN_SMOKE_OUTPUT}"
        0.02
        0.05
    )
    if (CUBEY_HAS_LIBAV)
        set(CUBEY_FIRE_3D_HEADLESS_VIDEO_SMOKE_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/fire-3d-headless-smoke.mp4"
        )
        cubey_add_video_smoke_test(
            fire_3d_headless_writes_video
            cubey_project_fire_3d
            "${CUBEY_FIRE_3D_HEADLESS_VIDEO_SMOKE_OUTPUT}"
            --grid-width 24
            --grid-height 24
            --grid-depth 24
        )
    endif()
endif()
