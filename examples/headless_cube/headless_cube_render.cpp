#include "headless_cube_app_internal.h"

#include "../common/cube_scene.h"

#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#include <stdexcept>

namespace cubey::examples::headless_cube {
namespace {

struct PushConstants {
    cubey::math::Mat4 mvp;
};

} // namespace

cubey::math::Mat4
HeadlessCubeApp::cube_mvp(VkExtent2D extent,
                          const cubey::host::HeadlessCaptureFrame& frame) const {
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
        .distance = 4.5F,
        .yaw = 0.62F,
        .pitch = -0.34F,
    });
    const cubey::Camera3D camera;
    const float spin_time =
        frame.count == 1 ? 0.75F : static_cast<float>(frame.timing.elapsed_seconds);
    const cubey::Transform3D cube_transform =
        cubey::examples::common::cube_spin_transform(spin_time);
    return camera.view_projection_matrix(camera_transform, aspect) * cube_transform.affine_matrix();
}

void HeadlessCubeApp::render_capture(VkCommandBuffer command_buffer,
                                     const cubey::host::HeadlessRenderTarget& target,
                                     const cubey::host::HeadlessCaptureFrame& frame) const {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    recorder.transition_image_layout(cubey::vulkan::begin_depth_attachment_transition(
        forward_pass().depth_attachment().handle()));
    forward_pass().record_to_prepared_target(
        recorder, target,
        [this, &target, &frame](const cubey::vulkan::CommandRecorder& pass_recorder) {
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        forward_pass().pipeline().pipeline());
            pass_recorder.push_constants(forward_pass().pipeline().layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT, 0,
                                         PushConstants{.mvp = cube_mvp(target.extent, frame)});
            cubey::render::record_draw_item(pass_recorder.handle(), {.mesh = &cube_mesh()});
        });
}

const cubey::render::Mesh& HeadlessCubeApp::cube_mesh() const {
    if (!cube_mesh_.has_value()) {
        throw std::runtime_error("headless cube mesh is not initialized");
    }
    return cube_mesh_.value();
}

const cubey::render::ForwardScenePass3D& HeadlessCubeApp::forward_pass() const {
    if (!forward_pass_.has_value()) {
        throw std::runtime_error("headless cube forward pass is not initialized");
    }
    return forward_pass_.value();
}

} // namespace cubey::examples::headless_cube
