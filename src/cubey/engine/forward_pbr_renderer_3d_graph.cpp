#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_common.h"

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

void ForwardPbrRenderer3D::record(const ForwardPbrRenderer3DRenderRequest& request) {
    validate_forward_pbr_renderer_3d_render_request(request);

    const ForwardPbrRenderer3DTargetInfo& target = request.target;
    const ForwardPbrRenderer3DViewInfo& view = request.view;
    const ForwardPbrRenderer3DSceneResources& resources = request.scene_resources;
    const ForwardPbrRenderer3DSettings& settings = request.settings;
    const ForwardPbrRenderer3DFramePlans frame_plans =
        forward_pbr_renderer_3d_frame_plans(*view.frame_plan);
    const scene::RenderFramePlan3D& shadow_plan = *frame_plans.shadow;
    const scene::RenderFramePlan3D& scene_plan = *frame_plans.scene;

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
            .environment_intensity = environment().intensity,
            .prefiltered_mip_levels = environment().prefiltered_mip_levels,
            .environment_rotation_degrees = settings.environment_rotation_degrees,
        }));
    skybox_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_skybox_uniforms({
            .view_projection = scene_plan.view_projection_matrix,
            .camera_position = camera_position,
            .environment_intensity = environment().intensity,
            .environment_rotation_degrees = settings.environment_rotation_degrees,
        }));
    post_material().upload(target.frame_slot, forward_pbr_renderer_3d_post_uniforms({
                                                  .color_format = target.color_target.format,
                                                  .exposure = settings.exposure,
                                                  .tonemap = settings.tonemap,
                                              }));

    const CompiledGraph render_graph = current_render_graph(
        target.color_target, target.frame_slot, target.color_initial_state,
        target.color_final_state, shadow_plan, scene_plan, *resources.meshes,
        resources.frame_meshes, resources.deformation_commands, *resources.materials);
    graph_executor_.record(
        render::RenderGraphFrameRecordInfo{
            .device = target.device,
            .command_buffer = target.command_buffer,
            .frame_slot = target.frame_slot,
            .label = target.command_buffer_label,
            .command_buffer_mode = target.command_buffer_mode,
        },
        render_graph.graph,
        [this, device = target.device, frame_slot = target.frame_slot,
         &render_graph](const render::RenderGraphResourceSet& graph_resources) {
            update_post_descriptor(*device, frame_slot, render_graph.graph, graph_resources,
                                   render_graph.scene_color);
        });
    shadow_depth_is_sampled_ = true;
}

ForwardPbrRenderer3D::CompiledGraph ForwardPbrRenderer3D::current_render_graph(
    render::ColorTargetView color_target, render::FrameSlot frame_slot,
    render::RenderGraphTextureState color_initial_state,
    render::RenderGraphTextureState color_final_state, const scene::RenderFramePlan3D& shadow_plan,
    const scene::RenderFramePlan3D& scene_plan,
    const render::MeshResourceTable<render::Mesh>& meshes,
    const render::FrameMeshResourceTable* frame_meshes,
    std::span<const render::GpuDeformationCommand> deformation_commands,
    const render::PbrMaterialTable& materials) {
    render::RenderGraphBuilder graph;
    const render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "backbuffer", color_target, color_initial_state, color_final_state);
    const render::RenderGraphTextureHandle scene_color =
        graph.create_texture(render::RenderGraphTextureDesc{
            .label = "scene color",
            .extent = color_target.extent,
            .format = config_.scene_color_format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        });
    const render::RenderGraphTextureHandle scene_depth =
        graph.import_depth_target("scene depth", render::depth_target_view(depth_attachment()),
                                  render::render_graph_undefined_texture_state());
    const std::optional<render::RenderGraphTextureState> shadow_initial_state =
        shadow_depth_is_sampled_ ? render::render_graph_sampled_depth_texture_state()
                                 : render::render_graph_undefined_texture_state();
    const render::RenderGraphTextureHandle shadow_depth = graph.import_depth_target(
        "shadow depth", shadow_pass().depth_target(), shadow_initial_state);
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
    declare_deformation_vertex_reads(scene_pass_builder, deformation_vertex_buffers);
    scene_pass_builder.execute([this, scene_color, frame_slot, &scene_plan, mesh_resolver,
                                &materials](const render::RenderGraphExecutionContext& context) {
        const render::ColorTargetView target =
            render::resolved_color_target_view(context, scene_color);
        record_scene_pass(context.recorder(), target, scene_plan, frame_slot, mesh_resolver,
                          materials);
    });
    graph.add_pass("post", render::RenderGraphQueueDomain::Graphics)
        .read_texture(scene_color)
        .write_color(backbuffer)
        .material_pass(render::pbr_post_pass_info())
        .execute(
            [this, color_target, frame_slot](const render::RenderGraphExecutionContext& context) {
                record_post_pass(context.recorder(), color_target, frame_slot);
            });

    return {
        .graph = graph.compile(),
        .scene_color = scene_color,
    };
}

void ForwardPbrRenderer3D::update_post_descriptor(
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
