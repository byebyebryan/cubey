set(
    CUBEY_SHADERTOY_REF_DIR
    "${CMAKE_SOURCE_DIR}/../ref/ShaderToy"
    CACHE PATH
    "Directory containing external ShaderToy reference sources"
)

set(CUBEY_SHADERTOY_MOUNTAINS_SOURCE "${CUBEY_SHADERTOY_REF_DIR}/mountains.glsl")
if (NOT EXISTS "${CUBEY_SHADERTOY_MOUNTAINS_SOURCE}")
    message(
        STATUS
        "Skipping terrain_shadertoy_ref: ${CUBEY_SHADERTOY_MOUNTAINS_SOURCE} was not found"
    )
    return()
endif()

cubey_add_app_target(
    cubey_project_terrain_shadertoy_ref
    OUTPUT_NAME terrain_shadertoy_ref
    SHADER_DEFINE CUBEY_TERRAIN_SHADERTOY_REF_SHADER_DIR
    SOURCES
        main.cpp
        terrain_shadertoy_ref_app.cpp
        terrain_shadertoy_ref_config.cpp
        terrain_shadertoy_ref_mesh.cpp
    SHADERS
        shaders/fullscreen.vert
        shaders/mountains_camera_probe.frag
        shaders/mountains_atlas_diagnostic.frag
        shaders/mountains_height_bake.frag
        shaders/mountains_mesh.frag
        shaders/mountains_mesh.vert
        shaders/mountains_raymarch.frag
        shaders/mountains_sky.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CUBEY_SHADERTOY_REF_DIR}"
    SHADER_DEPENDS
        "${CUBEY_SHADERTOY_MOUNTAINS_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/reference_frame.glsl"
)

if (BUILD_TESTING)
    add_executable(
        cubey_project_terrain_shadertoy_ref_tests
        terrain_shadertoy_ref_config.cpp
        terrain_shadertoy_ref_tests.cpp
    )
    target_include_directories(
        cubey_project_terrain_shadertoy_ref_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
        cubey_project_terrain_shadertoy_ref_tests
        PRIVATE
            cubey::core
            cubey_project_warnings
    )
    add_test(
        NAME terrain_shadertoy_ref_tests
        COMMAND cubey_project_terrain_shadertoy_ref_tests
    )

    set(
        CUBEY_TERRAIN_SHADERTOY_REF_RAYMARCH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-raymarch-smoke.png"
    )
    cubey_add_png_smoke_test(
        terrain_shadertoy_ref_raymarch_writes_png
        cubey_project_terrain_shadertoy_ref
        "${CUBEY_TERRAIN_SHADERTOY_REF_RAYMARCH_SMOKE_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --reference-render
        raymarch
        --reference-time
        20
    )
    cubey_add_png_stats_test(
        terrain_shadertoy_ref_raymarch_writes_png
        "${CUBEY_TERRAIN_SHADERTOY_REF_RAYMARCH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(
        CUBEY_TERRAIN_SHADERTOY_REF_MESH_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-mesh-smoke.png"
    )
    cubey_add_png_smoke_test(
        terrain_shadertoy_ref_mesh_writes_png
        cubey_project_terrain_shadertoy_ref
        "${CUBEY_TERRAIN_SHADERTOY_REF_MESH_SMOKE_OUTPUT}"
        --headless
        --width
        320
        --height
        180
        --reference-render
        mesh
        --reference-time
        20
        --reference-mesh-cells
        256
    )
    cubey_add_png_stats_test(
        terrain_shadertoy_ref_mesh_writes_png
        "${CUBEY_TERRAIN_SHADERTOY_REF_MESH_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    set(
        CUBEY_TERRAIN_SHADERTOY_REF_HEIGHT_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/terrain-shadertoy-ref-height-smoke.png"
    )
    cubey_add_png_smoke_test(
        terrain_shadertoy_ref_height_writes_png
        cubey_project_terrain_shadertoy_ref
        "${CUBEY_TERRAIN_SHADERTOY_REF_HEIGHT_SMOKE_OUTPUT}"
        --headless
        --width
        256
        --height
        256
        --reference-render
        mesh
        --reference-time
        20
        --reference-mesh-cells
        256
        --reference-mesh-surface
        terrain
        --reference-diagnostic
        height
    )
    cubey_add_png_stats_test(
        terrain_shadertoy_ref_height_writes_png
        "${CUBEY_TERRAIN_SHADERTOY_REF_HEIGHT_SMOKE_OUTPUT}"
        0.00001
        0.00005
    )

    cubey_label_tests(
        reference
        terrain_shadertoy_ref_tests
        terrain_shadertoy_ref_raymarch_writes_png
        terrain_shadertoy_ref_raymarch_writes_png_stats
        terrain_shadertoy_ref_mesh_writes_png
        terrain_shadertoy_ref_mesh_writes_png_stats
        terrain_shadertoy_ref_height_writes_png
        terrain_shadertoy_ref_height_writes_png_stats
    )
endif()
