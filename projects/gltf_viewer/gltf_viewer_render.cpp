#include "gltf_viewer_app_internal.h"

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

void GltfViewerApp::create_frame_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                           VkFormat color_format, std::uint32_t frame_slot_count) {
    forward_pbr_renderer().create_swapchain_resources(
        device, cubey::ForwardPbrRenderer3DTargetResourcesInfo{
                    .extent = extent,
                    .color_format = color_format,
                    .materials = &import_resources_.materials,
                });
    if (terrain_backdrop_enabled()) {
        const cubey::ForwardPbrRenderer3DSceneTargetInfo target =
            forward_pbr_renderer().scene_target_info();
        terrain_runtime_.create_target_resources(device, {
                                                             .extent = target.extent,
                                                             .color_format = target.color_format,
                                                             .depth_format = target.depth_format,
                                                         });
    }
    if (ocean_backdrop_enabled()) {
        if (!use_atmosphere_environment_source()) {
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
    if (use_atmosphere_environment_source()) {
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
    ocean_runtime_.reset();
    terrain_runtime_.destroy_target_resources();
    atmosphere_runtime_.clouds().destroy_surface_target_resources();
    engine_.renderers().destroy_swapchain_resources();
}

void GltfViewerApp::destroy_all_resources(cubey::vulkan::GpuRuntime& gpu) {
    ocean_runtime_.reset();
    terrain_runtime_.destroy();
    engine_.renderers().destroy_all_resources();
    forward_pbr_renderer_ = nullptr;
    gpu_profiler_.reset();
    atmosphere_runtime_.destroy();
    ibl_environment_.reset();
    atmosphere_background_atlases_.shutdown(gpu);
    destroy_scene_if_needed();
    cubey::destroy_gltf_scene_import(engine_, import_resources_, import_result_);
    animation_playback_ = {};
    animation_sample_.reset();
    triangle_count_ = 0;
    asset_.reset();
}

void GltfViewerApp::record_viewer_target(
    const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
    cubey::render::ColorTargetView color_target, cubey::render::FrameSlot frame_slot,
    cubey::render::RenderGraphTextureState color_initial_state,
    cubey::render::RenderGraphTextureState color_final_state,
    cubey::render::RenderGraphCommandBufferMode command_buffer_mode) {
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

    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, color_target.extent);
    if (asset_.has_value()) {
        cubey::update_gltf_deformation_frame(
            import_resources_, asset_.value(), import_result_, scene_view, frame_slot,
            animation_sample_.has_value() ? &animation_sample_.value() : nullptr);
    }
    const std::vector<cubey::render::GpuDeformationCommand> deformation_commands =
        cubey::gltf_deformation_commands_for_frame(import_resources_, frame_slot);
    const cubey::render::FrameMeshResourceTable* frame_meshes =
        deformation_commands.empty() ? nullptr : &import_resources_.deformation.frame_meshes;
    std::optional<cubey::CloudEnvironmentRuntimeFrame> cloud_frame;
    if (use_atmosphere_environment_source() && clouds_config_.enabled) {
        cloud_frame = cloud_environment_frame(scene_view, color_target.extent);
    }
    record_atmosphere_environment_if_needed(recorder, frame_slot);
    if (cloud_frame.has_value()) {
        record_cloud_environment_if_needed(recorder, frame_slot, cloud_frame.value());
    }
    if (ocean_backdrop_enabled()) {
        update_ocean_environment_descriptors(device, frame_slot);
    }
    forward_pbr_renderer().update_environment(device, frame_slot, pbr_environment_bindings());
    std::optional<cubey::ForwardPbrRenderer3DAtmosphereClouds> atmosphere_clouds;
    if (cloud_frame.has_value()) {
        atmosphere_clouds = cubey::ForwardPbrRenderer3DAtmosphereClouds{
            .runtime = &atmosphere_runtime_.clouds(),
            .frame = cloud_frame.value(),
        };
    }
    const cubey::render::AtmosphereEnvironmentFrameUniforms atmosphere_background =
        atmosphere_background_uniforms(scene_view, color_target.extent);
    std::optional<cubey::ForwardPbrRenderer3DTerrainBackdrop> terrain_backdrop;
    if (terrain_backdrop_enabled() && terrain_visible_) {
        terrain_backdrop = terrain_backdrop_frame(scene_view, frame_plan, atmosphere_background);
    }
    std::optional<cubey::ForwardPbrRenderer3DOceanSurface> ocean_surface;
    if (ocean_backdrop_enabled() && ocean_visible_) {
        ocean_surface = ocean_surface_frame(scene_view, color_target.extent);
    }
    forward_pbr_renderer().record({
        .device = &device,
        .command_buffer = command_buffer,
        .color_target = color_target,
        .frame_slot = frame_slot,
        .color_initial_state = color_initial_state,
        .color_final_state = color_final_state,
        .command_buffer_label = "vkEndCommandBuffer gltf_viewer",
        .command_buffer_mode = pbr_command_buffer_mode,
        .profiler = gpu_profiler_.has_value() ? &*gpu_profiler_ : nullptr,
        .scene = &scene_view,
        .frame_plan = &frame_plan,
        .camera_entity = camera_entity_,
        .light_entity = light_entity_,
        .fallback_light = fallback_light_packet(),
        .scene_resources =
            {
                .meshes = &import_resources_.meshes,
                .frame_meshes = frame_meshes,
                .deformation_commands = deformation_commands,
                .materials = &import_resources_.materials,
            },
        .settings =
            {
                .environment_rotation_degrees = config_.pbr.environment_rotation_degrees,
                .exposure = display_exposure(),
                .debug_view = debug_view_,
                .background_mode = cubey::ForwardPbrRenderer3DBackgroundMode::Atmosphere,
                .atmosphere_background = atmosphere_background,
                .atmosphere_clouds = atmosphere_clouds,
                .terrain_backdrop = terrain_backdrop,
                .ocean_surface = ocean_surface,
            },
    });
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
    if (!gpu_profiler_.has_value()) {
        return;
    }
    gpu_profiler_->collect(frame_slot.index);
    if (recorder == nullptr) {
        return;
    }
    const std::uint64_t collected_frame = collected_profile_frame_index(frame_index, frame_slot);
    for (const cubey::vulkan::GpuPassTiming& timing : gpu_profiler_->latest_timings()) {
        recorder->record_gpu_span(collected_frame, timing.label, timing.milliseconds);
    }
}

float GltfViewerApp::display_exposure() const {
    if (atmosphere_state_.auto_exposure_enabled && !config_.pbr.exposure_explicit) {
        return atmosphere_state_.resolved_exposure;
    }
    return config_.pbr.exposure;
}

void GltfViewerApp::record_atmosphere_environment_if_needed(
    const cubey::vulkan::CommandRecorder& recorder, cubey::render::FrameSlot frame_slot) {
    if (!use_atmosphere_environment_source()) {
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
    collect_gpu_timings(context.profile_recorder(), frame.timing.frame_index, frame.frame_slot);
    record_viewer_target(context.device(), frame.command_buffer, frame.color_target,
                         frame.frame_slot, cubey::render::render_graph_undefined_texture_state(),
                         cubey::render::render_graph_present_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::BeginAndEnd);
}

void GltfViewerApp::record_viewer_capture(cubey::host::HeadlessPngContext& context,
                                          const cubey::host::HeadlessCaptureFrame& frame,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
    ocean_delta_seconds_ =
        frame.timing.delta_seconds > 0.0 ? frame.timing.delta_seconds : (1.0 / 60.0);
    ocean_elapsed_seconds_ = frame.timing.elapsed_seconds;
    collect_gpu_timings(context.profile_recorder(), frame.index, frame.frame_slot);
    record_viewer_target(context.device(), command_buffer, target, frame.frame_slot,
                         cubey::render::render_graph_color_attachment_texture_state(),
                         cubey::render::render_graph_color_attachment_texture_state(),
                         cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
}

} // namespace cubey::projects::gltf_viewer
