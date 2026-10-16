#include "ocean_app_internal.h"

namespace cubey::projects::ocean::detail {

void OceanApp::record_atmosphere_background(const cubey::vulkan::CommandRecorder& recorder,
                                            VkExtent2D extent,
                                            cubey::render::FrameSlot frame_slot) const {
    const OceanSurfaceFrame surface_frame = ocean_surface_frame();
    atmosphere_background_.upload(frame_slot,
                                  atmosphere_background_uniforms(extent, surface_frame));
    cubey::render::record_fullscreen_pipeline_draw(
        recorder, cubey::render::FullscreenPipelineDrawInfo{
                      .pipeline = &atmosphere_background_.pipeline(),
                      .descriptor_set = atmosphere_background_.material().set(frame_slot),
                      .descriptor_set_index = 0,
                  });
}

void OceanApp::record_ocean_draw(const cubey::vulkan::CommandRecorder& recorder, VkExtent2D extent,
                                 cubey::render::FrameSlot frame_slot,
                                 const cubey::render::CloudLayerShadowProduct* cloud_shadow) const {
    const OceanMeshDrawPlan draw_plan = ocean_mesh_draw_plan(extent);
    ocean_gpu_.upload_surface_feature_uniforms(
        frame_slot,
        surface_feature_uniforms(draw_plan.surface_frame, cloud_shadow, frame_slot));
    const cubey::render::GraphicsPipelineResource* bound_pipeline = nullptr;
    for (std::size_t index = 0; index < draw_plan.patches.count; ++index) {
        if (!draw_plan.visible_patches[index]) {
            continue;
        }
        const OceanMeshPatch& patch = draw_plan.patches.patches[index];
        const OceanPatchShadingPlan& shading_plan = draw_plan.shading_plans[index];
        const cubey::render::GraphicsPipelineResource& surface_pipeline =
            ocean_gpu_.surface_pipeline(shading_plan.detail_filter);
        if (bound_pipeline != &surface_pipeline) {
            recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   surface_pipeline.pipeline());
            recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                         surface_pipeline.layout(), 0,
                                         ocean_gpu_.surface_set(frame_slot));
            bound_pipeline = &surface_pipeline;
        }
        const OceanPushConstants constants =
            surface_push_constants(extent, draw_plan.surface_frame, patch, shading_plan);
        recorder.push_constants(surface_pipeline.layout(),
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                constants);
        recorder.draw(ocean_mesh_patch_vertex_count(patch));
    }
}

void OceanApp::record_reference_pillar_draw(const cubey::vulkan::CommandRecorder& recorder,
                                            VkExtent2D extent) const {
    if (!diagnostics_.size_reference_enabled || render_view_ != OceanRenderView::Final) {
        return;
    }
    if (!reference_pillar_pipeline_.has_value() || !reference_pillar_mesh_.has_value()) {
        throw std::runtime_error("ocean reference pillar resources are not initialized");
    }
    const cubey::render::GraphicsPipelineResource& pipeline =
        reference_pillar_pipeline_.value();
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
    recorder.push_constants(pipeline.layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            reference_pillar_push_constants(extent));
    cubey::render::record_draw_item(recorder.handle(),
                                    {
                                        .mesh = &reference_pillar_mesh_.value(),
                                    });
}

[[nodiscard]] cubey::render::PbrPostUniforms OceanApp::post_uniforms() const {
    return cubey::render::hdr_post_uniforms(pipeline_color_format_, display_exposure());
}

[[nodiscard]] float OceanApp::display_exposure() const {
    if (atmosphere_state_.auto_exposure_enabled && !config_.pbr.exposure_explicit) {
        return atmosphere_state_.resolved_exposure;
    }
    return ocean_config_.exposure;
}

void OceanApp::record_ocean_post(const cubey::vulkan::CommandRecorder& recorder,
                                 cubey::render::ColorTargetView target,
                                 cubey::render::FrameSlot frame_slot) const {
    hdr_post_frame_.record_pass(recorder, target, frame_slot);
}

[[nodiscard]] OceanFrameGraph OceanApp::build_ocean_frame_graph(
    cubey::render::ColorTargetView color_target, cubey::render::FrameSlot frame_slot,
    OceanRenderTargetMode target_mode,
    const std::optional<cubey::CloudEnvironmentRuntimeFrame>& cloud_runtime_frame) const {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureState initial_state =
        target_mode == OceanRenderTargetMode::Present
            ? cubey::render::render_graph_undefined_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureState final_state =
        target_mode == OceanRenderTargetMode::Present
            ? cubey::render::render_graph_present_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("ocean backbuffer", color_target, initial_state, final_state);
    const cubey::render::RenderGraphTextureHandle scene_color =
        graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
            "ocean scene color", color_target.extent, kOceanSceneColorFormat));
    cubey::render::RenderGraphTextureHandle post_scene_color = scene_color;
    cubey::render::RenderGraphTextureHandle cloud_scene_color{};
    cubey::render::CloudLayerRuntimeFrame cloud_frame{};
    cubey::render::CloudLayerShadowProduct cloud_shadow{};
    const cubey::render::RenderGraphTextureHandle surface_depth =
        graph.create_texture(ocean_depth_texture_desc("ocean surface depth",
                                                      color_target.extent, kOceanDepthFormat));
    const bool cloud_product_enabled = cloud_products_enabled() &&
                                       cloud_runtime_frame.has_value() &&
                                       cloud_runtime_frame->enabled;
    const bool cloud_shadow_enabled =
        cloud_product_enabled && (ocean_config_.cloud_shadow_strength > 0.0F ||
                                  render_view_ == OceanRenderView::CloudShadow);
    const bool visible_clouds_enabled = cloud_composite_enabled() &&
                                        cloud_runtime_frame.has_value() &&
                                        cloud_runtime_frame->enabled;
    const bool cloud_march_enabled = visible_clouds_enabled;
    const OceanSurfaceFrame surface_frame = ocean_surface_frame();
    const cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    if (cloud_shadow_enabled) {
        cloud_shadow = clouds.declare_shadow_product(graph, frame_slot,
                                                     cloud_shadow_request(surface_frame));
    }
    if (cloud_march_enabled) {
        cloud_frame =
            clouds.declare_surface_product(graph, frame_slot, cloud_runtime_frame.value());
    }
    if (visible_clouds_enabled) {
        cloud_scene_color = graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
            "ocean cloud scene color", color_target.extent, kOceanSceneColorFormat));
    }

    auto surface_pass =
        graph.add_pass("ocean scene", cubey::render::RenderGraphQueueDomain::Graphics);
    if (cloud_shadow_enabled) {
        surface_pass.read_texture(cloud_shadow.transmittance,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    surface_pass.write_color(scene_color)
        .write_depth(surface_depth)
        .execute(
            [this, scene_color, frame_slot, surface_depth, cloud_shadow,
             cloud_shadow_enabled](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView depth =
                    cubey::render::resolved_depth_target_view(context, surface_depth);
                cubey::render::record_render_target_pass(
                    context.recorder(), cubey::render::render_target_view(target, depth),
                    cubey::render::RenderClearValues{
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
                    [this, target, frame_slot, cloud_shadow,
                     cloud_shadow_enabled](const cubey::vulkan::CommandRecorder& recorder) {
                        record_atmosphere_background(recorder, target.extent, frame_slot);
                        if (render_view_ != OceanRenderView::Background) {
                            record_ocean_draw(recorder, target.extent, frame_slot,
                                              cloud_shadow_enabled ? &cloud_shadow : nullptr);
                            record_reference_pillar_draw(recorder, target.extent);
                        }
                    });
            });
    if (visible_clouds_enabled) {
        clouds.declare_surface_composite(graph, cloud_scene_color, cloud_frame, frame_slot,
                                         scene_color, surface_depth);
        post_scene_color = cloud_scene_color;
    }
    graph.add_pass("ocean post", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(post_scene_color)
        .write_color(backbuffer)
        .material_pass(cubey::render::pbr_post_pass_info())
        .execute([this, color_target,
                  frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
            record_ocean_post(context.recorder(), color_target, frame_slot);
        });

    return {
        .graph = graph.compile(),
        .backbuffer = backbuffer,
        .post_scene_color = post_scene_color,
        .scene_color = scene_color,
        .surface_depth = surface_depth,
        .cloud_scene_color = cloud_scene_color,
        .cloud = cloud_frame,
        .cloud_shadow = cloud_shadow,
        .cloud_product_enabled = cloud_march_enabled,
        .cloud_shadow_enabled = cloud_shadow_enabled,
        .cloud_composite_enabled = visible_clouds_enabled,
    };
}

void OceanApp::update_post_descriptor(const cubey::vulkan::Device& device,
                                      cubey::render::FrameSlot frame_slot,
                                      const cubey::render::CompiledRenderGraph& graph,
                                      const cubey::render::RenderGraphResourceSet& resources,
                                      cubey::render::RenderGraphTextureHandle scene_color) const {
    hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, graph, resources,
                                                  scene_color);
}

void OceanApp::record_initial_texture_transitions(
    const cubey::vulkan::CommandRecorder& recorder) const {
    recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
        ocean_gpu_.fallback_field().handle()));
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (!ocean_gpu_.cascade_allocated(cascade)) {
            continue;
        }
        recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
            ocean_gpu_.h0(cascade).handle()));
        for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
            recorder.transition_image_layout(
                cubey::vulkan::begin_storage_image_write_transition(
                    ocean_gpu_.field(cascade, field).handle()));
            recorder.transition_image_layout(
                cubey::vulkan::begin_storage_image_write_transition(
                    ocean_gpu_.ping(cascade, field).handle()));
            recorder.transition_image_layout(
                cubey::vulkan::begin_storage_image_write_transition(
                    ocean_gpu_.pong(cascade, field).handle()));
        }
        recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
            ocean_gpu_.displacement(cascade).handle()));
        recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
            ocean_gpu_.normal(cascade).handle()));
        recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
            ocean_gpu_.foam(cascade).handle()));
    }
}

void OceanApp::record_spectrum_init(const cubey::vulkan::CommandRecorder& recorder,
                                    cubey::render::FrameSlot frame_slot) {
    cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_, cascade);
        const std::string profile_label = ocean_gpu_timing_label("spectrum", cascade);
        cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                               profile_label);
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                ocean_gpu_.spectrum_pipeline(), ocean_gpu_.spectrum_set(cascade), groups),
            VK_SHADER_STAGE_COMPUTE_BIT, spectrum_push_constants(cascade));
    }
}

void OceanApp::record_modulate(const cubey::vulkan::CommandRecorder& recorder,
                               cubey::render::FrameSlot frame_slot) {
    cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_, cascade);
        const std::string profile_label = ocean_gpu_timing_label("modulate", cascade);
        cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                               profile_label);
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                ocean_gpu_.modulate_pipeline(), ocean_gpu_.modulate_set(cascade), groups),
            VK_SHADER_STAGE_COMPUTE_BIT, modulate_push_constants(cascade));
    }
}

void OceanApp::record_fft_pass(const cubey::vulkan::CommandRecorder& recorder,
                               std::uint32_t cascade, std::uint32_t field, std::uint32_t stage,
                               bool horizontal, bool first_pass,
                               std::uint32_t descriptor_set_index) const {
    const cubey::render::ComputeDispatchGroups groups =
        ocean_dispatch_groups(ocean_config_, cascade);
    cubey::render::record_compute_pipeline_dispatch(
        recorder,
        cubey::render::compute_pipeline_dispatch_info(
            ocean_gpu_.fft_pipeline(), ocean_gpu_.fft_set(cascade, field, descriptor_set_index),
            groups),
        VK_SHADER_STAGE_COMPUTE_BIT,
        fft_push_constants(cascade, stage, horizontal, first_pass));
}

void OceanApp::record_fft(const cubey::vulkan::CommandRecorder& recorder,
                          cubey::render::FrameSlot frame_slot) {
    cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        const std::uint32_t stage_count =
            log2_exact(ocean_cascade_map_size(ocean_config_, cascade));
        const std::string profile_label = ocean_gpu_timing_label("fft", cascade);
        cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                               profile_label);
        for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
            bool source_is_ping = true;
            for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                const std::uint32_t set_index = stage == 1U ? 0U : (source_is_ping ? 1U : 2U);
                record_fft_pass(recorder, cascade, field, stage, true, stage == 1U, set_index);
                cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                source_is_ping = stage == 1U ? true : !source_is_ping;
            }

            for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                const std::uint32_t set_index = source_is_ping ? 1U : 2U;
                record_fft_pass(recorder, cascade, field, stage, false, stage == 1U, set_index);
                cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                source_is_ping = !source_is_ping;
            }
        }
    }
}

void OceanApp::record_unpack(const cubey::vulkan::CommandRecorder& recorder,
                             cubey::render::FrameSlot frame_slot) {
    cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_, cascade);
        const std::string profile_label = ocean_gpu_timing_label("unpack", cascade);
        cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                               profile_label);
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                ocean_gpu_.unpack_pipeline(), ocean_gpu_.unpack_set(cascade), groups),
            VK_SHADER_STAGE_COMPUTE_BIT, unpack_push_constants(cascade));
    }
}

void OceanApp::record_ocean_compute(const cubey::vulkan::CommandRecorder& recorder,
                                    cubey::render::FrameSlot frame_slot) {
    if (!textures_initialized_) {
        record_initial_texture_transitions(recorder);
        textures_initialized_ = true;
    }
    if (!spectrum_initialized_) {
        record_spectrum_init(recorder, frame_slot);
        cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
        spectrum_initialized_ = true;
    }
    record_modulate(recorder, frame_slot);
    cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
    record_fft(recorder, frame_slot);
    record_unpack(recorder, frame_slot);
    cubey::vulkan::record_shader_write_barrier(recorder.handle(),
                                               VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_ACCESS_SHADER_READ_BIT);
    foam_initialized_ = true;
    ++ocean_compute_frame_index_;
}

void OceanApp::record_atmosphere_environment_if_needed(
    const cubey::vulkan::CommandRecorder& recorder, cubey::render::FrameSlot frame_slot) {
    if (!atmosphere_runtime_.resources_created()) {
        throw std::runtime_error("ocean atmosphere runtime is not initialized");
    }
    atmosphere_runtime_.record_pending_update(recorder, frame_slot);
}

void OceanApp::record_cloud_environment_if_needed(
    const cubey::vulkan::CommandRecorder& recorder, cubey::render::FrameSlot frame_slot,
    const cubey::CloudEnvironmentRuntimeFrame& frame) {
    if (!cloud_environment_active()) {
        return;
    }
    static_cast<void>(
        atmosphere_runtime_.clouds().record_pending_update(recorder, frame_slot, frame));
}

void OceanApp::record_cloud_planar_reflection_if_needed(
    const cubey::vulkan::CommandRecorder& recorder, cubey::render::FrameSlot frame_slot,
    const cubey::CloudEnvironmentRuntimeFrame& frame) {
    if (!cloud_planar_reflection_active()) {
        return;
    }
    const OceanSurfaceFrame surface_frame = ocean_surface_frame();
    cloud_planar_reflection_.record(
        recorder, cubey::render::CloudPlanarReflectionRequest{
                      .frame_slot = frame_slot,
                      .cloud = frame.layer,
                      .frame = frame.view,
                      .plane_point = {0.0F, surface_frame.local_frame.water_datum_m, 0.0F},
                      .plane_normal = {0.0F, 1.0F, 0.0F},
                  });
}

void OceanApp::update_cloud_environment_descriptors(const cubey::vulkan::Device& device,
                                                    cubey::render::FrameSlot frame_slot) {
    const cubey::render::CloudEnvironmentProbeSnapshot snapshot =
        atmosphere_runtime_.clouds().snapshot();
    if (snapshot.valid) {
        ocean_gpu_.update_cloud_environment_descriptors(device, frame_slot, *snapshot.previous,
                                                        *snapshot.current);
        return;
    }
    const cubey::render::AtmosphereReflectionProbeSnapshot fallback =
        atmosphere_runtime_.reflection_probe().snapshot();
    ocean_gpu_.update_cloud_environment_descriptors(device, frame_slot, *fallback.current,
                                                    *fallback.current);
}

void OceanApp::update_atmosphere_environment_descriptors(const cubey::vulkan::Device& device,
                                                         cubey::render::FrameSlot frame_slot) {
    const cubey::render::AtmosphereReflectionProbe& probe =
        atmosphere_runtime_.reflection_probe();
    const cubey::render::AtmosphereReflectionProbeSnapshot snapshot = probe.snapshot();
    ocean_gpu_.update_atmosphere_probe_descriptors(
        device, frame_slot, *snapshot.previous, *snapshot.current, probe.sky_radiance_cube());
}

void OceanApp::record_ocean_target(VkCommandBuffer command_buffer,
                                   const cubey::vulkan::Device& device,
                                   cubey::render::ColorTargetView target,
                                   cubey::render::FrameSlot frame_slot,
                                   OceanRenderTargetMode target_mode) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    if (cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler()) {
        profiler->begin_frame(command_buffer, frame_slot.index);
    }
    std::optional<cubey::CloudEnvironmentRuntimeFrame> cloud_frame;
    if (cloud_products_enabled()) {
        cloud_frame = current_cloud_frame(target.extent, ocean_surface_frame());
        atmosphere_runtime_.clouds().upload_surface_frame(frame_slot, cloud_frame.value());
    }
    {
        cubey::vulkan::GpuTimestampScope scope(ocean_gpu_.profiler(), command_buffer,
                                               frame_slot.index, "ocean.atmosphere");
        record_atmosphere_environment_if_needed(recorder, frame_slot);
    }
    update_atmosphere_environment_descriptors(device, frame_slot);
    {
        cubey::vulkan::GpuTimestampScope scope(ocean_gpu_.profiler(), command_buffer,
                                               frame_slot.index, "ocean.cloud_environment");
        if (cloud_frame.has_value()) {
            record_cloud_environment_if_needed(recorder, frame_slot, cloud_frame.value());
        }
    }
    update_cloud_environment_descriptors(device, frame_slot);
    {
        cubey::vulkan::GpuTimestampScope scope(ocean_gpu_.profiler(), command_buffer,
                                               frame_slot.index,
                                               "ocean.cloud_planar_reflection");
        if (cloud_frame.has_value()) {
            record_cloud_planar_reflection_if_needed(recorder, frame_slot, cloud_frame.value());
        }
    }
    update_cloud_planar_reflection_descriptor(device, frame_slot);
    record_ocean_compute(recorder, frame_slot);
    upload_terrain_ocean_field_uniform(frame_slot);
    hdr_post_frame_.upload(frame_slot, post_uniforms());
    const OceanFrameGraph frame_graph =
        build_ocean_frame_graph(target, frame_slot, target_mode, cloud_frame);
    graph_executor_.record(
        cubey::render::RenderGraphFrameRecordInfo{
            .device = &device,
            .command_buffer = command_buffer,
            .frame_slot = frame_slot,
            .label = "vkEndCommandBuffer ocean graph",
            .command_buffer_mode =
                cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            .profiler = ocean_gpu_.profiler(),
        },
        frame_graph.graph,
        [this, &device, frame_slot,
         &frame_graph](const cubey::render::RenderGraphResourceSet& resources) {
            update_post_descriptor(device, frame_slot, frame_graph.graph, resources,
                                   frame_graph.post_scene_color);
            if (frame_graph.cloud_shadow_enabled) {
                atmosphere_runtime_.clouds().update_shadow_descriptors(
                    device, frame_slot, frame_graph.graph, resources, frame_graph.cloud_shadow);
                const cubey::render::RenderGraphSampledTextureView transmittance =
                    cubey::render::resolved_sampled_texture_view(
                        frame_graph.graph, resources, frame_graph.cloud_shadow.transmittance);
                ocean_gpu_.update_cloud_shadow_descriptor(
                    device, frame_slot, atmosphere_runtime_.clouds().shadow_sampler().handle(),
                    transmittance.view, transmittance.layout);
            } else {
                update_cloud_shadow_fallback_descriptor(device, frame_slot);
            }
            if (frame_graph.cloud_product_enabled) {
                atmosphere_runtime_.clouds().update_surface_descriptors(
                    device, frame_slot, frame_graph.graph, resources, frame_graph.cloud,
                    frame_graph.scene_color, frame_graph.surface_depth);
            }
        });
    if (frame_graph.cloud_product_enabled) {
        atmosphere_runtime_.clouds().complete_surface_frame(frame_slot, frame_graph.cloud);
    }
}

void OceanApp::record_windowed_frame(cubey::host::WindowedAppContext& context,
                                     const cubey::host::WindowedRenderFrame& frame) {
    collect_gpu_timings(context.profile_recorder(), windowed_frame_index_, frame.frame_slot,
                        time_seconds_);
    record_ocean_profile_metrics(context.profile_recorder(), windowed_frame_index_,
                                 frame.color_target.extent);
    const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
    recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    record_ocean_target(frame.command_buffer, context.device(), frame.color_target,
                        frame.frame_slot, OceanRenderTargetMode::Present);
    recorder.end("vkEndCommandBuffer ocean");
    ++windowed_frame_index_;
}
} // namespace cubey::projects::ocean::detail
