add_library(cubey STATIC
    app_config.cpp
    frame_clock.cpp
    frame_stats.cpp
    image_output.cpp
    orbit_controller.cpp
    vulkan/buffer.cpp
    vulkan/command_pool.cpp
    vulkan/descriptors.cpp
    vulkan/device.cpp
    vulkan/frame_resources.cpp
    vulkan/image.cpp
    vulkan/immediate_commands.cpp
    vulkan/instance.cpp
    vulkan/pipeline.cpp
    vulkan/rendering.cpp
    vulkan/render_context.cpp
    vulkan/sampler.cpp
    vulkan/shader_module.cpp
    vulkan/swapchain.cpp
)
add_library(cubey::cubey ALIAS cubey)

target_include_directories(
    cubey
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${vulkan_headers_SOURCE_DIR}/include"
)

target_include_directories(
    cubey
    SYSTEM
    PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/stb"
)

target_link_libraries(
    cubey
    PUBLIC
        cubey_project_options
        PkgConfig::VULKAN_LOADER
    PRIVATE
        cubey_project_warnings
)
