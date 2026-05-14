#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_common.h"

#include <cubey/render/pass.h>
#include <cubey/scene/render_recording.h>

namespace cubey {

void ForwardPbrRenderer3D::record_shadow_pass(
    const vulkan::CommandRecorder& recorder, const scene::RenderFramePlan3D& shadow_plan,
    render::FrameSlot frame_slot, const render::MeshResourceTable<render::Mesh>& meshes,
    const render::MaterialResourceTable<
        render::FrameUniformMaterialInstance<render::PbrMaterialUniforms>>& material_instances,
    const std::unordered_map<render::MaterialHandle, render::PbrMaterialFactors,
                             render::MaterialHandleHash>& material_factors) const {
    shadow_pass().record(
        recorder, render::depth_clear_value(),
        [this, &shadow_plan, &meshes, &material_instances, &material_factors,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            scene::record_pipeline_draw_packets_3d(
                pass_recorder, shadow_plan.draw_packets, meshes,
                {
                    .pipeline = &shadow_pass().pipeline(),
                    .filter =
                        {
                            .material_pass = render::MaterialPassKind::DepthOnly,
                            .alpha_mode = render::MaterialAlphaMode::Opaque,
                            .require_shadow_caster = true,
                        },
                },
                [this, &shadow_plan](const vulkan::CommandRecorder& packet_recorder,
                                     const scene::RenderDrawPacket3D& packet) {
                    packet_recorder.push_constants(
                        shadow_pass().pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                        ForwardPbrRenderer3DShadowPushConstants{
                            .light_mvp =
                                shadow_plan.view_projection_matrix * packet.world_affine_matrix,
                        });
                });
            scene::record_pipeline_draw_packets_3d(
                pass_recorder, shadow_plan.draw_packets, meshes,
                {
                    .pipeline = &mask_shadow_pipeline(),
                    .frame_slot = frame_slot,
                    .filter =
                        {
                            .material_pass = render::MaterialPassKind::DepthOnly,
                            .alpha_mode = render::MaterialAlphaMode::Mask,
                            .require_shadow_caster = true,
                        },
                },
                [this, &shadow_plan, &material_instances, &material_factors,
                 frame_slot](const vulkan::CommandRecorder& packet_recorder,
                             const scene::RenderDrawPacket3D& packet) {
                    const auto& material = material_instances.at(packet.material);
                    material.upload(frame_slot, render::pbr_material_uniforms(
                                                    material_factors.at(packet.material)));
                    render::bind_material_instance(packet_recorder, mask_shadow_pipeline(),
                                                   material.material(), frame_slot);
                    packet_recorder.push_constants(
                        mask_shadow_pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                        ForwardPbrRenderer3DShadowPushConstants{
                            .light_mvp =
                                shadow_plan.view_projection_matrix * packet.world_affine_matrix,
                        });
                });
        });
}

void ForwardPbrRenderer3D::record_scene_pass(
    const vulkan::CommandRecorder& recorder, render::ColorTargetView color_target,
    const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
    const render::MeshResourceTable<render::Mesh>& meshes,
    const render::MaterialResourceTable<
        render::FrameUniformMaterialInstance<render::PbrMaterialUniforms>>& material_instances,
    const std::unordered_map<render::MaterialHandle, render::PbrMaterialFactors,
                             render::MaterialHandleHash>& material_factors) const {
    render::record_render_target_pass(
        recorder,
        render::render_target_view(color_target, render::depth_target_view(depth_attachment())),
        config_.scene_clear,
        [this, &scene_plan, &meshes, &material_instances, &material_factors,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            render::record_fullscreen_pipeline_draw(
                pass_recorder, render::FullscreenPipelineDrawInfo{
                                   .pipeline = &skybox_pipeline(),
                                   .descriptor_set = skybox_material().set(frame_slot),
                                   .descriptor_set_index = 0,
                               });
            const auto record_blend = [this, &pass_recorder, &scene_plan, &meshes,
                                       &material_instances, &material_factors,
                                       frame_slot](const render::GraphicsPipelineResource& pipeline,
                                                   render::MaterialBlendMode blend) {
                scene::record_pipeline_draw_packets_3d(
                    pass_recorder, scene_plan.draw_packets, meshes,
                    {
                        .pipeline = &pipeline,
                        .material = &scene_material().material(),
                        .frame_slot = frame_slot,
                        .filter =
                            {
                                .material_pass = render::MaterialPassKind::ForwardColor,
                                .blend_mode = blend,
                            },
                    },
                    [&pipeline, &material_instances, &material_factors,
                     frame_slot](const vulkan::CommandRecorder& packet_recorder,
                                 const scene::RenderDrawPacket3D& packet) {
                        const auto& material = material_instances.at(packet.material);
                        material.upload(frame_slot, render::pbr_material_uniforms(
                                                        material_factors.at(packet.material)));
                        render::bind_material_instance(packet_recorder, pipeline,
                                                       material.material(), frame_slot);
                        packet_recorder.push_constants(
                            pipeline.layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            render::pbr_push_constants(packet.world_affine_matrix));
                    });
            };
            record_blend(opaque_pipeline(), render::MaterialBlendMode::Opaque);
            record_blend(alpha_pipeline(), render::MaterialBlendMode::AlphaBlend);
        });
}

void ForwardPbrRenderer3D::record_post_pass(const vulkan::CommandRecorder& recorder,
                                            render::ColorTargetView color_target,
                                            render::FrameSlot frame_slot) const {
    render::record_render_target_pass(
        recorder, render::render_target_view(color_target),
        render::RenderClearValues{
            .color = render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        [this, frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            render::record_fullscreen_pipeline_draw(
                pass_recorder, render::FullscreenPipelineDrawInfo{
                                   .pipeline = &post_pipeline(),
                                   .descriptor_set = post_material().set(frame_slot),
                                   .descriptor_set_index = 0,
                               });
        });
}

} // namespace cubey
