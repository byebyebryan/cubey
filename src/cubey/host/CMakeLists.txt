add_library(cubey_host STATIC
    atmosphere_environment_ui.cpp
    cloud_environment_ui.cpp
    frame_stats.cpp
    glfw_window.cpp
    headless_png_host.cpp
    imgui_overlay.cpp
    performance_ui.cpp
    windowed_app.cpp
    windowed_host.cpp
)
add_library(cubey::host ALIAS cubey_host)

target_include_directories(
    cubey_host
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_compile_definitions(
    cubey_host
    PRIVATE
        GLFW_INCLUDE_NONE
)

target_link_libraries(
    cubey_host
    PUBLIC
        cubey::core
        cubey::input
        cubey::render
        cubey::engine
        cubey::vulkan
        cubey_external_imgui
    PRIVATE
        cubey_project_warnings
        glfw
)
