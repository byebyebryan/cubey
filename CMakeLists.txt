cmake_minimum_required(VERSION 3.21)
project(cubey LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(CTest)

find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VULKAN REQUIRED vulkan)

FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.4.341
)
FetchContent_MakeAvailable(vulkan_headers)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

set(CUBEY_COMP_SHADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/shaders/headless.comp.spv")
set(CUBEY_VERT_SHADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/shaders/fullscreen.vert.spv")
set(CUBEY_FRAG_SHADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/shaders/sample.frag.spv")
configure_file(src/config.h.in generated/config.h @ONLY)

add_custom_command(
    OUTPUT "${CUBEY_COMP_SHADER_PATH}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/headless.comp" -o "${CUBEY_COMP_SHADER_PATH}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/headless.comp"
    VERBATIM
)
add_custom_command(
    OUTPUT "${CUBEY_VERT_SHADER_PATH}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert" -o "${CUBEY_VERT_SHADER_PATH}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert"
    VERBATIM
)
add_custom_command(
    OUTPUT "${CUBEY_FRAG_SHADER_PATH}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sample.frag" -o "${CUBEY_FRAG_SHADER_PATH}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sample.frag"
    VERBATIM
)
add_custom_target(cubey_shaders DEPENDS
    "${CUBEY_COMP_SHADER_PATH}"
    "${CUBEY_VERT_SHADER_PATH}"
    "${CUBEY_FRAG_SHADER_PATH}"
)

add_executable(cubey src/main.cpp)
add_dependencies(cubey cubey_shaders)
target_include_directories(cubey PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${vulkan_headers_SOURCE_DIR}/include"
)
target_compile_definitions(cubey PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(cubey PRIVATE vulkan glfw)

if (BUILD_TESTING)
    add_test(
        NAME headless_vulkan_graphics_pipeline
        COMMAND cubey --headless --width 128 --height 96 --frames 1 --output "${CMAKE_CURRENT_BINARY_DIR}/headless.ppm"
    )
    set_tests_properties(headless_vulkan_graphics_pipeline PROPERTIES
        PASS_REGULAR_EXPRESSION "graphics verification passed"
    )

    add_test(
        NAME window_mode_reaches_glfw
        COMMAND /bin/sh -c "out=$($<TARGET_FILE:cubey> --frames 1 --width 64 --height 64 2>&1); status=$?; printf '%s\n' \"$out\"; if [ $status -eq 0 ]; then printf '%s\n' \"$out\" | grep -q 'window mode:'; else printf '%s\n' \"$out\" | grep -q 'glfwInit failed'; fi"
    )
    set_tests_properties(window_mode_reaches_glfw PROPERTIES
        TIMEOUT 10
    )
endif()
