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
    SHADERS
        shaders/fullscreen.vert
        shaders/mountains_raymarch.frag
    SHADER_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CUBEY_SHADERTOY_REF_DIR}"
    SHADER_DEPENDS
        "${CUBEY_SHADERTOY_MOUNTAINS_SOURCE}"
)
