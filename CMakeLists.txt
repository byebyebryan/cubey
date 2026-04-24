cmake_minimum_required(VERSION 3.21)
project(cubey LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# WebGPU — Dawn backend via eliemichel's pre-built distribution
# Pin this tag once the stack is validated
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution
    GIT_TAG        main
)
FetchContent_MakeAvailable(webgpu)

# GLFW — windowing + input
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# glfw3webgpu — surface creation bridge between GLFW and WebGPU
FetchContent_Declare(
    glfw3webgpu
    GIT_REPOSITORY https://github.com/eliemichel/glfw3webgpu
    GIT_TAG        main
)
FetchContent_MakeAvailable(glfw3webgpu)

# Expose native handles so glfw3webgpu can build Wayland/X11 surface paths
if (GLFW_BUILD_WAYLAND)
    target_compile_definitions(glfw3webgpu PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
endif()
if (GLFW_BUILD_X11)
    target_compile_definitions(glfw3webgpu PRIVATE GLFW_EXPOSE_NATIVE_X11)
endif()

add_executable(cubey src/main.cpp)

target_link_libraries(cubey PRIVATE
    webgpu
    glfw
    glfw3webgpu
)

target_include_directories(cubey PRIVATE src)

# Copy Dawn shared lib next to the binary on Linux/Mac
if (TARGET copy_webgpu_binaries)
    add_custom_command(TARGET cubey POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:webgpu_dawn>
            $<TARGET_FILE_DIR:cubey>
        COMMENT "Copying Dawn runtime"
    )
endif()
