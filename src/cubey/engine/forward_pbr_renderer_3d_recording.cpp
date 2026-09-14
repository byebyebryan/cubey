#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_internal.h"

#include <cubey/render/pass.h>

namespace cubey {
namespace {

template <typename RecordPacket>
void record_forward_pbr_route(const vulkan::CommandRecorder& recorder,
                              const render::GraphicsPipelineResource& pipeline,
                              std::span<const scene::RenderDrawPacket3D> packets,
                              std::span<const std::uint32_t> packet_indices,
                              const render::MeshResolver& mesh_resolver,
                              const render::PbrMaterialTable* materials,
                              const render::MaterialInstance* scene_material,
                              render::FrameSlot frame_slot, RecordPacket&& record_packet) {
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
    if (scene_material != nullptr) {
        render::bind_material_instance(recorder, pipeline, *scene_material, frame_slot);
    }

    std::optional<render::MaterialHandle> bound_material;
    for (const std::uint32_t packet_index : packet_indices) {
        const scene::RenderDrawPacket3D& packet = packets[packet_index];
        if (materials != nullptr &&
            (!bound_material.has_value() || bound_material.value() != packet.material)) {
            render::bind_pbr_material(recorder, pipeline, materials->record(packet.material));
            bound_material = packet.material;
        }
        record_packet(recorder, packet);
        const render::DrawItem draw_item =
            render::resolve_draw_item(scene::render_item_from_packet(packet), mesh_resolver);
        render::record_draw_item(recorder.handle(), draw_item);
    }
}

void record_forward_pbr_scene_route(
    const vulkan::CommandRecorder& recorder, const render::GraphicsPipelineResource& pipeline,
    const render::MaterialInstance& scene_material, const scene::RenderFramePlan3D& scene_plan,
    const ForwardPbrDrawPlan& draw_plan, ForwardPbrDrawRoute route, render::FrameSlot frame_slot,
    const render::MeshResolver& mesh_resolver, const render::PbrMaterialTable& materials) {
    record_forward_pbr_route(recorder, pipeline, scene_plan.draw_packets, draw_plan.indices(route),
                             mesh_resolver, &materials, &scene_material, frame_slot,
                             [&pipeline](const vulkan::CommandRecorder& packet_recorder,
                                         const scene::RenderDrawPacket3D& packet) {
                                 packet_recorder.push_constants(
                                     pipeline.layout(),
                                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                     render::pbr_push_constants(packet.world_affine_matrix));
                             });
}

} // namespace

void ForwardPbrRenderer3D::Impl::record_shadow_pass(const vulkan::CommandRecorder& recorder,
                                                    const scene::RenderFramePlan3D& shadow_plan,
                                                    render::FrameSlot frame_slot,
                                                    const render::MeshResolver& mesh_resolver,
                                                    const render::PbrMaterialTable& materials,
                                                    const ForwardPbrDrawPlan& draw_plan) const {
    shadow_pass().record(
        recorder, render::depth_clear_value(),
        [this, &shadow_plan, mesh_resolver, &materials, &draw_plan,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            const auto record_opaque_shadow =
                [&pass_recorder, &shadow_plan, &draw_plan, mesh_resolver, frame_slot](
                    const render::GraphicsPipelineResource& pipeline, ForwardPbrDrawRoute route) {
                    record_forward_pbr_route(
                        pass_recorder, pipeline, shadow_plan.draw_packets, draw_plan.indices(route),
                        mesh_resolver, nullptr, nullptr, frame_slot,
                        [&pipeline, &shadow_plan](const vulkan::CommandRecorder& packet_recorder,
                                                  const scene::RenderDrawPacket3D& packet) {
                            packet_recorder.push_constants(
                                pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                ForwardPbrRenderer3DShadowPushConstants{
                                    .light_mvp = shadow_plan.view_projection_matrix *
                                                 packet.world_affine_matrix,
                                });
                        });
                };
            record_opaque_shadow(shadow_pass().pipeline(), ForwardPbrDrawRoute::ShadowOpaqueBack);
            record_opaque_shadow(shadow_double_sided_pipeline(),
                                 ForwardPbrDrawRoute::ShadowOpaqueNoCull);

            const auto record_mask_shadow =
                [&pass_recorder, &shadow_plan, &draw_plan, mesh_resolver, &materials, frame_slot](
                    const render::GraphicsPipelineResource& pipeline, ForwardPbrDrawRoute route) {
                    record_forward_pbr_route(
                        pass_recorder, pipeline, shadow_plan.draw_packets, draw_plan.indices(route),
                        mesh_resolver, &materials, nullptr, frame_slot,
                        [&pipeline, &shadow_plan](const vulkan::CommandRecorder& packet_recorder,
                                                  const scene::RenderDrawPacket3D& packet) {
                            packet_recorder.push_constants(
                                pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                ForwardPbrRenderer3DShadowPushConstants{
                                    .light_mvp = shadow_plan.view_projection_matrix *
                                                 packet.world_affine_matrix,
                                });
                        });
                };
            record_mask_shadow(mask_shadow_pipeline(), ForwardPbrDrawRoute::ShadowMaskedBack);
            record_mask_shadow(mask_shadow_double_sided_pipeline(),
                               ForwardPbrDrawRoute::ShadowMaskedNoCull);
        });
}

void ForwardPbrRenderer3D::Impl::record_scene_pass(
    const vulkan::CommandRecorder& recorder, render::ColorTargetView color_target,
    const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
    const render::MeshResolver& mesh_resolver, const render::PbrMaterialTable& materials,
    const ForwardPbrDrawPlan& draw_plan, render::PbrDebugView debug_view,
    ForwardPbrRenderer3DBackgroundMode background_mode, TerrainBackdropRuntime* terrain,
    OceanSurfaceRuntime* ocean, bool preserve_scene_depth) const {
    render::record_render_target_pass(
        recorder,
        render::render_target_view(color_target, render::depth_target_view(depth_attachment())),
        config_.scene_clear,
        render::RenderTargetAttachmentOps{
            .color = vulkan::clear_store_attachment_ops(),
            .depth = preserve_scene_depth ? vulkan::clear_store_attachment_ops()
                                          : vulkan::clear_discard_attachment_ops(),
        },
        [this, &scene_plan, mesh_resolver, &materials, &draw_plan, debug_view, frame_slot,
         background_mode, terrain, ocean](const vulkan::CommandRecorder& pass_recorder) {
            if (debug_view == render::PbrDebugView::Final &&
                background_mode == ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
                render::record_fullscreen_pipeline_draw(
                    pass_recorder,
                    render::FullscreenPipelineDrawInfo{
                        .pipeline = &global_.atmosphere_background.pipeline(),
                        .descriptor_set = global_.atmosphere_background.material().set(frame_slot),
                        .descriptor_set_index = 0,
                    });
            } else if (debug_view == render::PbrDebugView::Final) {
                render::record_fullscreen_pipeline_draw(
                    pass_recorder, render::FullscreenPipelineDrawInfo{
                                       .pipeline = &skybox_pipeline(),
                                       .descriptor_set = skybox_material().set(frame_slot),
                                       .descriptor_set_index = 0,
                                   });
            }
            if (terrain != nullptr) {
                terrain->record_surface_draws(pass_recorder, frame_slot);
            }
            if (ocean != nullptr) {
                ocean->record_surface_draws(pass_recorder, frame_slot);
            }
            record_forward_pbr_scene_route(pass_recorder, opaque_pipeline(),
                                           scene_material().material(), scene_plan, draw_plan,
                                           ForwardPbrDrawRoute::SceneOpaqueBack, frame_slot,
                                           mesh_resolver, materials);
            record_forward_pbr_scene_route(pass_recorder, opaque_double_sided_pipeline(),
                                           scene_material().material(), scene_plan, draw_plan,
                                           ForwardPbrDrawRoute::SceneOpaqueNoCull, frame_slot,
                                           mesh_resolver, materials);
            record_forward_pbr_scene_route(
                pass_recorder, alpha_pipeline(), scene_material().material(), scene_plan, draw_plan,
                ForwardPbrDrawRoute::SceneAlphaBack, frame_slot, mesh_resolver, materials);
            record_forward_pbr_scene_route(pass_recorder, alpha_double_sided_pipeline(),
                                           scene_material().material(), scene_plan, draw_plan,
                                           ForwardPbrDrawRoute::SceneAlphaNoCull, frame_slot,
                                           mesh_resolver, materials);
        });
}

void ForwardPbrRenderer3D::Impl::record_scene_opaque_pass(
    const vulkan::CommandRecorder& recorder, render::ColorTargetView color_target,
    const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
    const render::MeshResolver& mesh_resolver, const render::PbrMaterialTable& materials,
    const ForwardPbrDrawPlan& draw_plan, render::PbrDebugView debug_view,
    ForwardPbrRenderer3DBackgroundMode background_mode, TerrainBackdropRuntime* terrain,
    OceanSurfaceRuntime* ocean) const {
    render::record_render_target_pass(
        recorder,
        render::render_target_view(color_target, render::depth_target_view(depth_attachment())),
        config_.scene_clear,
        render::RenderTargetAttachmentOps{
            .color = vulkan::clear_store_attachment_ops(),
            .depth = vulkan::clear_store_attachment_ops(),
        },
        [this, &scene_plan, mesh_resolver, &materials, &draw_plan, debug_view, frame_slot,
         background_mode, terrain, ocean](const vulkan::CommandRecorder& pass_recorder) {
            if (debug_view == render::PbrDebugView::Final &&
                background_mode == ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
                render::record_fullscreen_pipeline_draw(
                    pass_recorder,
                    render::FullscreenPipelineDrawInfo{
                        .pipeline = &global_.atmosphere_background.pipeline(),
                        .descriptor_set = global_.atmosphere_background.material().set(frame_slot),
                        .descriptor_set_index = 0,
                    });
            } else if (debug_view == render::PbrDebugView::Final) {
                render::record_fullscreen_pipeline_draw(
                    pass_recorder, render::FullscreenPipelineDrawInfo{
                                       .pipeline = &skybox_pipeline(),
                                       .descriptor_set = skybox_material().set(frame_slot),
                                       .descriptor_set_index = 0,
                                   });
            }
            if (terrain != nullptr) {
                terrain->record_surface_draws(pass_recorder, frame_slot);
            }
            if (ocean != nullptr) {
                ocean->record_surface_draws(pass_recorder, frame_slot);
            }
            record_forward_pbr_scene_route(pass_recorder, opaque_pipeline(),
                                           scene_material().material(), scene_plan, draw_plan,
                                           ForwardPbrDrawRoute::SceneOpaqueBack, frame_slot,
                                           mesh_resolver, materials);
            record_forward_pbr_scene_route(pass_recorder, opaque_double_sided_pipeline(),
                                           scene_material().material(), scene_plan, draw_plan,
                                           ForwardPbrDrawRoute::SceneOpaqueNoCull, frame_slot,
                                           mesh_resolver, materials);
        });
}

void ForwardPbrRenderer3D::Impl::record_transmission_stage(
    const vulkan::CommandRecorder& recorder, render::ColorTargetView color_target,
    const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
    const render::MeshResolver& mesh_resolver, const render::PbrMaterialTable& materials,
    const ForwardPbrDrawPlan& draw_plan) const {
    render::record_render_target_pass(
        recorder,
        render::render_target_view(color_target, render::depth_target_view(depth_attachment())),
        config_.scene_clear,
        render::RenderTargetAttachmentOps{
            .color = vulkan::load_store_attachment_ops(),
            .depth = vulkan::load_store_attachment_ops(),
        },
        [this, &scene_plan, mesh_resolver, &materials, &draw_plan,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            record_forward_pbr_scene_route(pass_recorder, opaque_pipeline(),
                                           transmission_scene_material().material(), scene_plan,
                                           draw_plan, ForwardPbrDrawRoute::TransmissionOpaqueBack,
                                           frame_slot, mesh_resolver, materials);
            record_forward_pbr_scene_route(pass_recorder, opaque_double_sided_pipeline(),
                                           transmission_scene_material().material(), scene_plan,
                                           draw_plan, ForwardPbrDrawRoute::TransmissionOpaqueNoCull,
                                           frame_slot, mesh_resolver, materials);
            record_forward_pbr_scene_route(pass_recorder, alpha_pipeline(),
                                           transmission_scene_material().material(), scene_plan,
                                           draw_plan, ForwardPbrDrawRoute::TransmissionAlphaBack,
                                           frame_slot, mesh_resolver, materials);
            record_forward_pbr_scene_route(pass_recorder, alpha_double_sided_pipeline(),
                                           transmission_scene_material().material(), scene_plan,
                                           draw_plan, ForwardPbrDrawRoute::TransmissionAlphaNoCull,
                                           frame_slot, mesh_resolver, materials);
        });
}

void ForwardPbrRenderer3D::Impl::record_scene_alpha_pass(
    const vulkan::CommandRecorder& recorder, render::ColorTargetView color_target,
    const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
    const render::MeshResolver& mesh_resolver, const render::PbrMaterialTable& materials,
    const ForwardPbrDrawPlan& draw_plan) const {
    render::record_render_target_pass(
        recorder,
        render::render_target_view(color_target, render::depth_target_view(depth_attachment())),
        config_.scene_clear,
        render::RenderTargetAttachmentOps{
            .color = vulkan::load_store_attachment_ops(),
            .depth = vulkan::load_store_attachment_ops(),
        },
        [this, &scene_plan, mesh_resolver, &materials, &draw_plan,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            record_forward_pbr_scene_route(
                pass_recorder, alpha_pipeline(), scene_material().material(), scene_plan, draw_plan,
                ForwardPbrDrawRoute::SceneAlphaBack, frame_slot, mesh_resolver, materials);
            record_forward_pbr_scene_route(pass_recorder, alpha_double_sided_pipeline(),
                                           scene_material().material(), scene_plan, draw_plan,
                                           ForwardPbrDrawRoute::SceneAlphaNoCull, frame_slot,
                                           mesh_resolver, materials);
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
