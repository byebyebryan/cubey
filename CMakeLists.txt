cmake_minimum_required(VERSION 3.21)

project(cubey VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CUBEY_ENABLE_WARNINGS "Enable project warning flags" ON)
option(CUBEY_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(CUBEY_ENABLE_CLANG_TIDY "Run clang-tidy during builds" OFF)
option(CUBEY_BUILD_EXAMPLES "Build Cubey examples" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CubeyShaders)
include(CubeyWarnings)

if (CUBEY_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

add_library(cubey_project_options INTERFACE)
target_compile_features(cubey_project_options INTERFACE cxx_std_20)

add_library(cubey_project_warnings INTERFACE)
cubey_configure_warnings(cubey_project_warnings)

include(FetchContent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VULKAN_LOADER REQUIRED IMPORTED_TARGET vulkan)

FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.4.341
)
FetchContent_MakeAvailable(vulkan_headers)

include(CTest)

add_subdirectory(src/cubey)

if (BUILD_TESTING)
    add_subdirectory(tests/cubey)
endif()

if (CUBEY_BUILD_EXAMPLES)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw
        GIT_TAG        3.4
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    add_subdirectory(examples/window_clear)
    add_subdirectory(examples/triangle)
    add_subdirectory(examples/spinning_cube)
    add_subdirectory(examples/textured_cube)

    if (BUILD_TESTING)
        set(CUBEY_CLASSIC_RENDERING_PATTERN
            "vkCreateRenderPass|vkDestroyRenderPass|VkRenderPass|vkCreateFramebuffer|vkDestroyFramebuffer|VkFramebuffer|vkCmdBeginRenderPass|vkCmdEndRenderPass|VkRenderPassBeginInfo"
        )
        add_test(
            NAME shader_backed_examples_use_dynamic_rendering
            COMMAND
                /bin/sh -c
                "grep -RE \"$1\" \"$2\" \"$3\" \"$4\"; status=$?; test \"$status\" -eq 1"
                shader_backed_examples_use_dynamic_rendering
                "${CUBEY_CLASSIC_RENDERING_PATTERN}"
                "${CMAKE_SOURCE_DIR}/examples/triangle"
                "${CMAKE_SOURCE_DIR}/examples/spinning_cube"
                "${CMAKE_SOURCE_DIR}/examples/textured_cube"
        )
    endif()
endif()
