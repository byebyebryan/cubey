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
    add_executable(cubey_example_fractal_tests
        fractal_view_tests.cpp
    )
    target_include_directories(cubey_example_fractal_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_example_fractal_tests
        PRIVATE
            cubey_project_options
            cubey_project_warnings
    )
    add_test(NAME fractal_view_tests COMMAND cubey_example_fractal_tests)

    add_test(
        NAME fractal_reaches_glfw
        COMMAND
            /bin/sh -c
            "out=$($<TARGET_FILE:cubey_example_fractal> --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -eq 0 ]; then printf '%s\n' \"$out\" | grep -q 'fractal: .*rendering fullscreen fractal'; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed\\|GLFW reports Vulkan is not supported'; fi"
    )
    set_tests_properties(fractal_reaches_glfw PROPERTIES TIMEOUT 10)

    set(CUBEY_FRACTAL_HEADLESS_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/fractal-headless-smoke.png"
    )
    add_test(
        NAME fractal_headless_writes_png
        COMMAND
            /bin/sh -c
            "rm -f \"$1\"; out=$(\"$2\" --headless --width 64 --height 64 --output \"$1\" 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -ne 0 ]; then printf '%s\n' \"$out\" | grep -Eq 'no Vulkan physical devices found|vkEnumeratePhysicalDevices|no Vulkan device with required queues and dynamic rendering found|vkCreateInstance'; exit; fi; test -s \"$1\"; sig=$(od -An -tx1 -N8 \"$1\" | tr -d ' \\n'); test \"$sig\" = \"89504e470d0a1a0a\""
            fractal_headless_writes_png
            "${CUBEY_FRACTAL_HEADLESS_SMOKE_OUTPUT}"
            "$<TARGET_FILE:cubey_example_fractal>"
    )
    set_tests_properties(fractal_headless_writes_png PROPERTIES TIMEOUT 10)
endif()
