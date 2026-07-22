#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_internal.h"

#include <cubey/render/pass.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace cubey {
namespace {

using DeformationBufferMap =
    std::unordered_map<render::MeshHandle, render::RenderGraphBufferHandle, render::MeshHandleHash>;

[[nodiscard]] render::RenderGraphBufferDesc
deformation_output_buffer_desc(const render::GpuDeformationCommand& command,
                               std::uint32_t command_index) {
    if (!command.mesh || command.output_mesh == nullptr) {
        throw std::runtime_error("deformation graph import requires output meshes");
    }
    return {
        .label = "deformed vertices " + std::to_string(command_index),
        .byte_size = command.output_mesh->vertex_buffer().size(),
    };
}

[[nodiscard]] DeformationBufferMap import_deformation_output_buffers(
    render::RenderGraphBuilder& graph,
    std::span<const render::GpuDeformationCommand> deformation_commands) {
    DeformationBufferMap buffers;
    for (std::uint32_t command_index = 0;
         command_index < static_cast<std::uint32_t>(deformation_commands.size()); ++command_index) {
        const render::GpuDeformationCommand& command = deformation_commands[command_index];
        if (buffers.contains(command.mesh)) {
            continue;
        }
        buffers.emplace(command.mesh,
                        graph.import_buffer(deformation_output_buffer_desc(command, command_index),
                                            command.output_mesh->vertex_buffer().handle()));
    }
    return buffers;
}

void declare_deformation_vertex_reads(render::RenderGraphPassBuilder& pass,
                                      const DeformationBufferMap& buffers) {
    for (const auto& [mesh, buffer] : buffers) {
        (void)mesh;
        pass.read_vertex_buffer(buffer);
    }
}

} // namespace

void ForwardPbrRenderer3D::record(const ForwardPbrRenderer3DFrameRequestInfo& info) {
    record(forward_pbr_renderer_3d_render_request(info));
}

void ForwardPbrRenderer3D::record(const ForwardPbrRenderer3DRenderRequest& request) {
    impl_->record(request);
}

void ForwardPbrRenderer3D::Impl::record(const ForwardPbrRenderer3DRenderRequest& request) {
    require_global_resources();
    require_swapchain_resources();
    validate_forward_pbr_renderer_3d_render_request(request);

    const ForwardPbrRenderer3DTargetInfo& target = request.target;
    const ForwardPbrRenderer3DViewInfo& view = request.view;
    const ForwardPbrRenderer3DSceneResources& resources = request.scene_resources;
    const ForwardPbrRenderer3DSettings& settings = request.settings;
    const ForwardPbrRenderer3DFramePlans frame_plans =
        forward_pbr_renderer_3d_frame_plans(*view.frame_plan);
    const scene::RenderFramePlan3D& shadow_plan = *frame_plans.shadow;
    const scene::RenderFramePlan3D& scene_plan = *frame_plans.scene;
    if (settings.background_mode == ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
        require_atmosphere_background_resources();
    }

    const math::Vec3 camera_position =
        forward_pbr_renderer_3d_camera_world_position(*view.scene, view.camera_entity);
    const LightPacket3D light = forward_pbr_renderer_3d_selected_light(
        scene_plan.light_packets, view.light_entity, view.fallback_light);

    scene_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_scene_uniforms({
            .view_projection = scene_plan.view_projection_matrix,
            .light_view_projection = shadow_plan.view_projection_matrix,
            .camera_position = camera_position,
            .light = light,
            .environment = scene_plan.environment,
            .environment_intensity = global_.environment.intensity,
            .prefiltered_mip_levels = global_.environment.prefiltered_mip_levels,
            .environment_blend = global_.environment.prefiltered_blend,
            .environment_rotation_degrees = settings.environment_rotation_degrees,
            .debug_view = settings.debug_view,
        }));
    skybox_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_skybox_uniforms({
            .view_projection = scene_plan.view_projection_matrix,
            .camera_position = camera_position,
            .environment_intensity = global_.environment.intensity,
            .environment_blend = global_.environment.prefiltered_blend,
            .environment_rotation_degrees = settings.environment_rotation_degrees,
        }));
    if (settings.background_mode == ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
        global_.atmosphere_background.upload(target.frame_slot,
                                             settings.atmosphere_background.value());
    }
    const bool uses_final_display_transform = settings.debug_view == render::PbrDebugView::Final;
    std::optional<ForwardPbrRenderer3DTerrainBackdrop> terrain;
    if (uses_final_display_transform && settings.terrain_backdrop.has_value()) {
        terrain = settings.terrain_backdrop;
        terrain->frame.atmosphere = settings.atmosphere_background.value();
        terrain->runtime->prepare_frame(target.frame_slot, terrain->frame);
    }
    post_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_post_uniforms({
            .color_format = target.color_target.format,
            .exposure = uses_final_display_transform ? settings.exposure : 0.0F,
            .tonemap = uses_final_display_transform ? settings.tonemap : render::PbrTonemap::Linear,
        }));

    const CompiledGraph render_graph = current_render_graph(
        target.color_target, target.frame_slot, target.color_initial_state,
        target.color_final_state, shadow_plan, scene_plan, *resources.meshes,
        resources.frame_meshes, resources.deformation_commands, *resources.materials,
        settings.debug_view, settings.background_mode, settings.atmosphere_clouds, terrain);
    CloudEnvironmentRuntime* cloud_runtime =
        settings.atmosphere_clouds.has_value() ? settings.atmosphere_clouds->runtime : nullptr;
    global_.graph_executor.record(
        render::RenderGraphFrameRecordInfo{
            .device = target.device,
            .command_buffer = target.command_buffer,
            .frame_slot = target.frame_slot,
            .label = target.command_buffer_label,
            .command_buffer_mode = target.command_buffer_mode,
            .profiler = target.profiler,
        },
        render_graph.graph,
        [this, device = target.device, frame_slot = target.frame_slot, cloud_runtime,
         &render_graph](const render::RenderGraphResourceSet& graph_resources) {
            update_post_descriptor(*device, frame_slot, render_graph.graph, graph_resources,
                                   render_graph.post_scene_color);
            if (render_graph.clouds_enabled) {
                cloud_runtime->update_surface_descriptors(
                    *device, frame_slot, render_graph.graph, graph_resources, render_graph.cloud,
                    render_graph.scene_color, render_graph.scene_depth);
            }
        });
    if (render_graph.clouds_enabled) {
        cloud_runtime->complete_surface_frame(target.frame_slot, render_graph.cloud);
    }
    if (terrain.has_value()) {
        terrain->runtime->complete_frame();
    }
    swapchain_.shadow_depth_is_sampled = true;
}

ForwardPbrRenderer3D::Impl::CompiledGraph ForwardPbrRenderer3D::Impl::current_render_graph(
    render::ColorTargetView color_target, render::FrameSlot frame_slot,
    render::RenderGraphTextureState color_initial_state,
    render::RenderGraphTextureState color_final_state, const scene::RenderFramePlan3D& shadow_plan,
    const scene::RenderFramePlan3D& scene_plan,
    const render::MeshResourceTable<render::Mesh>& meshes,
    const render::FrameMeshResourceTable* frame_meshes,
    std::span<const render::GpuDeformationCommand> deformation_commands,
    const render::PbrMaterialTable& materials, render::PbrDebugView debug_view,
    ForwardPbrRenderer3DBackgroundMode background_mode,
    const std::optional<ForwardPbrRenderer3DAtmosphereClouds>& clouds,
    const std::optional<ForwardPbrRenderer3DTerrainBackdrop>& terrain) {
    render::RenderGraphBuilder graph;
    const render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "backbuffer", color_target, color_initial_state, color_final_state);
    const render::RenderGraphTextureHandle scene_color =
        graph.create_texture(render::RenderGraphTextureDesc{
            .label = "scene color",
            .extent = {color_target.extent.width, color_target.extent.height, 1},
            .format = config_.scene_color_format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        });
    const render::RenderGraphTextureHandle scene_depth =
        graph.import_depth_target("scene depth", render::depth_target_view(depth_attachment()),
                                  render::render_graph_undefined_texture_state());
    render::RenderGraphTextureHandle post_scene_color = scene_color;
    render::RenderGraphTextureHandle cloud_scene_color{};
    render::CloudLayerRuntimeFrame cloud_frame{};
    const bool clouds_enabled =
        debug_view == render::PbrDebugView::Final && clouds.has_value() && clouds->frame.enabled;
    if (clouds_enabled) {
        cloud_scene_color = graph.create_texture(render::RenderGraphTextureDesc{
            .label = "atmosphere cloud scene color",
            .extent = {color_target.extent.width, color_target.extent.height, 1},
            .format = config_.scene_color_format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        });
        cloud_frame = clouds->runtime->declare_surface_product(graph, frame_slot, clouds->frame);
    }
    const std::optional<render::RenderGraphTextureState> shadow_initial_state =
        swapchain_.shadow_depth_is_sampled ? render::render_graph_sampled_depth_texture_state()
                                           : render::render_graph_undefined_texture_state();
    const render::RenderGraphTextureHandle shadow_depth = graph.import_depth_target(
        "shadow depth", shadow_pass().depth_target(), shadow_initial_state);
    render::RenderGraphTextureHandle terrain_shadow_depth{};
    if (terrain.has_value()) {
        const std::optional<render::RenderGraphTextureState> terrain_shadow_initial_state =
            terrain->runtime->shadow_depth_is_sampled()
                ? render::render_graph_sampled_depth_texture_state()
                : render::render_graph_undefined_texture_state();
        terrain_shadow_depth = graph.import_depth_target("terrain shadow depth",
                                                         terrain->runtime->shadow_depth_target(),
                                                         terrain_shadow_initial_state);
    }
    const DeformationBufferMap deformation_vertex_buffers =
        import_deformation_output_buffers(graph, deformation_commands);
    const render::MeshResolver mesh_resolver{
        .meshes = &meshes,
        .frame_meshes = frame_meshes,
        .frame_slot = frame_slot,
    };

    if (!deformation_commands.empty()) {
        auto deformation_pass = graph.add_pass("deform", render::RenderGraphQueueDomain::Compute);
        for (const auto& [mesh, buffer] : deformation_vertex_buffers) {
            (void)mesh;
            deformation_pass.write_storage_buffer(buffer);
        }
        deformation_pass.execute(
            [deformation_commands](const render::RenderGraphExecutionContext& context) {
                render::record_gpu_deformation_commands(context.recorder(), deformation_commands);
            });
    }

    if (terrain.has_value() && terrain->runtime->shadow_update_this_frame()) {
        graph.add_pass("terrain shadow", render::RenderGraphQueueDomain::Graphics)
            .write_depth(terrain_shadow_depth)
            .material_pass(terrain->runtime->shadow_material_pass())
            .execute(
                [runtime = terrain->runtime](const render::RenderGraphExecutionContext& context) {
                    runtime->record_shadow_pass(context.recorder());
                });
    }

    auto shadow_pass_builder = graph.add_pass("shadow", render::RenderGraphQueueDomain::Graphics)
                                   .write_depth(shadow_depth)
                                   .material_pass(shadow_pass().material_pass());
    declare_deformation_vertex_reads(shadow_pass_builder, deformation_vertex_buffers);
    shadow_pass_builder.execute([this, frame_slot, &shadow_plan, mesh_resolver,
                                 &materials](const render::RenderGraphExecutionContext& context) {
        record_shadow_pass(context.recorder(), shadow_plan, frame_slot, mesh_resolver, materials);
    });
    auto scene_pass_builder = graph.add_pass("scene", render::RenderGraphQueueDomain::Graphics)
                                  .read_texture(shadow_depth)
                                  .write_color(scene_color)
                                  .write_depth(scene_depth)
                                  .material_pass(render::pbr_forward_pass_info());
    if (terrain.has_value()) {
        scene_pass_builder.read_texture(terrain_shadow_depth);
    }
    declare_deformation_vertex_reads(scene_pass_builder, deformation_vertex_buffers);
    scene_pass_builder.execute([this, scene_color, frame_slot, &scene_plan, mesh_resolver,
                                &materials, debug_view, background_mode,
                                terrain_runtime = terrain.has_value() ? terrain->runtime : nullptr](
                                   const render::RenderGraphExecutionContext& context) {
        const render::ColorTargetView target =
            render::resolved_color_target_view(context, scene_color);
        record_scene_pass(context.recorder(), target, scene_plan, frame_slot, mesh_resolver,
                          materials, debug_view, background_mode, terrain_runtime);
    });
    if (clouds_enabled) {
        clouds->runtime->declare_surface_composite(graph, cloud_scene_color, cloud_frame,
                                                   frame_slot, scene_color, scene_depth);
        post_scene_color = cloud_scene_color;
    }
    graph.add_pass("post", render::RenderGraphQueueDomain::Graphics)
        .read_texture(post_scene_color)
        .write_color(backbuffer)
        .material_pass(render::pbr_post_pass_info())
        .execute(
            [this, color_target, frame_slot](const render::RenderGraphExecutionContext& context) {
                record_post_pass(context.recorder(), color_target, frame_slot);
            });

    return {
        .graph = graph.compile(),
        .scene_color = scene_color,
        .post_scene_color = post_scene_color,
        .scene_depth = scene_depth,
        .cloud_scene_color = cloud_scene_color,
        .cloud = cloud_frame,
        .clouds_enabled = clouds_enabled,
    };
}

void ForwardPbrRenderer3D::Impl::update_post_descriptor(
    const vulkan::Device& device, render::FrameSlot frame_slot,
    const render::CompiledRenderGraph& graph, const render::RenderGraphResourceSet& resources,
    render::RenderGraphTextureHandle scene_color) const {
    const render::RenderGraphSampledTextureView sampled =
        render::resolved_sampled_texture_view(graph, resources, scene_color);
    render::MaterialDescriptorWriter(post_material().set(frame_slot))
        .combined_image_sampler(forward_pbr_renderer_3d_binding(render::PbrPostBinding::SceneColor),
                                post_sampler().handle(), sampled.view, sampled.layout)
        .update(device);
}

} // namespace cubey
