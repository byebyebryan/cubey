#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_internal.h"

#include <cubey/render/pass.h>
#include <cubey/render/temporal_upscaler.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace cubey {
namespace {

render::RenderGraphTextureState scene_depth_final_state(bool linear_output) {
    if (linear_output)
        return render::render_graph_sampled_depth_texture_state();
    return {.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT};
}

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
    impl_->record(request);
}

void ForwardPbrRenderer3D::Impl::record(const ForwardPbrRenderer3DRenderRequest& request) {
    require_global_resources();
    require_swapchain_resources();
    validate_forward_pbr_renderer_3d_render_request(request);
    if (recorded_target_state_.has_value()) {
        if (pending_temporal_frame_.has_value()) {
            throw std::logic_error(
                "forward PBR TAA candidate is missing its submission or discard notification");
        }
        // Existing non-temporal clients may not install submission callbacks.
        // Starting their next record is the compatibility acceptance point.
        recorded_target_state_.reset();
    }
    recorded_target_state_ = RecordedTargetState{
        .shadow_depth_is_sampled = swapchain_.shadow_depth_is_sampled,
        .scene_depth_state = swapchain_.scene_depth_state,
    };
    if (request.scene_readback) {
        const bool full = config_.scene_color_format == VK_FORMAT_R32G32B32A32_SFLOAT;
        if ((!full && config_.scene_color_format != VK_FORMAT_R16G16B16A16_SFLOAT) ||
            request.scene_readback->byte_size <
                VkDeviceSize{request.target.color_target.extent.width} *
                    request.target.color_target.extent.height * (full ? 16U : 8U))
            throw std::runtime_error(
                "scene HDR readback requires a complete RGBA16F/RGBA32F buffer");
    }

    ForwardPbrRenderer3DFrameMetrics* const metrics = request.metrics;
    if (metrics != nullptr) {
        *metrics = {};
    }

    const ForwardPbrRenderer3DTargetInfo& target = request.target;
    const ForwardPbrRenderer3DViewInfo& view = request.view;
    const ForwardPbrRenderer3DSceneResources& resources = request.scene_resources;
    const ForwardPbrRenderer3DSettings& settings = request.settings;
    const bool upscaling = swapchain_.upscaler != nullptr;
    if (target.color_target.extent.width != swapchain_.target_extent.width ||
        target.color_target.extent.height != swapchain_.target_extent.height) {
        throw std::invalid_argument("forward PBR target extent changed without recreation");
    }
    if (upscaling && settings.anti_aliasing != render::AntiAliasingMode::Off) {
        throw std::invalid_argument("forward PBR upscaling and native TAA require separate views");
    }
    const ForwardPbrRenderer3DFramePlans frame_plans =
        forward_pbr_renderer_3d_frame_plans(*view.frame_plan);
    const scene::RenderFramePlan3D& shadow_plan = *frame_plans.shadow;
    const scene::RenderFramePlan3D& original_scene_plan = *frame_plans.scene;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point draw_plan_start =
        metrics != nullptr ? Clock::now() : Clock::time_point{};
    const ForwardPbrDrawPlan draw_plan = build_forward_pbr_draw_plan(
        frame_plans, metrics != nullptr ? &metrics->draw_plan : nullptr);
    if (settings.opaque_linear_output &&
        (target.color_target.format != config_.scene_color_format ||
         !draw_plan.opaque_layer_compatible())) {
        throw std::runtime_error("opaque linear export requires scene HDR and opaque/masked "
                                 "assets, without clouds or transmission");
    }
    const Clock::time_point draw_plan_end = metrics != nullptr ? Clock::now() : Clock::time_point{};
    if (metrics != nullptr) {
        metrics->draw_plan_build_milliseconds =
            std::chrono::duration<double, std::milli>(draw_plan_end - draw_plan_start).count();
        metrics->material_table = resources.materials->metrics();
    }

    temporal_status_ = {};
    temporal_status_.requested = settings.anti_aliasing;
    std::optional<render::TemporalFrame> temporal_frame;
    std::optional<scene::RenderFramePlan3D> temporal_scene_plan;
    std::vector<ForwardPbrTemporalRigidMotionDraw> rigid_motion_draws;
    if (upscaling || settings.anti_aliasing == render::AntiAliasingMode::Taa) {
        const ForwardPbrRenderer3DTaaBypassReason bypass =
            temporal_bypass_reason(request, original_scene_plan, draw_plan);
        temporal_status_.bypass_reason = bypass;
        if (bypass == ForwardPbrRenderer3DTaaBypassReason::None) {
            const VkExtent2D render_extent = swapchain_.render_extent;
            ensure_temporal_resources(*target.device, render_extent);
            const Entity camera =
                view.camera_entity ? view.camera_entity : original_scene_plan.camera_entity;
            const std::uint64_t view_id =
                view.temporal_view_id != 0
                    ? view.temporal_view_id
                    : (static_cast<std::uint64_t>(camera.generation) << 32U) | camera.index;
            const render::TemporalView temporal_view{
                .projection = original_scene_plan.projection_matrix,
                .view = original_scene_plan.view_matrix,
                .view_id = view_id,
                .content_generation = view.temporal_content_generation,
                .reset = view.temporal_reset || force_temporal_reset_};
            temporal_frame = upscaling
                                 ? swapchain_.upscale_view.prepare(temporal_view, render_extent)
                                 : swapchain_.temporal_aa->begin_frame(target.frame_slot,
                                                                      temporal_view);
            pending_temporal_frame_ = temporal_frame;
            temporal_scene_plan = original_scene_plan;
            temporal_scene_plan->projection_matrix = temporal_frame->jittered_projection;
            temporal_scene_plan->view_projection_matrix = temporal_frame->jittered_view_projection;
            rigid_motion_draws = prepare_rigid_motion_draws(original_scene_plan, draw_plan);
            if (!rigid_motion_draws.empty()) {
                swapchain_.temporal_rigid_motion_material->upload(
                    target.frame_slot,
                    ForwardPbrTemporalRigidMotionUniforms{
                        .current_jittered_view_projection =
                            temporal_frame->jittered_view_projection,
                        .current_view_projection = temporal_frame->view_projection,
                        .previous_view_projection = temporal_frame->previous_view_projection,
                        .extent = {static_cast<float>(render_extent.width),
                                   static_cast<float>(render_extent.height), 0.0F, 0.0F},
                    });
            }
            temporal_status_.upscaled = upscaling;
            temporal_status_.effective =
                upscaling ? render::AntiAliasingMode::Off : render::AntiAliasingMode::Taa;
            temporal_status_.reset_reason = temporal_frame->reset_reason;
            temporal_status_.history_age = temporal_frame->history_age;
            temporal_status_.sample_offset_pixels = temporal_frame->sample_offset_pixels;
        } else {
            if (upscaling) {
                throw std::runtime_error(
                    "forward PBR upscaler cannot admit this view: " +
                    std::string(forward_pbr_renderer_3d_taa_bypass_reason_name(bypass)));
            }
            if (accepted_temporal_frame_) {
                reset_temporal();
            }
        }
    } else if (settings.anti_aliasing != render::AntiAliasingMode::Off) {
        throw std::runtime_error("unknown forward PBR anti-aliasing mode");
    } else if (accepted_temporal_frame_) {
        reset_temporal();
    }
    const scene::RenderFramePlan3D& scene_plan =
        temporal_scene_plan.has_value() ? *temporal_scene_plan : original_scene_plan;
    ForwardPbrRenderer3DFramePlans recording_frame_plans = frame_plans;
    recording_frame_plans.scene = &scene_plan;

    if (draw_plan.has_transmission()) {
        ensure_refraction_pyramid(*target.device);
    }
    if (settings.background_mode == ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
        require_atmosphere_background_resources();
    }

    const math::Vec3 camera_position =
        forward_pbr_renderer_3d_camera_world_position(*view.scene, view.camera_entity);
    const LightPacket3D light = forward_pbr_renderer_3d_selected_light(
        scene_plan.light_packets, view.light_entity, view.fallback_light);

    const bool uses_final_display_transform = settings.debug_view == render::PbrDebugView::Final;
    std::optional<render::AtmosphereEnvironmentFrameUniforms> temporal_atmosphere_background =
        settings.atmosphere_background;
    if (temporal_frame.has_value() &&
        settings.background_mode == ForwardPbrRenderer3DBackgroundMode::Atmosphere &&
        temporal_atmosphere_background.has_value()) {
        auto& uniforms = *temporal_atmosphere_background;
        const float pixel_scale = 2.0F * uniforms.camera_up_tan_half_fovy.w /
                                  static_cast<float>(temporal_frame->extent.height);
        math::Vec3 forward{uniforms.camera_forward_debug_view};
        forward += math::Vec3{uniforms.camera_right_aspect} *
                   (temporal_frame->sample_offset_pixels.x * pixel_scale);
        forward -= math::Vec3{uniforms.camera_up_tan_half_fovy} *
                   (temporal_frame->sample_offset_pixels.y * pixel_scale);
        uniforms.camera_forward_debug_view.x = forward.x;
        uniforms.camera_forward_debug_view.y = forward.y;
        uniforms.camera_forward_debug_view.z = forward.z;
    }
    const render::AtmosphereEnvironmentFrameUniforms* atmosphere_background =
        temporal_atmosphere_background.has_value() ? &*temporal_atmosphere_background : nullptr;
    std::optional<ForwardPbrRenderer3DTerrainBackdrop> terrain;
    if (uses_final_display_transform && settings.terrain_backdrop.has_value()) {
        if (atmosphere_background == nullptr) {
            throw std::runtime_error(
                "forward PBR terrain backdrop requires atmosphere frame uniforms");
        }
        terrain = settings.terrain_backdrop;
        terrain->frame.atmosphere = *atmosphere_background;
        terrain->runtime->prepare_frame(target.frame_slot, terrain->frame);
    }
    std::optional<ForwardPbrRenderer3DOceanSurface> ocean;
    if (uses_final_display_transform && settings.ocean_surface.has_value()) {
        ocean = settings.ocean_surface;
        ocean->frame.view_projection = scene_plan.view_projection_matrix;
        ocean->frame.camera_position_m = camera_position;
        ocean->frame.viewport_extent = target.color_target.extent;
        ocean->runtime->prepare_frame(target.frame_slot, ocean->frame);
    }

    BackdropReflection backdrop_reflection;
    if (terrain.has_value()) {
        backdrop_reflection = terrain->runtime->reflection();
    } else if (ocean.has_value()) {
        backdrop_reflection = ocean->runtime->reflection();
    }
    const render::PbrSceneUniforms scene_uniforms = forward_pbr_renderer_3d_scene_uniforms({
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
        .backdrop_reflection = backdrop_reflection,
        .transmission_source = settings.transmission_source,
    });
    scene_material().upload(target.frame_slot, scene_uniforms);
    if (draw_plan.has_transmission()) {
        transmission_scene_material().upload(target.frame_slot, scene_uniforms);
        const render::HdrColorPyramidSnapshot radiance =
            refraction_pyramid().snapshot(target.frame_slot);
        if (!radiance.bindable()) {
            throw std::runtime_error(
                "refraction pyramid did not create a bindable frame-slot image");
        }
        render::MaterialDescriptorWriter(transmission_scene_material().set(target.frame_slot))
            .combined_image_sampler(
                forward_pbr_renderer_3d_binding(render::PbrSceneBinding::RefractionRadiance),
                radiance.texture->sampler().handle(), radiance.texture->view(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .update(*target.device);
    }
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
        if (atmosphere_background == nullptr) {
            throw std::runtime_error(
                "forward PBR atmosphere background requires atmosphere frame uniforms");
        }
        global_.atmosphere_background.upload(target.frame_slot, *atmosphere_background);
    }
    post_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_post_uniforms({
            .color_format = target.color_target.format,
            .exposure = uses_final_display_transform ? settings.exposure : 0.0F,
            .tonemap = uses_final_display_transform ? settings.tonemap : render::PbrTonemap::Linear,
        }));

    const Clock::time_point render_graph_start =
        metrics != nullptr ? Clock::now() : Clock::time_point{};
    const CompiledGraph render_graph = current_render_graph(
        target.color_target, target.frame_slot, target.color_initial_state,
        target.color_final_state, recording_frame_plans, *resources.meshes, resources.frame_meshes,
        resources.deformation_commands, *resources.materials, settings.debug_view,
        settings.background_mode, settings.atmosphere_clouds, terrain, ocean, draw_plan,
        settings.opaque_linear_output,
        settings.transmission_source == ForwardPbrTransmissionSource::Hybrid,
        request.scene_readback, temporal_frame, rigid_motion_draws, settings.frame_time_ms);
    const Clock::time_point render_graph_end =
        metrics != nullptr ? Clock::now() : Clock::time_point{};
    if (metrics != nullptr) {
        metrics->render_graph_build_compile_milliseconds =
            std::chrono::duration<double, std::milli>(render_graph_end - render_graph_start)
                .count();
        metrics->render_graph = render_graph.graph.metrics();
    }
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
            .metrics = metrics != nullptr ? &metrics->render_graph_frame : nullptr,
        },
        render_graph.graph,
        [this, device = target.device, frame_slot = target.frame_slot, cloud_runtime, &render_graph,
         hybrid = settings.transmission_source == ForwardPbrTransmissionSource::Hybrid,
         linear =
             settings.opaque_linear_output](const render::RenderGraphResourceSet& graph_resources) {
            if (render_graph.temporal_frame.has_value()) {
                if (swapchain_.upscaler) {
                    swapchain_.upscale_motion->update_descriptors(
                        *device, frame_slot, render_graph.graph, graph_resources,
                        render_graph.scene_depth, render_graph.temporal_motion);
                } else {
                    swapchain_.temporal_aa->update_descriptors(
                        *device, render_graph.graph, graph_resources, *render_graph.temporal_frame);
                }
            }
            if (!linear)
                update_post_descriptor(*device, frame_slot, render_graph.graph, graph_resources,
                                       render_graph.post_scene_color);
            if (render_graph.has_transmission) {
                const render::RenderGraphSampledTextureView source =
                    render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                          render_graph.post_scene_color);
                refraction_pyramid().update_source_descriptor(
                    *device, frame_slot, post_sampler().handle(), source.view, source.layout,
                    hybrid ? depth_attachment().view() : VK_NULL_HANDLE);
            }
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
    swapchain_.scene_depth_state = scene_depth_final_state(settings.opaque_linear_output);
    if (swapchain_.temporal_aa.has_value() && swapchain_.temporal_aa->created()) {
        temporal_status_.resources = swapchain_.temporal_aa->metrics();
    }
}

ForwardPbrRenderer3D::Impl::CompiledGraph ForwardPbrRenderer3D::Impl::current_render_graph(
    render::ColorTargetView color_target, render::FrameSlot frame_slot,
    render::RenderGraphTextureState color_initial_state,
    render::RenderGraphTextureState color_final_state,
    const ForwardPbrRenderer3DFramePlans& frame_plans,
    const render::MeshResourceTable<render::Mesh>& meshes,
    const render::FrameMeshResourceTable* frame_meshes,
    std::span<const render::GpuDeformationCommand> deformation_commands,
    const render::PbrMaterialTable& materials, render::PbrDebugView debug_view,
    ForwardPbrRenderer3DBackgroundMode background_mode,
    const std::optional<ForwardPbrRenderer3DAtmosphereClouds>& clouds,
    const std::optional<ForwardPbrRenderer3DTerrainBackdrop>& terrain,
    const std::optional<ForwardPbrRenderer3DOceanSurface>& ocean,
    const ForwardPbrDrawPlan& draw_plan, bool opaque_linear_output, bool hybrid_transmission,
    const std::optional<ForwardPbrRenderer3DSceneReadback>& readback,
    const std::optional<render::TemporalFrame>& temporal_frame,
    std::span<const ForwardPbrTemporalRigidMotionDraw> rigid_motion_draws,
    float frame_time_ms) {
    if (frame_plans.shadow == nullptr || frame_plans.scene == nullptr) {
        throw std::runtime_error("forward PBR render graph requires shadow and scene plans");
    }
    const scene::RenderFramePlan3D& shadow_plan = *frame_plans.shadow;
    const scene::RenderFramePlan3D& scene_plan = *frame_plans.scene;
    const bool has_transmission = draw_plan.has_transmission();
    render::RenderGraphBuilder graph;
    const render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
        "backbuffer", color_target, color_initial_state, color_final_state);
    const render::RenderGraphTextureHandle scene_color =
        opaque_linear_output
            ? backbuffer
            : graph.create_texture(render::RenderGraphTextureDesc{
                  .label = "scene color",
                  .extent = {swapchain_.render_extent.width, swapchain_.render_extent.height, 1},
                  .format = config_.scene_color_format,
                  .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
              });
    // Persistent depth imports carry prior access/layout even when this frame
    // clears the attachment contents.
    const render::RenderGraphTextureHandle scene_depth = graph.import_depth_target(
        "scene depth", render::depth_target_view(depth_attachment()), swapchain_.scene_depth_state,
        scene_depth_final_state(opaque_linear_output));
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

    if (ocean.has_value()) {
        graph.add_pass("ocean update", render::RenderGraphQueueDomain::Compute)
            .execute([runtime = ocean->runtime,
                      frame_slot](const render::RenderGraphExecutionContext& context) {
                runtime->record_update(context.recorder(), frame_slot);
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
    shadow_pass_builder.execute([this, frame_slot, &shadow_plan, &draw_plan, mesh_resolver,
                                 &materials](const render::RenderGraphExecutionContext& context) {
        record_shadow_pass(context.recorder(), shadow_plan, frame_slot, mesh_resolver, materials,
                           draw_plan);
    });
    if (!has_transmission) {
        // Preserve the original graph shape and work for all existing material
        // packets. The staged path below is opt-in once a material explicitly
        // declares optical transmission.
        auto scene_pass_builder = graph.add_pass("scene", render::RenderGraphQueueDomain::Graphics)
                                      .read_texture(shadow_depth)
                                      .write_color(scene_color)
                                      .write_depth(scene_depth)
                                      .material_pass(render::pbr_forward_pass_info());
        if (terrain.has_value()) {
            scene_pass_builder.read_texture(terrain_shadow_depth);
        }
        declare_deformation_vertex_reads(scene_pass_builder, deformation_vertex_buffers);
        scene_pass_builder.execute(
            [this, scene_color, frame_slot, &scene_plan, &draw_plan, mesh_resolver, &materials,
             debug_view, background_mode,
             terrain_runtime = terrain.has_value() ? terrain->runtime : nullptr,
             ocean_runtime = ocean.has_value() ? ocean->runtime : nullptr,
             preserve_depth = clouds_enabled || opaque_linear_output || temporal_frame.has_value()](
                const render::RenderGraphExecutionContext& context) {
                const render::ColorTargetView target =
                    render::resolved_color_target_view(context, scene_color);
                record_scene_pass(context.recorder(), target, scene_plan, frame_slot, mesh_resolver,
                                  materials, draw_plan, debug_view, background_mode,
                                  terrain_runtime, ocean_runtime, preserve_depth);
            });
    } else {
        auto opaque_pass_builder =
            graph.add_pass("opaque", render::RenderGraphQueueDomain::Graphics)
                .read_texture(shadow_depth)
                .write_color(scene_color)
                .write_depth(scene_depth)
                .material_pass(render::pbr_forward_pass_info());
        if (terrain.has_value()) {
            opaque_pass_builder.read_texture(terrain_shadow_depth);
        }
        declare_deformation_vertex_reads(opaque_pass_builder, deformation_vertex_buffers);
        opaque_pass_builder.execute(
            [this, scene_color, frame_slot, &scene_plan, &draw_plan, mesh_resolver, &materials,
             debug_view, background_mode,
             terrain_runtime = terrain.has_value() ? terrain->runtime : nullptr,
             ocean_runtime = ocean.has_value() ? ocean->runtime : nullptr](
                const render::RenderGraphExecutionContext& context) {
                const render::ColorTargetView target =
                    render::resolved_color_target_view(context, scene_color);
                record_scene_opaque_pass(context.recorder(), target, scene_plan, frame_slot,
                                         mesh_resolver, materials, draw_plan, debug_view,
                                         background_mode, terrain_runtime, ocean_runtime);
            });
    }
    if (clouds_enabled) {
        clouds->runtime->declare_surface_composite(graph, cloud_scene_color, cloud_frame,
                                                   frame_slot, scene_color, scene_depth);
        post_scene_color = cloud_scene_color;
    }
    if (has_transmission) {
        if (hybrid_transmission) {
            // Sample opaque depth before rebinding it for the alpha composite.
            // Splitting these stages avoids depth attachment feedback.
            graph.add_pass("refraction geometry coverage", render::RenderGraphQueueDomain::Graphics)
                .read_texture(post_scene_color)
                .read_texture(scene_depth)
                .material_pass(render::hdr_color_pyramid_filter_pass_info())
                .execute([this, frame_slot](const render::RenderGraphExecutionContext& context) {
                    refraction_pyramid().record_source_copy(context.recorder(), frame_slot, true);
                });
        }
        auto refraction_source_pass =
            graph.add_pass("refraction source", render::RenderGraphQueueDomain::Graphics)
                .read_texture(post_scene_color)
                .write_depth(scene_depth)
                .material_pass(render::pbr_forward_pass_info(
                    {.blend = render::MaterialBlendMode::AlphaBlend}));
        declare_deformation_vertex_reads(refraction_source_pass, deformation_vertex_buffers);
        refraction_source_pass.execute(
            [this, frame_slot, &scene_plan, &draw_plan, mesh_resolver, &materials,
             hybrid_transmission](const render::RenderGraphExecutionContext& context) {
                // Mip zero is an isolated radiance source. Ordinary alpha is
                // composited here for transmission lookup and later once into
                // the final scene after transmissive geometry.
                if (!hybrid_transmission)
                    refraction_pyramid().record_source_copy(context.recorder(), frame_slot);
                record_scene_alpha_pass(context.recorder(),
                                        refraction_pyramid().source_target(frame_slot), scene_plan,
                                        frame_slot, mesh_resolver, materials, draw_plan);
                refraction_pyramid().record_remaining_mips(context.recorder(), frame_slot);
            });
        graph.add_pass("transmission", render::RenderGraphQueueDomain::Graphics)
            .read_write_color(post_scene_color)
            .write_depth(scene_depth)
            .material_pass(render::pbr_forward_pass_info())
            .execute([this, post_scene_color, frame_slot, &scene_plan, &draw_plan, mesh_resolver,
                      &materials](const render::RenderGraphExecutionContext& context) {
                const render::ColorTargetView target =
                    render::resolved_color_target_view(context, post_scene_color);
                record_transmission_stage(context.recorder(), target, scene_plan, frame_slot,
                                          mesh_resolver, materials, draw_plan);
            });
        auto alpha_pass_builder = graph.add_pass("alpha", render::RenderGraphQueueDomain::Graphics)
                                      .read_write_color(post_scene_color)
                                      .write_depth(scene_depth)
                                      .material_pass(render::pbr_forward_pass_info(
                                          {.blend = render::MaterialBlendMode::AlphaBlend}));
        declare_deformation_vertex_reads(alpha_pass_builder, deformation_vertex_buffers);
        alpha_pass_builder.execute(
            [this, post_scene_color, frame_slot, &scene_plan, &draw_plan, mesh_resolver,
             &materials](const render::RenderGraphExecutionContext& context) {
                const render::ColorTargetView target =
                    render::resolved_color_target_view(context, post_scene_color);
                record_scene_alpha_pass(context.recorder(), target, scene_plan, frame_slot,
                                        mesh_resolver, materials, draw_plan);
            });
    }
    render::RenderGraphTextureHandle temporal_motion{};
    if (temporal_frame.has_value()) {
        temporal_motion = swapchain_.upscaler
                              ? swapchain_.upscale_motion->declare(graph, frame_slot, scene_depth,
                                                                  *temporal_frame)
                              : swapchain_.temporal_aa->declare_camera_motion(graph, scene_depth,
                                                                             *temporal_frame);
        if (!rigid_motion_draws.empty()) {
            const render::FrameUniformMaterialInstance<ForwardPbrTemporalRigidMotionUniforms>&
                material = *swapchain_.temporal_rigid_motion_material;
            const vulkan::Buffer& uniforms = material.uniforms().buffer(frame_slot);
            const render::RenderGraphBufferHandle uniform_buffer = graph.import_buffer(
                {.label = "forward PBR rigid-motion uniforms", .byte_size = uniforms.size()},
                uniforms.handle());
            graph.add_pass("rigid object motion", render::RenderGraphQueueDomain::Graphics)
                .read_uniform_buffer(uniform_buffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                .read_write_color(temporal_motion)
                .write_depth(scene_depth)
                .material_pass(forward_pbr_temporal_rigid_motion_pass_info(
                    "forward_pbr.taa.rigid_motion", VK_CULL_MODE_BACK_BIT))
                .execute([this, temporal_motion, scene_depth, frame_slot, mesh_resolver,
                          rigid_motion_draws](const render::RenderGraphExecutionContext& context) {
                    const render::ColorTargetView motion_target =
                        render::resolved_color_target_view(context, temporal_motion);
                    const render::DepthTargetView depth_target =
                        render::resolved_depth_target_view(context, scene_depth);
                    record_rigid_motion_pass(context.recorder(), motion_target, depth_target,
                                             frame_slot, mesh_resolver, rigid_motion_draws);
                });
        }
        if (swapchain_.upscaler) {
            post_scene_color = swapchain_.upscaler->declare(
                graph, {.color = post_scene_color,
                        .depth = scene_depth,
                        .motion = temporal_motion,
                        .view = *temporal_frame,
                        .output_extent = color_target.extent,
                        .frame_time_ms = frame_time_ms});
        } else {
            post_scene_color = swapchain_.temporal_aa->declare_resolve(
                graph, post_scene_color, temporal_motion, *temporal_frame);
        }
    }
    if (readback) {
        const auto buffer = graph.import_buffer(
            {.label = "scene HDR readback", .byte_size = readback->byte_size}, readback->buffer,
            render::RenderGraphBufferState{.access_mask = VK_ACCESS_HOST_READ_BIT,
                                           .stage_mask = VK_PIPELINE_STAGE_HOST_BIT},
            render::RenderGraphBufferState{.access_mask = VK_ACCESS_HOST_READ_BIT,
                                           .stage_mask = VK_PIPELINE_STAGE_HOST_BIT});
        graph.add_pass("scene HDR readback", render::RenderGraphQueueDomain::Transfer)
            .transfer_read_texture(post_scene_color)
            .transfer_write_buffer(buffer)
            .execute([post_scene_color, buffer, extent = color_target.extent](
                         const render::RenderGraphExecutionContext& context) {
                const VkBufferImageCopy copy{
                    .bufferOffset = 0,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageOffset = {0, 0, 0},
                    .imageExtent = {extent.width, extent.height, 1}};
                vkCmdCopyImageToBuffer(context.recorder().handle(),
                                       context.resolved_texture(post_scene_color).image,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       context.resolved_buffer(buffer).buffer, 1, &copy);
            });
    }
    if (!opaque_linear_output)
        graph.add_pass("post", render::RenderGraphQueueDomain::Graphics)
            .read_texture(post_scene_color)
            .write_color(backbuffer)
            .material_pass(render::pbr_post_pass_info())
            .execute([this, color_target,
                      frame_slot](const render::RenderGraphExecutionContext& context) {
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
        .has_transmission = has_transmission,
        .temporal_frame = temporal_frame,
        .temporal_motion = temporal_motion,
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
