cubey_forward_pbr_shader_sources(CUBEY_FORWARD_PBR_SHADERS)

cubey_add_app_target(
    cubey_example_material_cubes
    OUTPUT_NAME material_cubes
    SHADER_DEFINE CUBEY_MATERIAL_CUBES_SHADER_DIR
    WINDOWED_SMOKE_TEST material_cubes_reaches_glfw
    WINDOWED_SMOKE_PATTERN "material_cubes: .*rendering material instance cubes"
    SOURCES
        main.cpp
        material_cubes_app.cpp
        material_cubes_render.cpp
        material_cubes_resources.cpp
        material_cubes_scene.cpp
    SHADERS
        ${CUBEY_FORWARD_PBR_SHADERS}
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/pbr.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/atmosphere.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
)
