add_executable(cubey_example_triangle
    main.cpp
    triangle_app.cpp
)
set_target_properties(cubey_example_triangle PROPERTIES OUTPUT_NAME triangle)

target_compile_definitions(
    cubey_example_triangle
    PRIVATE
        GLFW_INCLUDE_NONE
        CUBEY_TRIANGLE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_triangle
    PRIVATE
        cubey::cubey
        cubey_project_warnings
        glfw
)

cubey_add_glsl_shaders(
    cubey_example_triangle
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
        shaders/triangle.vert
        shaders/triangle.frag
)

if (BUILD_TESTING)
    add_test(
        NAME triangle_reaches_glfw
        COMMAND
            /bin/sh -c
            "out=$($<TARGET_FILE:cubey_example_triangle> --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -eq 0 ]; then printf '%s\n' \"$out\" | grep -q 'triangle: .*rendering dynamic triangle'; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed\\|GLFW reports Vulkan is not supported'; fi"
    )
    set_tests_properties(triangle_reaches_glfw PROPERTIES TIMEOUT 10)
endif()
