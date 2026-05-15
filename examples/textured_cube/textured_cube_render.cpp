#include "textured_cube_app_internal.h"

#include <cubey/scene/render_recording.h>
#include <cubey/vulkan/command_recorder.h>

#include <stdexcept>

namespace cubey::examples::textured_cube {

SceneUniforms
TexturedCubeApp::current_scene_uniforms(const cubey::scene::RenderFramePlan3D& plan,
                                        const cubey::scene::RenderDrawPacket3D& packet) const {
    const cubey::LightPacket3D light = current_light_packet(plan);
    const cubey::math::Vec3 ambient =
        plan.environment.ambient_color * plan.environment.ambient_intensity;

    return {
        .mvp = plan.view_projection_matrix * packet.world_affine_matrix,
        .model = packet.world_affine_matrix,
        .light_direction = {light.direction.x, light.direction.y, light.direction.z, 0.0F},
        .light_color = {light.color.x * light.intensity, light.color.y * light.intensity,
                        light.color.z * light.intensity, 1.0F},
        .ambient_color = {ambient.x, ambient.y, ambient.z, 1.0F},
    };
}

cubey::LightPacket3D
TexturedCubeApp::current_light_packet(const cubey::scene::RenderFramePlan3D& plan) const {
    for (const cubey::LightPacket3D& light : plan.light_packets) {
        if (light.entity == light_entity_) {
            if (light.kind != cubey::LightKind3D::Directional) {
                throw std::runtime_error("textured_cube scene light should be directional");
            }
            return light;
        }
    }
    throw std::runtime_error("textured_cube scene should produce one directional light packet");
}

void TexturedCubeApp::update_scene_uniforms(const cubey::scene::RenderFramePlan3D& plan,
                                            const cubey::scene::RenderDrawPacket3D& packet,
                                            cubey::render::FrameSlot frame_slot) {
    const SceneUniforms uniforms = current_scene_uniforms(plan, packet);
    material().upload(frame_slot, uniforms);
}

cubey::scene::RenderFramePlan3D
TexturedCubeApp::current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const {
    const cubey::scene::View3D render_view{
        .camera_entity = camera_entity_,
        .width = extent.width,
        .height = extent.height,
        .environment =
            cubey::scene::Environment3D{
                .ambient_color = {0.045F, 0.045F, 0.045F},
                .ambient_intensity = 1.0F,
            },
    };
    cubey::scene::RenderFramePlan3D plan =
        cubey::scene::build_render_frame_plan_3d(render_view, view, engine_.render_resources());
    if (plan.draw_packets.size() != 1) {
        throw std::runtime_error("textured_cube scene should produce one draw packet");
    }
    return plan;
}

void TexturedCubeApp::record_cube_frame(const cubey::host::WindowedRenderFrame& frame) {
    const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::RenderFramePlan3D frame_plan =
        current_frame_plan(scene_view, frame.color_target.extent);
    const cubey::scene::RenderDrawPacket3D& draw_packet = frame_plan.draw_packets[0];
    update_scene_uniforms(frame_plan, draw_packet, frame.frame_slot);

    recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    forward_pass().record_to_present_target(
        recorder, frame.color_target,
        [this, &frame_plan,
         frame_slot = frame.frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::scene::record_pipeline_draw_packets_3d(
                pass_recorder, frame_plan.draw_packets, meshes_,
                {
                    .pipeline = &forward_pass().pipeline(),
                    .material = &material().material(),
                    .frame_slot = frame_slot,
                },
                [](const cubey::vulkan::CommandRecorder&,
                   const cubey::scene::RenderDrawPacket3D&) {});
        });

    recorder.end("vkEndCommandBuffer textured_cube");
}

} // namespace cubey::examples::textured_cube
