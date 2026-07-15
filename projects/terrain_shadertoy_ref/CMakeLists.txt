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
