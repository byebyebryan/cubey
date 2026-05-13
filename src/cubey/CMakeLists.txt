add_library(cubey_stb_image_write OBJECT
    core/stb_image_write.cpp
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
    core/run_config.cpp
    core/file_io.cpp
    core/frame_clock.cpp
    core/image_io.cpp
    core/jobs.cpp
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
    vulkan/shader_bytecode.cpp
    vulkan/submission_tickets.cpp
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
    render/material_instance.cpp
    render/material.cpp
    render/mesh.cpp
    render/pass.cpp
    render/pipeline_resource.cpp
    render/primitive_mesh.cpp
    render/render_graph.cpp
    render/render_graph_resources.cpp
    render/render_graph_sync.cpp
    render/render_item.cpp
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

add_library(cubey_scene STATIC
    scene/camera_2d.cpp
    scene/camera_3d.cpp
    scene/entity.cpp
    scene/light_manager.cpp
    scene/renderable_manager.cpp
    scene/scene.cpp
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
    PRIVATE
        cubey_project_warnings
)

add_library(cubey_engine STATIC
    engine/capture_queue.cpp
    engine/engine.cpp
    engine/project_gpu_services.cpp
    engine/project_runtime.cpp
    engine/upload_queue.cpp
)
add_library(cubey::engine ALIAS cubey_engine)

target_include_directories(
    cubey_engine
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    cubey_engine
    PUBLIC
        cubey::core
        cubey::vulkan
        cubey::render
        cubey::scene
    PRIVATE
        cubey_project_warnings
)

add_library(cubey_input STATIC
    input/input.cpp
    input/orbit_controller.cpp
    input/pan_zoom_2d_controller.cpp
    input/pointer_drag.cpp
)
add_library(cubey::input ALIAS cubey_input)

target_include_directories(
    cubey_input
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(
    cubey_input
    PUBLIC
        cubey::core
        cubey::scene
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
        cubey::scene
        cubey::engine
        cubey::input
)
