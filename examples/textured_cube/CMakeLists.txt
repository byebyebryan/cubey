add_executable(cubey_example_textured_cube
    main.cpp
    textured_cube_app.cpp
)
set_target_properties(cubey_example_textured_cube PROPERTIES OUTPUT_NAME textured_cube)

target_compile_definitions(
    cubey_example_textured_cube
    PRIVATE
        CUBEY_TEXTURED_CUBE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_textured_cube
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
)

cubey_add_glsl_shaders(
    cubey_example_textured_cube
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/cubey/lighting.glsl"
    SOURCES
        shaders/textured_cube.vert
        shaders/textured_cube.frag
        shaders/textured_cube.comp
)

if (BUILD_TESTING)
    cubey_add_windowed_smoke_test(
        textured_cube_reaches_glfw
        cubey_example_textured_cube
        "textured_cube: .*rendering interactive compute shaded textured cube"
    )
endif()
