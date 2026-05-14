cubey_add_app_target(
    cubey_example_textured_cube
    OUTPUT_NAME textured_cube
    SHADER_DEFINE CUBEY_TEXTURED_CUBE_SHADER_DIR
    WINDOWED_SMOKE_TEST textured_cube_reaches_glfw
    WINDOWED_SMOKE_PATTERN "textured_cube: .*rendering interactive compute shaded textured cube"
    SOURCES
        main.cpp
        textured_cube_app.cpp
    SHADERS
        shaders/textured_cube.vert
        shaders/textured_cube.frag
        shaders/textured_cube.comp
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/color_space.glsl"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/lighting.glsl"
)
