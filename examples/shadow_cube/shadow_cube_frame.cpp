#include "shadow_cube_app_internal.h"
#include "shadow_cube_render.h"

#include <cubey/render/pass.h>
#include <cubey/scene/render_recording.h>
#include <cubey/vulkan/command_recorder.h>

#include <optional>
#include <stdexcept>

namespace cubey::examples::shadow_cube::detail {

ShadowCubeApp::ShadowRenderGraph ShadowCubeApp::current_render_graph(
    const cubey::host::WindowedRenderFrame& frame,
    const cubey::scene::RenderFramePlan3D& shadow_plan,
    const cubey::scene::RenderFramePlan3D& scene_plan) const {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle backbuffer_handle = graph.import_color_target(
        "backbuffer", frame.color_target, undefined_texture_state(), present_texture_state());
    const cubey::render::RenderGraphTextureHandle scene_color_handle =
        graph.create_texture(cubey::render::RenderGraphTextureDesc{
            .label = "scene color",
            .extent = frame.color_target.extent,
            .format = frame.color_target.format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        });
    const cubey::render::RenderGraphTextureHandle scene_depth_handle =
        graph.import_depth_target("scene depth",
                                  cubey::render::depth_target_view(depth_attachment()),
                                  undefined_texture_state());
    const std::optional<cubey::render::RenderGraphTextureState> shadow_initial_state =
        shadow_depth_is_sampled_ ? sampled_depth_texture_state() : undefined_texture_state();
    const cubey::render::RenderGraphTextureHandle shadow_depth_handle =
        graph.import_depth_target("shadow depth", cubey::render::depth_target_view(shadow_depth()),
                                  shadow_initial_state);

    graph.add_pass("shadow", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_depth(shadow_depth_handle)
        .material_pass(shadow_depth_pass_info())
        .execute([this, &shadow_plan](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder& recorder = context.recorder();
            record_shadow_pass(recorder, shadow_plan);
        });
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(shadow_depth_handle)
        .write_color(scene_color_handle)
        .write_depth(scene_depth_handle)
        .material_pass(shadow_scene_pass_info())
        .execute([this, scene_color_handle, &scene_plan,
                  &shadow_plan](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder& recorder = context.recorder();
            const cubey::render::ColorTargetView target =
                cubey::render::resolved_color_target_view(context, scene_color_handle);
            record_scene_pass(recorder, target, scene_plan, shadow_plan);
        });
    graph.add_pass("present", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(scene_color_handle)
        .write_color(backbuffer_handle)
        .execute([this, &frame](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder& recorder = context.recorder();
            record_present_pass(recorder, frame);
        });

    return {
        .graph = graph.compile(),
        .scene_color = scene_color_handle,
    };
}

void ShadowCubeApp::update_present_descriptor(
    cubey::host::WindowedAppContext& context,
    cubey::render::FrameSlot frame_slot,
    const cubey::render::CompiledRenderGraph& graph,
    const cubey::render::RenderGraphResourceSet& resources,
    cubey::render::RenderGraphTextureHandle scene_color) const {
    const cubey::render::RenderGraphSampledTextureView sampled =
        cubey::render::resolved_sampled_texture_view(graph, resources, scene_color);
    cubey::render::MaterialDescriptorWriter(present_material_instance().set(frame_slot))
        .combined_image_sampler(0, present_sampler().handle(), sampled.view, sampled.layout)
        .update(context.device());
}

void ShadowCubeApp::record_shadow_frame(cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, frame.color_target.extent);
    if (frame_plan.passes().size() != 2) {
        throw std::runtime_error("shadow_cube frame plan should have two passes");
    }
    const cubey::scene::RenderFramePlan3D& shadow_plan = frame_plan.passes()[0].frame_plan;
    const cubey::scene::RenderFramePlan3D& scene_plan = frame_plan.passes()[1].frame_plan;

    const ShadowRenderGraph render_graph = current_render_graph(frame, shadow_plan, scene_plan);
    graph_executor_.record(
        cubey::render::RenderGraphFrameRecordInfo{
            .device = &context.device(),
            .command_buffer = frame.command_buffer,
            .frame_slot = frame.frame_slot,
            .label = "vkEndCommandBuffer shadow_cube",
        },
        render_graph.graph,
        [this, &context, &frame,
         &render_graph](const cubey::render::RenderGraphResourceSet& resources) {
            update_present_descriptor(context, frame.frame_slot, render_graph.graph, resources,
                                      render_graph.scene_color);
        });
    shadow_depth_is_sampled_ = true;
}

void ShadowCubeApp::record_shadow_pass(
    const cubey::vulkan::CommandRecorder& recorder,
    const cubey::scene::RenderFramePlan3D& shadow_plan) const {
    cubey::render::record_depth_only_pass(
        recorder, cubey::render::depth_target_view(shadow_depth()),
        cubey::render::depth_clear_value(),
        [this, &shadow_plan](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::scene::record_pipeline_draw_packets_3d(
                pass_recorder, shadow_plan.draw_packets, meshes_,
                {
                    .pipeline = &shadow_pipeline_resource(),
                    .filter =
                        {
                            .material_pass = cubey::render::MaterialPassKind::DepthOnly,
                            .require_shadow_caster = true,
                        },
                },
                [this, &shadow_plan](const cubey::vulkan::CommandRecorder& packet_recorder,
                                     const cubey::scene::RenderDrawPacket3D& packet) {
                    const ShadowPushConstants push_constants{
                        .light_mvp =
                            shadow_plan.view_projection_matrix * packet.world_affine_matrix,
                    };
                    packet_recorder.push_constants(shadow_pipeline_resource().layout(),
                                                   VK_SHADER_STAGE_VERTEX_BIT, 0,
                                                   push_constants);
                });
        });
}

void ShadowCubeApp::record_scene_pass(
    const cubey::vulkan::CommandRecorder& recorder,
    cubey::render::ColorTargetView color_target,
    const cubey::scene::RenderFramePlan3D& scene_plan,
    const cubey::scene::RenderFramePlan3D& shadow_plan) const {
    cubey::render::record_render_target_pass(
        recorder,
        cubey::render::render_target_view(color_target,
                                          cubey::render::depth_target_view(depth_attachment())),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.026F, 0.029F, 0.034F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [this, &scene_plan, &shadow_plan](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::scene::record_pipeline_draw_packets_3d(
                pass_recorder, scene_plan.draw_packets, meshes_,
                {
                    .pipeline = &scene_pipeline_resource(),
                    .material = &scene_material_instance(),
                    .filter =
                        {
                            .material_pass = cubey::render::MaterialPassKind::ForwardColor,
                        },
                },
                [this, &scene_plan,
                 &shadow_plan](const cubey::vulkan::CommandRecorder& packet_recorder,
                               const cubey::scene::RenderDrawPacket3D& packet) {
                    const ScenePushConstants push_constants{
                        .mvp = scene_plan.view_projection_matrix * packet.world_affine_matrix,
                        .light_mvp =
                            shadow_plan.view_projection_matrix * packet.world_affine_matrix,
                    };
                    packet_recorder.push_constants(scene_pipeline_resource().layout(),
                                                   VK_SHADER_STAGE_VERTEX_BIT, 0,
                                                   push_constants);
                });
        });
}

void ShadowCubeApp::record_present_pass(
    const cubey::vulkan::CommandRecorder& recorder,
    const cubey::host::WindowedRenderFrame& frame) const {
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(frame.color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        [this, frame_slot = frame.frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &present_pipeline_resource(),
                    .descriptor_set = present_material_instance().set(frame_slot),
                });
        });
}

} // namespace cubey::examples::shadow_cube::detail
