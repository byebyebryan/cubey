cubey_add_app_target(
    cubey_example_material_cubes
    OUTPUT_NAME material_cubes
    SHADER_DEFINE CUBEY_MATERIAL_CUBES_SHADER_DIR
    WINDOWED_SMOKE_TEST material_cubes_reaches_glfw
    WINDOWED_SMOKE_PATTERN "material_cubes: .*rendering material instance cubes"
    SOURCES
        main.cpp
        material_cubes_app.cpp
    SHADERS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/gltf_pbr.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/gltf_pbr.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/gltf_skybox.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/gltf_skybox.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/pbr_post.frag"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/gltf_shadow_depth.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/gltf_shadow_depth.frag"
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
)
