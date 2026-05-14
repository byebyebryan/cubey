cmake_minimum_required(VERSION 3.21)

project(cubey VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CUBEY_ENABLE_WARNINGS "Enable project warning flags" ON)
option(CUBEY_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(CUBEY_ENABLE_CLANG_TIDY "Run clang-tidy during builds" OFF)
option(CUBEY_BUILD_HOST "Build Cubey concrete host layer" ON)
option(CUBEY_BUILD_EXAMPLES "Build Cubey examples" ${CUBEY_BUILD_HOST})
option(CUBEY_BUILD_PROJECTS "Build Cubey projects" ${CUBEY_BUILD_HOST})
option(
    CUBEY_FETCH_GLTF_SAMPLE_ASSETS
    "Fetch selected Khronos glTF sample assets into the build tree"
    OFF
)
option(
    CUBEY_FETCH_HDR_SAMPLE_ASSETS
    "Fetch selected Filament HDR environment samples into the build tree"
    OFF
)
set(
    CUBEY_GLTF_SAMPLE_ASSETS_DIR
    ""
    CACHE PATH "Existing Khronos glTF-Sample-Assets checkout"
)
set(
    CUBEY_HDR_SAMPLE_ASSETS_DIR
    ""
    CACHE PATH "Existing Cubey HDR sample asset directory"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CubeyApps)
include(CubeyHdrSamples)
include(CubeyShaders)
include(CubeySmokeTests)
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

FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
)
FetchContent_MakeAvailable(cgltf)
add_library(cubey_external_cgltf INTERFACE)
target_include_directories(
    cubey_external_cgltf
    SYSTEM
    INTERFACE
        "${cgltf_SOURCE_DIR}"
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
)
FetchContent_MakeAvailable(stb)
add_library(cubey_external_stb_image INTERFACE)
target_include_directories(
    cubey_external_stb_image
    SYSTEM
    INTERFACE
        "${stb_SOURCE_DIR}"
)

find_package(glm CONFIG QUIET)
if (NOT glm_FOUND)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.3
    )
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glm)
endif()

if (NOT DEFINED BUILD_TESTING)
    set(BUILD_TESTING ${CUBEY_BUILD_HOST} CACHE BOOL "Build tests")
endif()
include(CTest)

add_subdirectory(src/cubey)

if (CUBEY_BUILD_EXAMPLES AND NOT CUBEY_BUILD_HOST)
    message(FATAL_ERROR "CUBEY_BUILD_EXAMPLES requires CUBEY_BUILD_HOST")
endif()

if (CUBEY_BUILD_PROJECTS AND NOT CUBEY_BUILD_HOST)
    message(FATAL_ERROR "CUBEY_BUILD_PROJECTS requires CUBEY_BUILD_HOST")
endif()

if (BUILD_TESTING AND NOT CUBEY_BUILD_HOST)
    message(FATAL_ERROR "BUILD_TESTING requires CUBEY_BUILD_HOST")
endif()

if (CUBEY_BUILD_HOST)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw
        GIT_TAG        3.4
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    add_subdirectory(src/cubey/host)
endif()

if (BUILD_TESTING)
    add_subdirectory(tests/cubey)
endif()

if (CUBEY_BUILD_EXAMPLES)
    add_subdirectory(examples/spinning_cube)
    add_subdirectory(examples/textured_cube)
    add_subdirectory(examples/shadow_cube)
    add_subdirectory(examples/instanced_cubes)
    add_subdirectory(examples/material_cubes)
    add_subdirectory(examples/headless_cube)
    add_subdirectory(examples/particle_cubes)

    if (BUILD_TESTING)
        set(CUBEY_CLASSIC_RENDERING_PATTERN
            "vkCreateRenderPass|vkDestroyRenderPass|VkRenderPass|vkCreateFramebuffer|vkDestroyFramebuffer|VkFramebuffer|vkCmdBeginRenderPass|vkCmdEndRenderPass|VkRenderPassBeginInfo"
        )
        add_test(
            NAME examples_use_dynamic_rendering
            COMMAND
                /bin/sh -c
                "pattern=\"$1\"; shift; grep -RE \"$pattern\" \"$@\"; status=$?; test \"$status\" -eq 1"
                examples_use_dynamic_rendering
                "${CUBEY_CLASSIC_RENDERING_PATTERN}"
                "${CMAKE_SOURCE_DIR}/examples/spinning_cube"
                "${CMAKE_SOURCE_DIR}/examples/textured_cube"
                "${CMAKE_SOURCE_DIR}/examples/shadow_cube"
                "${CMAKE_SOURCE_DIR}/examples/instanced_cubes"
                "${CMAKE_SOURCE_DIR}/examples/material_cubes"
                "${CMAKE_SOURCE_DIR}/examples/headless_cube"
                "${CMAKE_SOURCE_DIR}/examples/particle_cubes"
                "${CMAKE_SOURCE_DIR}/projects/fractal_2d"
                "${CMAKE_SOURCE_DIR}/projects/pbr_furnace"
        )
    endif()
endif()

if (CUBEY_FETCH_GLTF_SAMPLE_ASSETS)
    FetchContent_Declare(
        gltf_sample_assets
        GIT_REPOSITORY https://github.com/KhronosGroup/glTF-Sample-Assets.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(gltf_sample_assets)
    set(CUBEY_GLTF_SAMPLE_ASSETS_DIR "${gltf_sample_assets_SOURCE_DIR}" CACHE PATH "" FORCE)
endif()

if (CUBEY_FETCH_HDR_SAMPLE_ASSETS)
    cubey_fetch_hdr_sample_assets()
endif()

if (CUBEY_BUILD_PROJECTS)
    add_subdirectory(projects)
endif()
