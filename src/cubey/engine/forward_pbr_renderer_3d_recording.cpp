#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_internal.h"

#include <cubey/render/pass.h>
#include <cubey/scene/render_recording.h>

namespace cubey {

void ForwardPbrRenderer3D::Impl::record_shadow_pass(
    const vulkan::CommandRecorder& recorder, const scene::RenderFramePlan3D& shadow_plan,
    render::FrameSlot frame_slot, const render::MeshResolver& mesh_resolver,
    const render::PbrMaterialTable& materials) const {
    shadow_pass().record(
        recorder, render::depth_clear_value(),
        [this, &shadow_plan, mesh_resolver, &materials,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            const auto record_opaque_shadow = [&pass_recorder, &shadow_plan, mesh_resolver](
                                                  const render::GraphicsPipelineResource& pipeline,
                                                  VkCullModeFlags cull_mode) {
                scene::record_pipeline_draw_packets_3d(
                    pass_recorder, shadow_plan.draw_packets, mesh_resolver,
                    {
                        .pipeline = &pipeline,
                        .filter =
                            {
                                .material_pass = render::MaterialPassKind::DepthOnly,
                                .alpha_mode = render::MaterialAlphaMode::Opaque,
                                .cull_mode = cull_mode,
                                .require_shadow_caster = true,
                            },
                    },
                    [&pipeline, &shadow_plan](const vulkan::CommandRecorder& packet_recorder,
                                              const scene::RenderDrawPacket3D& packet) {
                        packet_recorder.push_constants(
                            pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                            ForwardPbrRenderer3DShadowPushConstants{
                                .light_mvp =
                                    shadow_plan.view_projection_matrix * packet.world_affine_matrix,
                            });
                    });
            };
            record_opaque_shadow(shadow_pass().pipeline(), VK_CULL_MODE_BACK_BIT);
            record_opaque_shadow(shadow_double_sided_pipeline(), VK_CULL_MODE_NONE);

            const auto record_mask_shadow =
                [&pass_recorder, &shadow_plan, mesh_resolver, &materials, frame_slot](
                    const render::GraphicsPipelineResource& pipeline, VkCullModeFlags cull_mode) {
                    scene::record_pipeline_draw_packets_3d(
                        pass_recorder, shadow_plan.draw_packets, mesh_resolver,
                        {
                            .pipeline = &pipeline,
                            .frame_slot = frame_slot,
                            .filter =
                                {
                                    .material_pass = render::MaterialPassKind::DepthOnly,
                                    .alpha_mode = render::MaterialAlphaMode::Mask,
                                    .cull_mode = cull_mode,
                                    .require_shadow_caster = true,
                                },
                        },
                        [&pipeline, &shadow_plan, &materials,
                         frame_slot](const vulkan::CommandRecorder& packet_recorder,
                                     const scene::RenderDrawPacket3D& packet) {
                            const auto& material = materials.instance(packet.material);
                            materials.upload(packet.material, frame_slot,
                                             packet.material_info.alpha_mode);
                            render::bind_material_instance(packet_recorder, pipeline,
                                                           material.material(), frame_slot);
                            packet_recorder.push_constants(
                                pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                ForwardPbrRenderer3DShadowPushConstants{
                                    .light_mvp = shadow_plan.view_projection_matrix *
                                                 packet.world_affine_matrix,
                                });
                        });
                };
            record_mask_shadow(mask_shadow_pipeline(), VK_CULL_MODE_BACK_BIT);
            record_mask_shadow(mask_shadow_double_sided_pipeline(), VK_CULL_MODE_NONE);
        });
}

void ForwardPbrRenderer3D::Impl::record_scene_pass(const vulkan::CommandRecorder& recorder,
                                                   render::ColorTargetView color_target,
                                                   const scene::RenderFramePlan3D& scene_plan,
                                                   render::FrameSlot frame_slot,
                                                   const render::MeshResolver& mesh_resolver,
                                                   const render::PbrMaterialTable& materials,
                                                   render::PbrDebugView debug_view) const {
    render::record_render_target_pass(
        recorder,
        render::render_target_view(color_target, render::depth_target_view(depth_attachment())),
        config_.scene_clear,
        [this, &scene_plan, mesh_resolver, &materials, debug_view,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            if (debug_view == render::PbrDebugView::Final) {
                render::record_fullscreen_pipeline_draw(
                    pass_recorder, render::FullscreenPipelineDrawInfo{
                                       .pipeline = &skybox_pipeline(),
                                       .descriptor_set = skybox_material().set(frame_slot),
                                       .descriptor_set_index = 0,
                                   });
            }
            const auto record_blend = [this, &pass_recorder, &scene_plan, mesh_resolver, &materials,
                                       frame_slot](const render::GraphicsPipelineResource& pipeline,
                                                   render::MaterialBlendMode blend,
                                                   VkCullModeFlags cull_mode) {
                scene::record_pipeline_draw_packets_3d(
                    pass_recorder, scene_plan.draw_packets, mesh_resolver,
                    {
                        .pipeline = &pipeline,
                        .material = &scene_material().material(),
                        .frame_slot = frame_slot,
                        .filter =
                            {
                                .material_pass = render::MaterialPassKind::ForwardColor,
                                .blend_mode = blend,
                                .cull_mode = cull_mode,
                            },
                    },
                    [&pipeline, &materials,
                     frame_slot](const vulkan::CommandRecorder& packet_recorder,
                                 const scene::RenderDrawPacket3D& packet) {
                        const auto& material = materials.instance(packet.material);
                        materials.upload(packet.material, frame_slot,
                                         packet.material_info.alpha_mode);
                        render::bind_material_instance(packet_recorder, pipeline,
                                                       material.material(), frame_slot);
                        packet_recorder.push_constants(
                            pipeline.layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            render::pbr_push_constants(packet.world_affine_matrix));
                    });
            };
            record_blend(opaque_pipeline(), render::MaterialBlendMode::Opaque,
                         VK_CULL_MODE_BACK_BIT);
            record_blend(opaque_double_sided_pipeline(), render::MaterialBlendMode::Opaque,
                         VK_CULL_MODE_NONE);
            record_blend(alpha_pipeline(), render::MaterialBlendMode::AlphaBlend,
                         VK_CULL_MODE_BACK_BIT);
            record_blend(alpha_double_sided_pipeline(), render::MaterialBlendMode::AlphaBlend,
                         VK_CULL_MODE_NONE);
        });
}

void ForwardPbrRenderer3D::Impl::record_post_pass(const vulkan::CommandRecorder& recorder,
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
