add_executable(cubey_example_window_clear
    main.cpp
    window_clear_app.cpp
)
set_target_properties(cubey_example_window_clear PROPERTIES OUTPUT_NAME window_clear)

target_include_directories(
    cubey_example_window_clear
    PRIVATE
        "${vulkan_headers_SOURCE_DIR}/include"
)

target_compile_definitions(cubey_example_window_clear PRIVATE GLFW_INCLUDE_NONE)

target_link_libraries(
    cubey_example_window_clear
    PRIVATE
        cubey::cubey
        cubey_project_warnings
        glfw
        PkgConfig::VULKAN_LOADER
)

if (BUILD_TESTING)
    add_test(
        NAME window_clear_reaches_glfw
        COMMAND
            /bin/sh -c
            "out=$($<TARGET_FILE:cubey_example_window_clear> --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -eq 0 ]; then printf '%s\n' \"$out\" | grep -q 'window_clear:'; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed\\|GLFW reports Vulkan is not supported'; fi"
    )
    set_tests_properties(window_clear_reaches_glfw PROPERTIES TIMEOUT 10)
endif()
