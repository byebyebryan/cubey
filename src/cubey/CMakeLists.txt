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

add_library(cubey_core STATIC
    $<TARGET_OBJECTS:cubey_stb_image_write>
    run_config.cpp
    camera_2d.cpp
    camera_3d.cpp
    file_io.cpp
    frame_clock.cpp
    frame_stats.cpp
    frame_tickets.cpp
    image_io.cpp
    input.cpp
    jobs.cpp
    orbit_controller.cpp
    pan_zoom_2d_controller.cpp
    pointer_drag.cpp
    spirv_io.cpp
)
add_library(cubey::core ALIAS cubey_core)

target_include_directories(
    cubey_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_include_directories(
    cubey_core
    SYSTEM
    PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/stb"
)

target_link_libraries(
    cubey_core
    PUBLIC
        cubey_project_options
        glm::glm
    PRIVATE
        cubey_project_warnings
)

target_compile_definitions(
    cubey_core
    PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
)

add_library(cubey_vulkan STATIC
    vulkan/buffer.cpp
    vulkan/command_pool.cpp
    vulkan/command_recorder.cpp
    vulkan/descriptors.cpp
    vulkan/device.cpp
    vulkan/dynamic_rendering.cpp
    vulkan/frame_resources.cpp
    vulkan/gpu_runtime.cpp
    vulkan/image.cpp
    vulkan/immediate_commands.cpp
    vulkan/instance.cpp
    vulkan/pipeline.cpp
    vulkan/image_transitions.cpp
    vulkan/queue_submit.cpp
    vulkan/render_context.cpp
    vulkan/sampler.cpp
    vulkan/shader_module.cpp
    vulkan/submission_coordinator.cpp
    vulkan/swapchain.cpp
)
add_library(cubey::vulkan ALIAS cubey_vulkan)

target_include_directories(
    cubey_vulkan
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${vulkan_headers_SOURCE_DIR}/include"
)

target_link_libraries(
    cubey_vulkan
    PUBLIC
        cubey::core
        PkgConfig::VULKAN_LOADER
    PRIVATE
        cubey_project_warnings
)

add_library(cubey_render STATIC
    render/frame_data.cpp
    render/mesh.cpp
    render/target.cpp
    render/texture.cpp
    render/uniform_buffer.cpp
)
add_library(cubey::render ALIAS cubey_render)

target_include_directories(
    cubey_render
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    cubey_render
    PUBLIC
        cubey::core
        cubey::vulkan
    PRIVATE
        cubey_project_warnings
)

add_library(cubey_runtime STATIC
    capture_queue.cpp
    headless_png_host.cpp
    project_gpu_services.cpp
    project_runtime.cpp
    upload_queue.cpp
)
add_library(cubey::runtime ALIAS cubey_runtime)

target_include_directories(
    cubey_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    cubey_runtime
    PUBLIC
        cubey::core
        cubey::vulkan
        cubey::render
    PRIVATE
        cubey_project_warnings
)

add_library(cubey_scene STATIC
    engine.cpp
    entity.cpp
    scene.cpp
)
add_library(cubey::scene ALIAS cubey_scene)

target_include_directories(
    cubey_scene
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    cubey_scene
    PUBLIC
        cubey::core
        cubey::render
        cubey::runtime
    PRIVATE
        cubey_project_warnings
)

add_library(cubey INTERFACE)
add_library(cubey::cubey ALIAS cubey)

target_link_libraries(
    cubey
    INTERFACE
        cubey::core
        cubey::vulkan
        cubey::render
        cubey::runtime
        cubey::scene
)
