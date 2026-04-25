cmake_minimum_required(VERSION 3.21)
project(cubey LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ── Dawn ─────────────────────────────────────────────────────────────────────
# DAWN_FETCH_DEPENDENCIES pulls abseil, SPIRV-Tools, Tint etc. via CMake
# instead of depot_tools/gclient. Slow first configure, but no extra tooling.
set(DAWN_FETCH_DEPENDENCIES  ON  CACHE BOOL "" FORCE)
set(DAWN_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_BENCHMARKS    OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_INSTALL      OFF CACHE BOOL "" FORCE)
set(DAWN_USE_GLFW            OFF CACHE BOOL "" FORCE)  # we bring our own
# Backends — Vulkan only on Linux
set(DAWN_ENABLE_VULKAN       ON  CACHE BOOL "" FORCE)
set(DAWN_ENABLE_METAL        OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_D3D11        OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_D3D12        OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_OPENGLES     OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_DESKTOP_GL   OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_NULL         OFF CACHE BOOL "" FORCE)
set(DAWN_USE_WAYLAND         ON  CACHE BOOL "" FORCE)
set(DAWN_USE_X11             ON  CACHE BOOL "" FORCE)
# Tint — shader compiler, keep but skip CLI tools
set(TINT_BUILD_TESTS         OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_CMD_TOOLS     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    dawn
    GIT_REPOSITORY https://github.com/google/dawn
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""  # DAWN_FETCH_DEPENDENCIES handles deps via CMake
)
FetchContent_MakeAvailable(dawn)

# ── GLFW ─────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── glfw3webgpu ───────────────────────────────────────────────────────────────
# glfw3webgpu expects a target named 'webgpu'; alias Dawn's library
add_library(webgpu ALIAS webgpu_dawn)

FetchContent_Declare(
    glfw3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu
    GIT_TAG        main
)
FetchContent_MakeAvailable(glfw3webgpu)

if (GLFW_BUILD_WAYLAND)
    target_compile_definitions(glfw3webgpu PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
endif()
if (GLFW_BUILD_X11)
    target_compile_definitions(glfw3webgpu PRIVATE GLFW_EXPOSE_NATIVE_X11)
endif()

# ── cubey ─────────────────────────────────────────────────────────────────────
add_executable(cubey src/main.cpp)

target_link_libraries(cubey PRIVATE
    webgpu_dawn
    glfw
    glfw3webgpu
)

target_include_directories(cubey PRIVATE src)
