#include "material_cubes_app_internal.h"

#include <cubey/render/render_graph.h>

namespace cubey::examples::material_cubes::detail {

void MaterialCubesApp::record_cube_frame(cubey::host::WindowedAppContext& context,
                                         const cubey::host::WindowedRenderFrame& frame) {
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, frame.color_target.extent);
    forward_pbr_renderer().record({
        .device = &context.device(),
        .command_buffer = frame.command_buffer,
        .color_target = frame.color_target,
        .frame_slot = frame.frame_slot,
        .color_initial_state = cubey::render::render_graph_undefined_texture_state(),
        .color_final_state = cubey::render::render_graph_present_texture_state(),
        .command_buffer_label = "vkEndCommandBuffer material_cubes",
        .scene = &scene_view,
        .frame_plan = &frame_plan,
        .camera_entity = camera_entity_,
        .light_entity = light_entity_,
        .fallback_light = fallback_light_packet(),
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

} // namespace cubey::examples::material_cubes::detail
