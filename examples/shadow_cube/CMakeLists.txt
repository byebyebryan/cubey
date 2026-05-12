add_executable(cubey_example_shadow_cube
    main.cpp
    shadow_cube_app.cpp
)
set_target_properties(cubey_example_shadow_cube PROPERTIES OUTPUT_NAME shadow_cube)

target_compile_definitions(
    cubey_example_shadow_cube
    PRIVATE
        CUBEY_SHADOW_CUBE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_shadow_cube
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
)

cubey_add_glsl_shaders(
    cubey_example_shadow_cube
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
        shaders/shadow_depth.vert
        shaders/shadow_cube.vert
        shaders/shadow_cube.frag
)

if (BUILD_TESTING)
    cubey_add_windowed_smoke_test(
        shadow_cube_reaches_glfw
        cubey_example_shadow_cube
        "shadow_cube: .*rendering directional shadow cube"
    )
endif()
