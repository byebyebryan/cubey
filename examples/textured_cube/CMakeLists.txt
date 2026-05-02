add_executable(cubey_example_textured_cube
    main.cpp
    textured_cube_app.cpp
)
set_target_properties(cubey_example_textured_cube PROPERTIES OUTPUT_NAME textured_cube)

target_compile_definitions(
    cubey_example_textured_cube
    PRIVATE
        GLFW_INCLUDE_NONE
        CUBEY_TEXTURED_CUBE_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    cubey_example_textured_cube
    PRIVATE
        cubey::cubey
        cubey_project_warnings
        glfw
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
    add_test(
        NAME textured_cube_reaches_glfw
        COMMAND
            /bin/sh -c
            "out=$($<TARGET_FILE:cubey_example_textured_cube> --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -eq 0 ]; then printf '%s\n' \"$out\" | grep -q 'textured_cube: .*rendering interactive compute shaded textured cube'; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed\\|GLFW reports Vulkan is not supported'; fi"
    )
    set_tests_properties(textured_cube_reaches_glfw PROPERTIES TIMEOUT 10)
endif()
