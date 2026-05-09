add_library(cubey_stb_image_write OBJECT
    stb_image_write.cpp
)
target_include_directories(
    cubey_stb_image_write
    SYSTEM
    PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/stb"
)
target_link_libraries(
    cubey_stb_image_write
    PRIVATE
        cubey_project_options
)
set_target_properties(cubey_stb_image_write PROPERTIES CXX_CLANG_TIDY "")

add_library(cubey STATIC
    $<TARGET_OBJECTS:cubey_stb_image_write>
    run_config.cpp
    capture_queue.cpp
    file_io.cpp
    frame_clock.cpp
    frame_stats.cpp
    frame_tickets.cpp
    headless_png_host.cpp
    image_io.cpp
    input.cpp
    jobs.cpp
    orbit_controller.cpp
    pan_zoom_2d_controller.cpp
    pointer_drag.cpp
    project_runtime.cpp
    spirv_io.cpp
    upload_queue.cpp
    vulkan/buffer.cpp
    vulkan/command_pool.cpp
    vulkan/descriptors.cpp
    vulkan/device.cpp
    vulkan/dynamic_rendering.cpp
    vulkan/frame_resources.cpp
    vulkan/image.cpp
    vulkan/immediate_commands.cpp
    vulkan/instance.cpp
    vulkan/pipeline.cpp
    vulkan/image_transitions.cpp
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
        glm::glm
        PkgConfig::VULKAN_LOADER
    PRIVATE
        cubey_project_warnings
)

target_compile_definitions(
    cubey
    PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
)
