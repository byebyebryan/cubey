add_executable(cubey_example_triangle
    main.cpp
    triangle_app.cpp
)
set_target_properties(cubey_example_triangle PROPERTIES OUTPUT_NAME triangle)

target_compile_definitions(
    cubey_example_triangle
    PRIVATE
        CUBEY_TRIANGLE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_triangle
    PRIVATE
        cubey::cubey
        cubey::host
        cubey_project_warnings
)

cubey_add_glsl_shaders(
    cubey_example_triangle
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
        shaders/triangle.vert
        shaders/triangle.frag
)

if (BUILD_TESTING)
    cubey_add_windowed_smoke_test(
        triangle_reaches_glfw
        cubey_example_triangle
        "triangle: .*rendering dynamic triangle"
    )
endif()
