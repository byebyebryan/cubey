#include "instanced_cubes_app_internal.h"

#include "../common/cube_scene.h"

#include <cubey/scene/render_recording.h>
#include <cubey/vulkan/command_recorder.h>

#include <stdexcept>

namespace cubey::examples::instanced_cubes {
namespace {

struct PushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Mat4 cube_spin;
};

} // namespace

cubey::scene::RenderFramePlan3D
InstancedCubesApp::current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const {
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
        throw std::runtime_error("instanced_cubes scene should produce one draw packet");
    }
    return plan;
}

cubey::math::Mat4 InstancedCubesApp::cube_spin_matrix(const FrameTiming& timing) const {
    const float seconds = static_cast<float>(timing.elapsed_seconds);
    return cubey::examples::common::cube_spin_transform(seconds).affine_matrix();
}

void InstancedCubesApp::record_cube_frame(const cubey::host::WindowedRenderFrame& frame) {
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::RenderFramePlan3D frame_plan =
        current_frame_plan(scene_view, frame.color_target.extent);
    const cubey::math::Mat4 cube_spin = cube_spin_matrix(frame.timing);
    const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
    recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    forward_pass().record_to_present_target(
        recorder, frame.color_target,
        [this, &frame_plan, &cube_spin](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::scene::record_pipeline_draw_packets_3d(
                pass_recorder, frame_plan.draw_packets, meshes_,
                {
                    .pipeline = &forward_pass().pipeline(),
                },
                [this, &frame_plan, &cube_spin](
                    const cubey::vulkan::CommandRecorder& packet_recorder,
                    const cubey::scene::RenderDrawPacket3D&) {
                    instance_buffer().bind(packet_recorder, 1);
                    packet_recorder.push_constants(
                        forward_pass().pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                        PushConstants{
                            .view_projection = frame_plan.view_projection_matrix,
                            .cube_spin = cube_spin,
                        });
                });
        });

    recorder.end("vkEndCommandBuffer instanced_cubes");
}

} // namespace cubey::examples::instanced_cubes
