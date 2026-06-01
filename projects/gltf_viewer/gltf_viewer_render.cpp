#include "gltf_viewer_app_internal.h"

#include <stdexcept>
#include <vector>

namespace cubey::projects::gltf_viewer {

void GltfViewerApp::create_frame_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                           VkFormat color_format) {
    forward_pbr_renderer().create_swapchain_resources(
        device, cubey::ForwardPbrRenderer3DTargetResourcesInfo{
                    .extent = extent,
                    .color_format = color_format,
                    .materials = &import_resources_.materials,
                });
}

void GltfViewerApp::destroy_swapchain_resources() {
    engine_.renderers().destroy_swapchain_resources();
}

void GltfViewerApp::destroy_all_resources() {
    engine_.renderers().destroy_all_resources();
    forward_pbr_renderer_ = nullptr;
    atmosphere_runtime_.destroy();
    ibl_environment_.reset();
    atmosphere_background_placeholders_.reset();
    destroy_scene_if_needed();
    cubey::destroy_gltf_scene_import(engine_, import_resources_, import_result_);
    animation_playback_ = {};
    animation_sample_.reset();
    triangle_count_ = 0;
    asset_.reset();
}

void GltfViewerApp::record_viewer_target(
    const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
    cubey::render::ColorTargetView color_target, cubey::render::FrameSlot frame_slot,
    cubey::render::RenderGraphTextureState color_initial_state,
    cubey::render::RenderGraphTextureState color_final_state,
    cubey::render::RenderGraphCommandBufferMode command_buffer_mode) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    cubey::render::RenderGraphCommandBufferMode pbr_command_buffer_mode = command_buffer_mode;
    bool owns_command_buffer = false;
    switch (command_buffer_mode) {
    case cubey::render::RenderGraphCommandBufferMode::BeginAndEnd:
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        pbr_command_buffer_mode = cubey::render::RenderGraphCommandBufferMode::AlreadyRecording;
        owns_command_buffer = true;
        break;
    case cubey::render::RenderGraphCommandBufferMode::AlreadyRecording:
        break;
    default:
        throw std::runtime_error("glTF viewer command buffer mode is invalid");
    }

    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, color_target.extent);
    if (asset_.has_value()) {
        cubey::update_gltf_deformation_frame(
            import_resources_, asset_.value(), import_result_, scene_view, frame_slot,
            animation_sample_.has_value() ? &animation_sample_.value() : nullptr);
    }
    const std::vector<cubey::render::GpuDeformationCommand> deformation_commands =
        cubey::gltf_deformation_commands_for_frame(import_resources_, frame_slot);
    const cubey::render::FrameMeshResourceTable* frame_meshes =
        deformation_commands.empty() ? nullptr : &import_resources_.deformation.frame_meshes;
    record_atmosphere_environment_if_needed(recorder, frame_slot);
    forward_pbr_renderer().record({
        .device = &device,
        .command_buffer = command_buffer,
        .color_target = color_target,
        .frame_slot = frame_slot,
        .color_initial_state = color_initial_state,
        .color_final_state = color_final_state,
        .command_buffer_label = "vkEndCommandBuffer gltf_viewer",
        .command_buffer_mode = pbr_command_buffer_mode,
        .scene = &scene_view,
        .frame_plan = &frame_plan,
        .camera_entity = camera_entity_,
        .light_entity = light_entity_,
        .fallback_light = fallback_light_packet(),
        .scene_resources =
            {
                .meshes = &import_resources_.meshes,
                .frame_meshes = frame_meshes,
                .deformation_commands = deformation_commands,
                .materials = &import_resources_.materials,
            },
        .settings =
            {
                .environment_rotation_degrees = config_.pbr.environment_rotation_degrees,
                .exposure = config_.pbr.exposure,
                .debug_view = debug_view_,
                .background_mode = cubey::ForwardPbrRenderer3DBackgroundMode::Atmosphere,
                .atmosphere_background =
                    atmosphere_background_uniforms(scene_view, color_target.extent),
            },
    });
    if (owns_command_buffer) {
        recorder.end("vkEndCommandBuffer gltf_viewer");
    }
}

void GltfViewerApp::record_atmosphere_environment_if_needed(
    const cubey::vulkan::CommandRecorder& recorder, cubey::render::FrameSlot frame_slot) {
    if (!use_atmosphere_environment_source()) {
        return;
    }
    if (!atmosphere_runtime_.resources_created()) {
        throw std::runtime_error("glTF viewer atmosphere runtime is not initialized");
    }

    atmosphere_runtime_.record_pending_update(recorder, frame_slot);
}

void GltfViewerApp::record_viewer_frame(cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
    record_viewer_target(context.device(), frame.command_buffer, frame.color_target,
                         frame.frame_slot, cubey::render::render_graph_undefined_texture_state(),
                         cubey::render::render_graph_present_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::BeginAndEnd);
}

void GltfViewerApp::record_viewer_capture(cubey::host::HeadlessPngContext& context,
                                          const cubey::host::HeadlessCaptureFrame& frame,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
    record_viewer_target(context.device(), command_buffer, target, frame.frame_slot,
                         cubey::render::render_graph_color_attachment_texture_state(),
                         cubey::render::render_graph_color_attachment_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
}

} // namespace cubey::projects::gltf_viewer
