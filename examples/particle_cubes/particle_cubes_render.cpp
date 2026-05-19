#include "particle_cubes_app_internal.h"

#include <cubey/render/mesh.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>

namespace cubey::examples::particle_cubes {

DrawPushConstants ParticleCubesApp::draw_push_constants(const FrameTiming& timing,
                                                        VkExtent2D extent) const {
    (void)timing;
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
        .distance = orbit_controller_.distance(),
        .yaw = orbit_controller_.yaw(),
        .pitch = kCameraBasePitch + orbit_controller_.pitch(),
    });
    const cubey::Camera3D camera;
    return {
        .view_projection = camera.view_projection_matrix(camera_transform, aspect),
    };
}

void ParticleCubesApp::record_particle_cubes_frame(
    const cubey::host::WindowedRenderFrame& frame) {
    const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
    recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    if (!paused_) {
        record_particle_compute(recorder, frame.timing);
    }

    forward_pass().record_to_present_target(
        recorder, frame.color_target,
        [this, &frame](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline = forward_pass().pipeline();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                                              descriptors().set());
            pass_recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                         draw_push_constants(frame.timing,
                                                             frame.color_target.extent));
            cubey::render::record_draw_item(pass_recorder.handle(),
                                            {
                                                .mesh = &cube_mesh(),
                                                .instance_count = kParticleCubeCount,
                                            });
        });

    recorder.end("vkEndCommandBuffer particle_cubes");
}

} // namespace cubey::examples::particle_cubes
