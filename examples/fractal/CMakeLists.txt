add_executable(cubey_example_fractal
    fractal_app.cpp
    main.cpp
)
set_target_properties(cubey_example_fractal PROPERTIES OUTPUT_NAME fractal)

target_compile_definitions(
    cubey_example_fractal
    PRIVATE
        GLFW_INCLUDE_NONE
        CUBEY_FRACTAL_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_fractal
    PRIVATE
        cubey::cubey
        cubey_project_warnings
        glfw
)

cubey_add_glsl_shaders(
    cubey_example_fractal
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    SOURCES
        shaders/fractal.vert
        shaders/fractal.frag
)

if (BUILD_TESTING)
    add_test(
        NAME fractal_reaches_glfw
        COMMAND
            /bin/sh -c
            "out=$($<TARGET_FILE:cubey_example_fractal> --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -eq 0 ]; then printf '%s\n' \"$out\" | grep -q 'fractal: .*rendering fullscreen fractal'; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed\\|GLFW reports Vulkan is not supported'; fi"
    )
    set_tests_properties(fractal_reaches_glfw PROPERTIES TIMEOUT 10)
endif()
