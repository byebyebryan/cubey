cmake_minimum_required(VERSION 3.21)

project(cubey VERSION 0.1.0 LANGUAGES C CXX)

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
    CUBEY_BUILD_TERRAIN_STUDIES
    "Build optional terrain reference and hydrology studies"
    OFF
)
set(
    CUBEY_VIDEO_CAPTURE
    "AUTO"
    CACHE STRING "Enable in-process video capture through libav/FFmpeg: AUTO, ON, or OFF"
)
set_property(CACHE CUBEY_VIDEO_CAPTURE PROPERTY STRINGS AUTO ON OFF)
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
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Vulkan REQUIRED)

string(TOUPPER "${CUBEY_VIDEO_CAPTURE}" CUBEY_VIDEO_CAPTURE_MODE)
if (NOT CUBEY_VIDEO_CAPTURE_MODE MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "CUBEY_VIDEO_CAPTURE must be AUTO, ON, or OFF")
endif()

set(CUBEY_HAS_LIBAV OFF)
if (NOT CUBEY_VIDEO_CAPTURE_MODE STREQUAL "OFF")
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(
            CUBEY_LIBAV
            QUIET
            IMPORTED_TARGET
            libavcodec
            libavformat
            libavutil
            libswscale
        )
    endif()

    if (CUBEY_LIBAV_FOUND)
        set(CUBEY_HAS_LIBAV ON)
        message(STATUS "Cubey video capture: enabled with libav/FFmpeg")
    elseif (CUBEY_VIDEO_CAPTURE_MODE STREQUAL "ON")
        message(FATAL_ERROR "CUBEY_VIDEO_CAPTURE=ON requires libavcodec, libavformat, libavutil, and libswscale")
    else()
        message(STATUS "Cubey video capture: disabled, libav/FFmpeg development packages not found")
    endif()
endif()

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

FetchContent_Declare(
    fastnoise_lite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG        v1.1.1
)
FetchContent_GetProperties(fastnoise_lite)
if (NOT fastnoise_lite_POPULATED)
    cmake_policy(PUSH)
    if (POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(fastnoise_lite)
    cmake_policy(POP)
endif()
add_library(cubey_external_fastnoise_lite INTERFACE)
target_include_directories(
    cubey_external_fastnoise_lite
    SYSTEM
    INTERFACE
        "${fastnoise_lite_SOURCE_DIR}/Cpp"
)
set(
    CUBEY_FASTNOISE_LITE_GLSL_DIR
    "${fastnoise_lite_SOURCE_DIR}/GLSL"
    CACHE PATH "FastNoiseLite GLSL include directory for future shader-side procedural noise"
    FORCE
)

FetchContent_Declare(
    basis_universal
    GIT_REPOSITORY https://github.com/BinomialLLC/basis_universal.git
    GIT_TAG        v2_1_0
)
FetchContent_GetProperties(basis_universal)
if (NOT basis_universal_POPULATED)
    cmake_policy(PUSH)
    if (POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(basis_universal)
    cmake_policy(POP)
endif()
add_library(cubey_external_basisu_transcoder STATIC
    "${basis_universal_SOURCE_DIR}/transcoder/basisu_transcoder.cpp"
    "${basis_universal_SOURCE_DIR}/zstd/zstd.c"
)
target_include_directories(
    cubey_external_basisu_transcoder
    SYSTEM
    PUBLIC
        "${basis_universal_SOURCE_DIR}/transcoder"
)
target_compile_definitions(
    cubey_external_basisu_transcoder
    PRIVATE
        BASISD_SUPPORT_KTX2=1
        BASISD_SUPPORT_KTX2_ZSTD=1
)
target_compile_options(
    cubey_external_basisu_transcoder
    PRIVATE
        "$<$<CXX_COMPILER_ID:Clang,GNU>:-w>"
)
set_target_properties(cubey_external_basisu_transcoder PROPERTIES CXX_CLANG_TIDY "")

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

set(LAZY_SERIALIZABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_MULTI OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_LAZY_JSON OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_TEXT OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_RAPID_JSON OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_NLOHMANN_JSON ON CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_YAML OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_TOML OFF CACHE BOOL "" FORCE)
set(LAZY_SERIALIZABLE_ENABLE_SINGLE_HEADER_TARGET OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    lazy_serializable
    GIT_REPOSITORY https://github.com/byebyebryan/lazy-serializable.git
    GIT_TAG        5137bc5c2665f7962fc1a8385b6e2872112fc55c
)
FetchContent_MakeAvailable(lazy_serializable)

if (NOT DEFINED BUILD_TESTING)
    set(BUILD_TESTING ${CUBEY_BUILD_HOST} CACHE BOOL "Build tests")
endif()
include(CTest)

if (BUILD_TESTING)
    add_executable(cubey_png_stats
        tests/tools/png_stats.cpp
    )
    target_link_libraries(
        cubey_png_stats
        PRIVATE
            cubey_external_stb_image
            cubey_project_options
            cubey_project_warnings
    )
    set_target_properties(cubey_png_stats PROPERTIES CXX_CLANG_TIDY "")
endif()

add_subdirectory(src/cubey)

if (CUBEY_BUILD_EXAMPLES AND NOT CUBEY_BUILD_HOST)
    message(FATAL_ERROR "CUBEY_BUILD_EXAMPLES requires CUBEY_BUILD_HOST")
endif()

if (CUBEY_BUILD_PROJECTS AND NOT CUBEY_BUILD_HOST)
    message(FATAL_ERROR "CUBEY_BUILD_PROJECTS requires CUBEY_BUILD_HOST")
endif()

if (CUBEY_BUILD_TERRAIN_STUDIES AND NOT CUBEY_BUILD_HOST)
    message(FATAL_ERROR "CUBEY_BUILD_TERRAIN_STUDIES requires CUBEY_BUILD_HOST")
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

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.8
    )
    FetchContent_GetProperties(imgui)
    if (NOT imgui_POPULATED)
        cmake_policy(PUSH)
        if (POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(imgui)
        cmake_policy(POP)
    endif()
    add_library(cubey_external_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    )
    target_include_directories(
        cubey_external_imgui
        SYSTEM
        PUBLIC
            "${imgui_SOURCE_DIR}"
            "${imgui_SOURCE_DIR}/backends"
    )
    target_compile_definitions(
        cubey_external_imgui
        PRIVATE
            GLFW_INCLUDE_NONE
    )
    target_compile_options(
        cubey_external_imgui
        PRIVATE
            "$<$<CXX_COMPILER_ID:Clang,GNU>:-w>"
    )
    target_link_libraries(cubey_external_imgui PUBLIC glfw Vulkan::Vulkan)
    set_target_properties(cubey_external_imgui PROPERTIES CXX_CLANG_TIDY "")

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
endif()

if (BUILD_TESTING AND (CUBEY_BUILD_EXAMPLES OR CUBEY_BUILD_PROJECTS))
    set(CUBEY_CLASSIC_RENDERING_PATTERN
        "vkCreateRenderPass|vkDestroyRenderPass|VkRenderPass|vkCreateFramebuffer|vkDestroyFramebuffer|VkFramebuffer|vkCmdBeginRenderPass|vkCmdEndRenderPass|VkRenderPassBeginInfo"
    )
    set(CUBEY_DYNAMIC_RENDERING_SCAN_DIRS)
    if (CUBEY_BUILD_EXAMPLES)
        list(
            APPEND
            CUBEY_DYNAMIC_RENDERING_SCAN_DIRS
                "${CMAKE_SOURCE_DIR}/examples/spinning_cube"
                "${CMAKE_SOURCE_DIR}/examples/textured_cube"
                "${CMAKE_SOURCE_DIR}/examples/shadow_cube"
                "${CMAKE_SOURCE_DIR}/examples/instanced_cubes"
                "${CMAKE_SOURCE_DIR}/examples/material_cubes"
                "${CMAKE_SOURCE_DIR}/examples/headless_cube"
                "${CMAKE_SOURCE_DIR}/examples/particle_cubes"
        )
    endif()
    if (CUBEY_BUILD_PROJECTS)
        list(
            APPEND
            CUBEY_DYNAMIC_RENDERING_SCAN_DIRS
                "${CMAKE_SOURCE_DIR}/projects/atmosphere"
                "${CMAKE_SOURCE_DIR}/projects/cloud_ref"
                "${CMAKE_SOURCE_DIR}/projects/cloud_ref_2"
                "${CMAKE_SOURCE_DIR}/projects/clouds_legacy"
                "${CMAKE_SOURCE_DIR}/projects/fractal_2d"
                "${CMAKE_SOURCE_DIR}/projects/fluid"
                "${CMAKE_SOURCE_DIR}/projects/gltf_viewer"
                "${CMAKE_SOURCE_DIR}/projects/ocean"
                "${CMAKE_SOURCE_DIR}/projects/pbr_furnace"
                "${CMAKE_SOURCE_DIR}/projects/planet"
                "${CMAKE_SOURCE_DIR}/projects/terrain"
        )
    endif()
    if (CUBEY_BUILD_TERRAIN_STUDIES)
        list(
            APPEND
            CUBEY_DYNAMIC_RENDERING_SCAN_DIRS
                "${CMAKE_SOURCE_DIR}/studies/terrain/reference"
                "${CMAKE_SOURCE_DIR}/studies/terrain/shadertoy"
                "${CMAKE_SOURCE_DIR}/studies/terrain/hydrology"
        )
    endif()
    add_test(
        NAME render_apps_use_dynamic_rendering
        COMMAND
            /bin/sh -c
            "pattern=\"$1\"; shift; grep -RE \"$pattern\" \"$@\"; status=$?; test \"$status\" -eq 1"
            render_apps_use_dynamic_rendering
            "${CUBEY_CLASSIC_RENDERING_PATTERN}"
            ${CUBEY_DYNAMIC_RENDERING_SCAN_DIRS}
    )
endif()

if (CUBEY_FETCH_GLTF_SAMPLE_ASSETS)
    FetchContent_Declare(
        gltf_sample_assets
        GIT_REPOSITORY https://github.com/KhronosGroup/glTF-Sample-Assets.git
        GIT_TAG        2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf
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
if (CUBEY_BUILD_TERRAIN_STUDIES)
    add_subdirectory(studies/terrain)
endif()
