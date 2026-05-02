add_executable(cubey_example_spinning_cube
    main.cpp
    spinning_cube_app.cpp
)
set_target_properties(cubey_example_spinning_cube PROPERTIES OUTPUT_NAME spinning_cube)

target_compile_definitions(
    cubey_example_spinning_cube
    PRIVATE
        GLFW_INCLUDE_NONE
        CUBEY_SPINNING_CUBE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_spinning_cube
    PRIVATE
        cubey::cubey
        cubey_project_warnings
        glfw
)

cubey_add_glsl_shaders(
    cubey_example_spinning_cube
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
        shaders/spinning_cube.vert
        shaders/spinning_cube.frag
)

if (BUILD_TESTING)
    add_test(
        NAME spinning_cube_reaches_glfw
        COMMAND
            /bin/sh -c
            "out=$($<TARGET_FILE:cubey_example_spinning_cube> --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -eq 0 ]; then printf '%s\n' \"$out\" | grep -q 'spinning_cube: .*rendering indexed cube'; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed\\|GLFW reports Vulkan is not supported'; fi"
    )
    set_tests_properties(spinning_cube_reaches_glfw PROPERTIES TIMEOUT 10)
endif()
