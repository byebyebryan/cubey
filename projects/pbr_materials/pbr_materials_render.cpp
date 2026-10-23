#include "pbr_materials_app_internal.h"

#include <cubey/render/render_graph.h>

namespace cubey::projects::pbr_materials::detail {

void PbrMaterialsApp::record_cube_frame(cubey::host::WindowedAppContext& context,
                                         const cubey::host::WindowedRenderFrame& frame) {
    record_frame(context.device(), frame.command_buffer, frame.color_target, frame.frame_slot, true);
}

void PbrMaterialsApp::record_frame(const cubey::vulkan::Device& device, VkCommandBuffer command,
                                  cubey::render::ColorTargetView target,
                                  cubey::render::FrameSlot slot, bool present) {
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, target.extent);
    forward_pbr_renderer().record({
        .target =
            {
                .device = &device,
                .command_buffer = command,
                .color_target = target,
                .frame_slot = slot,
                .color_initial_state = cubey::render::render_graph_undefined_texture_state(),
                .color_final_state = present ? cubey::render::render_graph_present_texture_state() : cubey::render::render_graph_color_attachment_texture_state(),
                .command_buffer_label = "vkEndCommandBuffer pbr_materials",
                .command_buffer_mode = present ? cubey::render::RenderGraphCommandBufferMode::BeginAndEnd : cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
        .view =
            {
                .scene = &scene_view,
                .frame_plan = &frame_plan,
                .camera_entity = camera_entity_,
                .light_entity = light_entity_,
                .fallback_light = fallback_light_packet(),
            },
        .scene_resources =
            {
                .meshes = &meshes_,
                .materials = &materials_,
            },
        .settings =
            {
                .environment_rotation_degrees = config_.pbr.environment_rotation_degrees,
                .exposure = config_.pbr.exposure,
                .debug_view = debug_view_,
            },
    });
}

} // namespace cubey::projects::pbr_materials::detail
