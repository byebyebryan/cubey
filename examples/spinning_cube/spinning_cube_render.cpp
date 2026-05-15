#include "spinning_cube_app_internal.h"

#include <cubey/scene/render_recording.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>

#include <stdexcept>

namespace cubey::examples::spinning_cube {
namespace {

struct PushConstants {
    cubey::math::Mat4 mvp;
};

[[nodiscard]] PushConstants
current_push_constants(const cubey::scene::RenderFramePlan3D& plan,
                       const cubey::scene::RenderDrawPacket3D& packet) {
    return {
        plan.view_projection_matrix * packet.world_affine_matrix,
    };
}

} // namespace

cubey::scene::RenderFramePlan3D
SpinningCubeApp::current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const {
    const cubey::scene::View3D render_view{
        .camera_entity = camera_entity_,
        .width = extent.width,
        .height = extent.height,
    };
    cubey::scene::RenderFramePlan3D plan =
        cubey::scene::build_render_frame_plan_3d(render_view, view, engine_.render_resources());
    if (plan.draw_packets.size() != 1) {
        throw std::runtime_error("spinning_cube scene should produce one draw packet");
    }
    return plan;
}

void SpinningCubeApp::record_cube_frame(const cubey::host::WindowedRenderFrame& frame) {
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::RenderFramePlan3D frame_plan =
        current_frame_plan(scene_view, frame.color_target.extent);
    const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
    recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    forward_pass().record_to_present_target(
        recorder, frame.color_target,
        [this, &frame_plan](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::scene::record_pipeline_draw_packets_3d(
                pass_recorder, frame_plan.draw_packets, meshes_,
                {
                    .pipeline = &forward_pass().pipeline(),
                },
                [this, &frame_plan](const cubey::vulkan::CommandRecorder& packet_recorder,
                                    const cubey::scene::RenderDrawPacket3D& packet) {
                    const PushConstants push_constants =
                        current_push_constants(frame_plan, packet);
                    packet_recorder.push_constants(forward_pass().pipeline().layout(),
                                                   VK_SHADER_STAGE_VERTEX_BIT, 0, push_constants);
                });
        });

    recorder.end("vkEndCommandBuffer spinning_cube");
}

const cubey::render::ForwardScenePass3D& SpinningCubeApp::forward_pass() const {
    if (!forward_pass_.has_value()) {
        throw std::runtime_error("forward pass is not initialized");
    }
    return forward_pass_.value();
}

} // namespace cubey::examples::spinning_cube
