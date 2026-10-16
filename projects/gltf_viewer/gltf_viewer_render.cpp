#include "gltf_viewer_app_internal.h"

#include <cubey/vulkan/memory_barriers.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <vector>

namespace cubey::projects::gltf_viewer {
namespace {

[[nodiscard]] std::uint64_t
collected_profile_frame_index(std::uint64_t frame_index,
                              cubey::render::FrameSlot frame_slot) noexcept {
    if (frame_index > frame_slot.count) {
        return frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return frame_index == 0U ? 0U : frame_index - 1U;
}

} // namespace

void GltfViewerApp::create_frame_resources(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu, VkExtent2D extent,
                                           VkFormat color_format, std::uint32_t frame_slot_count) {
    forward_pbr_renderer().create_swapchain_resources(
        device, cubey::ForwardPbrRenderer3DTargetResourcesInfo{
                    .extent = extent,
                    .color_format = color_format,
                    .materials = &active_generation().import_resources.materials,
                });
    if (landscape_runtime_) {
        landscape_foreground_.emplace(
            device, cubey::render::Texture2DConfig{
                        .extent = extent,
                        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                        .usage = cubey::render::Texture2DUsage::ColorAttachmentSampled,
                        .sampler = {}});
        landscape_runtime_->ensure_resources(device, gpu, frame_slot_count,
                                             {.extent = extent, .format = color_format}, true);
    }
    if (terrain_backdrop_enabled() && active_generation().terrain_surface.has_value() &&
        terrain_runtime_.product_ready()) {
        const cubey::ForwardPbrRenderer3DSceneTargetInfo target =
            forward_pbr_renderer().scene_target_info();
        terrain_target_info_ = cubey::TerrainBackdropRuntimeTargetInfo{
            .extent = target.extent,
            .color_format = target.color_format,
            .depth_format = target.depth_format,
        };
        terrain_runtime_.create_target_resources(device, terrain_target_info_.value());
    } else if (terrain_backdrop_enabled()) {
        const cubey::ForwardPbrRenderer3DSceneTargetInfo target =
            forward_pbr_renderer().scene_target_info();
        terrain_target_info_ = cubey::TerrainBackdropRuntimeTargetInfo{
            .extent = target.extent,
            .color_format = target.color_format,
            .depth_format = target.depth_format,
        };
    }
    if (ocean_backdrop_enabled()) {
        if (!environment_policy().uses_atmosphere_resources()) {
            throw std::runtime_error("ocean backdrop requires --pbr-environment-source atmosphere");
        }
        const cubey::ForwardPbrRenderer3DSceneTargetInfo target =
            forward_pbr_renderer().scene_target_info();
        ocean_runtime_.create(device, {
                                          .ocean = ocean_config_,
                                          .shader_dir = CUBEY_GLTF_VIEWER_SHADER_DIR,
                                          .color_format = target.color_format,
                                          .depth_format = target.depth_format,
                                          .target_extent = target.extent,
                                          .frame_slot_count = frame_slot_count,
                                      });
        for (std::uint32_t index = 0U; index < frame_slot_count; ++index) {
            update_ocean_environment_descriptors(device,
                                                 {.index = index, .count = frame_slot_count});
        }
    }
    if (environment_policy().uses_atmosphere_resources()) {
        atmosphere_runtime_.clouds().create_surface_target_resources(
            device,
            cubey::render::cloud_layer_runtime_shader_files(
                CUBEY_GLTF_VIEWER_SHADER_DIR,
                cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth),
            cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
            VK_FORMAT_R16G16B16A16_SFLOAT, extent, frame_slot_count);
    }
}

void GltfViewerApp::destroy_swapchain_resources() {
    if (landscape_runtime_)
        landscape_runtime_->destroy_target_resources();
    landscape_foreground_.reset();
    ocean_runtime_.reset();
    terrain_runtime_.destroy_target_resources();
    terrain_target_info_.reset();
    if (environment_policy().uses_atmosphere_resources()) {
        atmosphere_runtime_.clouds().destroy_surface_target_resources();
    }
    engine_.renderers().destroy_swapchain_resources();
}

void GltfViewerApp::destroy_all_resources(cubey::vulkan::GpuRuntime& gpu) {
    asset_builds_.shutdown(gpu);
    gpu.wait_queue_idle("gltf_viewer generation retirement");
    if (landscape_runtime_) {
        if (config_.terrain.captured_water_audit)
            landscape_runtime_->verify_immutable_fields(gpu);
        landscape_runtime_->destroy();
        landscape_runtime_.reset();
    }
    landscape_foreground_.reset();
    if (active_generation_) {
        destroy_scene_generation(*active_generation_);
        cubey::destroy_gltf_scene_import(engine_, active_generation_->import_resources,
                                         active_generation_->import_result);
        active_generation_.reset();
    }
    ocean_runtime_.reset();
    terrain_runtime_.destroy();
    terrain_target_info_.reset();
    engine_.renderers().destroy_all_resources();
    forward_pbr_renderer_ = nullptr;
    gpu_profiler_.reset();
    whole_scene_profiler_.reset();
    if (environment_policy().uses_atmosphere_resources()) {
        atmosphere_runtime_.destroy();
    }
    ibl_environment_.reset();
    if (environment_policy().uses_atmosphere_resources()) {
        atmosphere_background_atlases_.shutdown(gpu);
    }
    requested_input_path_.clear();
    pending_loading_metrics_.clear();
    asset_activation_error_.clear();
    global_resources_created_ = false;
    frame_slot_count_ = 0U;
}

void GltfViewerApp::record_viewer_target(
    const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
    cubey::render::ColorTargetView color_target, cubey::render::FrameSlot frame_slot,
    cubey::render::RenderGraphTextureState color_initial_state,
    cubey::render::RenderGraphTextureState color_final_state,
    cubey::render::RenderGraphCommandBufferMode command_buffer_mode,
    cubey::ForwardPbrRenderer3DFrameMetrics* metrics) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    cubey::render::RenderGraphCommandBufferMode pbr_command_buffer_mode = command_buffer_mode;
    bool owns_command_buffer = false;
    switch (command_buffer_mode) {
    case cubey::render::RenderGraphCommandBufferMode::BeginAndEnd:
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        pbr_command_buffer_mode = cubey::render::RenderGraphCommandBufferMode::AlreadyRecording;
        owns_command_buffer = true;
        break;
    case cubey::render::RenderGraphCommandBufferMode::AlreadyRecording:
        break;
    default:
        throw std::runtime_error("glTF viewer command buffer mode is invalid");
    }
    if (gpu_profiler_.has_value()) {
        gpu_profiler_->begin_frame(command_buffer, frame_slot.index);
    }

    std::optional<cubey::vulkan::GpuTimestampScope> whole_scene;
    if (whole_scene_profiler_) {
        whole_scene_profiler_->begin_frame(command_buffer, frame_slot.index);
        whole_scene.emplace(&*whole_scene_profiler_, command_buffer, frame_slot.index,
                            "gltf viewer whole scene");
    }

    GltfViewerSceneGeneration& generation = active_generation();
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, color_target.extent);
    if (generation.runtime_scene.has_value()) {
        cubey::update_gltf_deformation_frame(
            generation.import_resources, generation.runtime_scene.value(), generation.import_result,
            scene_view, frame_slot,
            generation.animation_sample.has_value() ? &generation.animation_sample.value()
                                                    : nullptr);
    }
    const std::vector<cubey::render::GpuDeformationCommand> deformation_commands =
        cubey::gltf_deformation_commands_for_frame(generation.import_resources, frame_slot);
    const cubey::render::FrameMeshResourceTable* frame_meshes =
        deformation_commands.empty() ? nullptr
                                     : &generation.import_resources.deformation.frame_meshes;
    std::optional<cubey::CloudEnvironmentRuntimeFrame> cloud_frame;
    if (environment_policy().uses_atmosphere_resources() && clouds_config_.enabled) {
        cloud_frame = cloud_environment_frame(scene_view, color_target.extent);
    }
    if (environment_policy().uses_atmosphere_resources()) {
        record_atmosphere_environment_if_needed(recorder, frame_slot);
    }
    if (cloud_frame.has_value()) {
        record_cloud_environment_if_needed(recorder, frame_slot, cloud_frame.value());
    }
    if (ocean_backdrop_enabled()) {
        update_ocean_environment_descriptors(device, frame_slot);
    }
    forward_pbr_renderer().update_environment_frame(
        device, frame_slot,
        cubey::render::pbr_environment_frame_bindings(pbr_environment_bindings()));
    std::optional<cubey::ForwardPbrRenderer3DAtmosphereClouds> atmosphere_clouds;
    if (cloud_frame.has_value()) {
        atmosphere_clouds = cubey::ForwardPbrRenderer3DAtmosphereClouds{
            .runtime = &atmosphere_runtime_.clouds(),
            .frame = cloud_frame.value(),
        };
    }
    std::optional<cubey::render::AtmosphereEnvironmentFrameUniforms> atmosphere_background;
    if (environment_policy().uses_atmosphere_background()) {
        atmosphere_background.emplace(
            atmosphere_background_uniforms(scene_view, color_target.extent));
    }
    std::optional<cubey::ForwardPbrRenderer3DTerrainBackdrop> terrain_backdrop;
    if (terrain_backdrop_enabled() && terrain_visible_ && generation.terrain_surface.has_value() &&
        terrain_runtime_.product_ready()) {
        terrain_backdrop =
            terrain_backdrop_frame(scene_view, frame_plan, atmosphere_background.value());
    }
    std::optional<cubey::ForwardPbrRenderer3DOceanSurface> ocean_surface;
    if (ocean_backdrop_enabled() && ocean_visible_) {
        ocean_surface = ocean_surface_frame(scene_view, color_target.extent);
    }
    auto pbr_target = color_target;
    auto pbr_initial = color_initial_state;
    auto pbr_final = color_final_state;
    if (landscape_runtime_) {
        // Both retained targets are queue-owned, not per-slot. Complete the
        // preceding frame's merge reads before overwriting them on this queue.
        cubey::vulkan::record_memory_barrier(
            command_buffer, {.src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             .dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             .src_access = VK_ACCESS_SHADER_READ_BIT,
                             .dst_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT});
        pbr_target = cubey::render::color_target_view(
            landscape_foreground_->extent(), landscape_foreground_->format(),
            landscape_foreground_->handle(), landscape_foreground_->view());
        pbr_initial = cubey::render::render_graph_undefined_texture_state();
        pbr_final = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
    }
    forward_pbr_renderer().record({
        .target =
            {
                .device = &device,
                .command_buffer = command_buffer,
                .color_target = pbr_target,
                .frame_slot = frame_slot,
                .color_initial_state = pbr_initial,
                .color_final_state = pbr_final,
                .command_buffer_label = "vkEndCommandBuffer gltf_viewer",
                .command_buffer_mode = pbr_command_buffer_mode,
                .profiler = gpu_profiler_.has_value() ? &*gpu_profiler_ : nullptr,
            },
        .view =
            {
                .scene = &scene_view,
                .frame_plan = &frame_plan,
                .camera_entity = generation.camera_entity,
                .light_entity = generation.light_entity,
                .fallback_light = fallback_light_packet(),
            },
        .scene_resources =
            {
                .meshes = &generation.import_resources.meshes,
                .frame_meshes = frame_meshes,
                .deformation_commands = deformation_commands,
                .materials = &generation.import_resources.materials,
            },
        .settings =
            {
                .environment_rotation_degrees = config_.pbr.environment_rotation_degrees,
                .exposure = display_exposure(),
                .tonemap = resolve_gltf_viewer_pbr_tonemap(config_.pbr.tonemap),
                .debug_view = debug_view_,
                .background_mode = environment_policy().uses_ibl_skybox()
                                       ? cubey::ForwardPbrRenderer3DBackgroundMode::IblSkybox
                                       : cubey::ForwardPbrRenderer3DBackgroundMode::Atmosphere,
                .atmosphere_background = atmosphere_background,
                .atmosphere_clouds = atmosphere_clouds,
                .terrain_backdrop = terrain_backdrop,
                .ocean_surface = ocean_surface,
                .opaque_linear_output = landscape_runtime_ != nullptr,
            },
        .metrics = metrics,
    });
    if (landscape_runtime_) {
        // One rigid translation for the paired ground/water/context. The pivot
        // stays 200m (or the requested clearance) above the highest saved surface.
        const auto placement = cubey::render::resolve_backdrop_surface_placement(
            {.surface = {.nominal_local_height_m = landscape_maximum_surface_m_,
                         .maximum_local_height_m = landscape_maximum_surface_m_},
             .foreground = {.anchor_world_height_m = generation.bounds.center.y,
                            .minimum_local_height_m = -generation.bounds.half_extent.y},
             .requested_foreground_height_m = terrain_foreground_height_m_,
             .minimum_clearance_m = .1F});
        const cubey::math::Vec3 translation{generation.bounds.center.x,
                                            placement.surface_world_translation_y,
                                            generation.bounds.center.z};
        const auto& world = scene_view.transforms3d().world_affine_matrix(
            scene_view.transforms3d().instance(generation.camera_entity));
        const auto& asset_camera = scene_view.cameras3d().camera(
            scene_view.cameras3d().instance(generation.camera_entity));
        const float aspect = float(color_target.extent.width) / float(color_target.extent.height);
        const auto range =
            config_.terrain.landscape_depth == "fitted"
                ? gltf_viewer_landscape_depth_range(
                      generation.landscape_distance_bounds.x,
                      generation.landscape_distance_bounds.y, generation.landscape_orbit_radius_m,
                      asset_camera.fovy_radians(), aspect, config_.terrain.landscape_near_m)
                : GltfViewerLandscapeDepthRange{8, asset_camera.far_z()};
        if (reported_landscape_depth_ != cubey::math::Vec2{range.near_m, range.far_m}) {
            std::printf("gltf_landscape_depth: mode=%s foreground=(%.6f,%.6f) "
                        "background=(%.6f,%.6f) orbit_radius=%.6f distance_bounds=(%.6f,%.6f)\n",
                        config_.terrain.landscape_depth.c_str(), asset_camera.near_z(),
                        asset_camera.far_z(), range.near_m, range.far_m,
                        generation.landscape_orbit_radius_m, generation.landscape_distance_bounds.x,
                        generation.landscape_distance_bounds.y);
            reported_landscape_depth_ = {range.near_m, range.far_m};
        }
        const cubey::Camera3D landscape_camera({.fovy_radians = asset_camera.fovy_radians(),
                                                .near_z = range.near_m,
                                                .far_z = range.far_m});
        const cubey::render::TerrainSurfacePatchCamera camera{
            landscape_camera.view_projection_matrix(world, aspect) *
                glm::translate(cubey::math::Mat4{1}, translation),
            cubey::math::Vec3{world[3]} - translation};
        const auto& sky = atmosphere_runtime_.reflection_probe().sky_radiance_cube();
        const cubey::render::TerrainSurfacePatchEnvironment environment{
            pbr_environment_bindings(),
            atmosphere_runtime_.lighting(),
            display_exposure(),
            sky.sampler().handle(),
            sky.view(),
            *atmosphere_background};
        const cubey::render::TerrainSurfacePatchOpaqueLayer foreground{
            pbr_target, forward_pbr_renderer().scene_depth_target(),
            cubey::render::terrain_surface_patch_depth_scale_bias(
                asset_camera.near_z(), asset_camera.far_z(), landscape_camera.near_z(),
                landscape_camera.far_z())};
        landscape_runtime_->record(device, command_buffer, frame_slot, color_target,
                                   color_final_state.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                   camera, ocean_elapsed_seconds_, landscape_water_visible_,
                                   gpu_profiler_.has_value(), true, 0U, true, &environment,
                                   &foreground);
    }
    whole_scene.reset();
    if (owns_command_buffer) {
        recorder.end("vkEndCommandBuffer gltf_viewer");
    }
}

void GltfViewerApp::update_ocean_environment_descriptors(const cubey::vulkan::Device& device,
                                                         cubey::render::FrameSlot frame_slot) {
    const cubey::render::AtmosphereReflectionProbe& atmosphere_probe =
        atmosphere_runtime_.reflection_probe();
    const cubey::render::AtmosphereReflectionProbeSnapshot atmosphere = atmosphere_probe.snapshot();
    ocean_runtime_.update_atmosphere_probe_descriptors(device, frame_slot, *atmosphere.previous,
                                                       *atmosphere.current,
                                                       atmosphere_probe.sky_radiance_cube());

    const cubey::render::CloudEnvironmentProbeSnapshot clouds =
        atmosphere_runtime_.clouds().snapshot();
    if (clouds.valid) {
        ocean_runtime_.update_cloud_environment_descriptors(device, frame_slot, *clouds.previous,
                                                            *clouds.current);
    } else {
        ocean_runtime_.update_cloud_environment_descriptors(device, frame_slot, *atmosphere.current,
                                                            *atmosphere.current);
    }
}

void GltfViewerApp::collect_gpu_timings(cubey::profiling::ProfileRecorder* recorder,
                                        std::uint64_t frame_index,
                                        cubey::render::FrameSlot frame_slot) {
    const std::uint64_t collected_frame = collected_profile_frame_index(frame_index, frame_slot);
    if (whole_scene_profiler_) {
        whole_scene_profiler_->collect(frame_slot.index);
        if (recorder)
            for (const auto& timing : whole_scene_profiler_->latest_timings())
                recorder->record_gpu_span(collected_frame, timing.label, timing.milliseconds);
    }
    if (gpu_profiler_.has_value()) {
        gpu_profiler_->collect(frame_slot.index);
        if (recorder != nullptr) {
            for (const cubey::vulkan::GpuPassTiming& timing : gpu_profiler_->latest_timings()) {
                recorder->record_gpu_span(collected_frame, timing.label, timing.milliseconds);
            }
        }
    }
    if (ocean_backdrop_enabled()) {
        cubey::vulkan::GpuTimestampProfiler* ocean_profiler = ocean_runtime_.profiler();
        if (ocean_profiler != nullptr) {
            ocean_profiler->collect(frame_slot.index);
            if (recorder != nullptr) {
                for (const cubey::vulkan::GpuPassTiming& timing : ocean_runtime_.latest_timings()) {
                    recorder->record_gpu_span(collected_frame, timing.label, timing.milliseconds);
                }
            }
        }
    }
    if (landscape_runtime_) {
        const auto timings = landscape_runtime_->collect_timings(frame_slot.index);
        if (recorder)
            for (const auto& timing : timings)
                recorder->record_gpu_span(collected_frame, timing.label, timing.milliseconds);
    }
}

float GltfViewerApp::display_exposure() const {
    if (environment_policy().uses_atmosphere_auto_exposure() &&
        atmosphere_state_.auto_exposure_enabled && !config_.pbr.exposure_explicit) {
        return atmosphere_state_.resolved_exposure;
    }
    return config_.pbr.exposure;
}

void GltfViewerApp::record_atmosphere_environment_if_needed(
    const cubey::vulkan::CommandRecorder& recorder, cubey::render::FrameSlot frame_slot) {
    if (!environment_policy().uses_atmosphere_resources()) {
        return;
    }
    if (!atmosphere_runtime_.resources_created()) {
        throw std::runtime_error("glTF viewer atmosphere runtime is not initialized");
    }

    atmosphere_runtime_.record_pending_update(recorder, frame_slot);
}

void GltfViewerApp::record_cloud_environment_if_needed(
    const cubey::vulkan::CommandRecorder& recorder, cubey::render::FrameSlot frame_slot,
    const cubey::CloudEnvironmentRuntimeFrame& frame) {
    cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    if (!clouds.resources_created() || !clouds.pipelines_created()) {
        throw std::runtime_error("glTF viewer cloud environment runtime is not initialized");
    }
    static_cast<void>(clouds.record_pending_update(recorder, frame_slot, frame));
}

void GltfViewerApp::record_viewer_frame(cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
    cubey::profiling::ProfileRecorder* const recorder = context.profile_recorder();
    static_cast<void>(emit_gltf_viewer_pending_loading_metrics(recorder, frame.timing.frame_index,
                                                               pending_loading_metrics_));
    collect_gpu_timings(recorder, frame.timing.frame_index, frame.frame_slot);
    std::optional<cubey::ForwardPbrRenderer3DFrameMetrics> metrics;
    if (recorder != nullptr && recorder->should_record_frame(frame.timing.frame_index)) {
        metrics.emplace();
    }
    record_viewer_target(context.device(), frame.command_buffer, frame.color_target,
                         frame.frame_slot, cubey::render::render_graph_undefined_texture_state(),
                         cubey::render::render_graph_present_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::BeginAndEnd,
                         metrics.has_value() ? &*metrics : nullptr);
    if (metrics.has_value()) {
        static_cast<void>(
            emit_gltf_viewer_render_metrics(recorder, frame.timing.frame_index, *metrics));
    }
}

void GltfViewerApp::record_viewer_capture(cubey::host::HeadlessPngContext& context,
                                          const cubey::host::HeadlessCaptureFrame& frame,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
    ocean_delta_seconds_ =
        frame.timing.delta_seconds > 0.0 ? frame.timing.delta_seconds : (1.0 / 60.0);
    ocean_elapsed_seconds_ = frame.timing.elapsed_seconds;
    cubey::profiling::ProfileRecorder* const recorder = context.profile_recorder();
    static_cast<void>(
        emit_gltf_viewer_pending_loading_metrics(recorder, frame.index, pending_loading_metrics_));
    collect_gpu_timings(recorder, frame.index, frame.frame_slot);
    std::optional<cubey::ForwardPbrRenderer3DFrameMetrics> metrics;
    if (recorder != nullptr && recorder->should_record_frame(frame.index)) {
        metrics.emplace();
    }
    record_viewer_target(context.device(), command_buffer, target, frame.frame_slot,
                         cubey::render::render_graph_color_attachment_texture_state(),
                         cubey::render::render_graph_color_attachment_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                         metrics.has_value() ? &*metrics : nullptr);
    if (metrics.has_value()) {
        static_cast<void>(emit_gltf_viewer_render_metrics(recorder, frame.index, *metrics));
    }
}

} // namespace cubey::projects::gltf_viewer
