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
                    .material_descriptor_set_layout = material_descriptor_set_layout(),
                });
}

void GltfViewerApp::destroy_swapchain_resources() {
    engine_.renderers().destroy_swapchain_resources();
}

void GltfViewerApp::destroy_all_resources() {
    engine_.renderers().destroy_all_resources();
    forward_pbr_renderer_ = nullptr;
    ibl_environment_.reset();
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
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, color_target.extent);
    if (frame_plan.passes().size() != 2) {
        throw std::runtime_error("gltf_viewer frame plan should have two passes");
    }
    const cubey::scene::RenderFramePlan3D& shadow_plan = frame_plan.passes()[0].frame_plan;
    const cubey::scene::RenderFramePlan3D& scene_plan = frame_plan.passes()[1].frame_plan;
    if (asset_.has_value()) {
        cubey::update_gltf_deformation_frame(
            import_resources_, asset_.value(), import_result_, scene_view, frame_slot,
            animation_sample_.has_value() ? &animation_sample_.value() : nullptr);
    }
    const std::vector<cubey::render::GpuDeformationCommand> deformation_commands =
        cubey::gltf_deformation_commands_for_frame(import_resources_, frame_slot);
    const cubey::render::FrameMeshResourceTable* frame_meshes =
        deformation_commands.empty() ? nullptr : &import_resources_.deformation.frame_meshes;
    const cubey::ForwardPbrRenderer3DRenderRequest request{
        .target =
            {
                .device = &device,
                .command_buffer = command_buffer,
                .color_target = color_target,
                .frame_slot = frame_slot,
                .color_initial_state = color_initial_state,
                .color_final_state = color_final_state,
                .command_buffer_label = "vkEndCommandBuffer gltf_viewer",
                .command_buffer_mode = command_buffer_mode,
            },
        .view =
            {
                .scene = &scene_view,
                .shadow_plan = &shadow_plan,
                .scene_plan = &scene_plan,
                .camera_entity = camera_entity_,
                .light_entity = light_entity_,
                .fallback_light = fallback_light_packet(),
            },
        .resources =
            {
                .meshes = &import_resources_.meshes,
                .frame_meshes = frame_meshes,
                .deformation_commands = deformation_commands,
                .material_instances = &import_resources_.materials.instances(),
                .material_factors = &import_resources_.materials.factors_map(),
            },
        .settings =
            {
                .environment_rotation_degrees = config_.environment_rotation_degrees,
                .exposure = config_.exposure,
            },
    };
    forward_pbr_renderer().record(request);
}

void GltfViewerApp::record_viewer_frame(cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
    record_viewer_target(context.device(), frame.command_buffer, frame.color_target,
                         frame.frame_slot, undefined_texture_state(), present_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::BeginAndEnd);
}

void GltfViewerApp::record_viewer_capture(cubey::host::HeadlessPngContext& context,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
    record_viewer_target(context.device(), command_buffer, target,
                         cubey::render::FrameSlot{.index = 0, .count = 1},
                         color_attachment_texture_state(), color_attachment_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
}

} // namespace cubey::projects::gltf_viewer
