#include "ocean_app_internal.h"

namespace cubey::projects::ocean::detail {

int OceanApp::run() {
    if (config_.common.headless) {
        return run_headless();
    }
    return run_windowed();
}

int OceanApp::run_windowed() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                        context.swapchain().extent(), context.frame_slot_count());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context,
                              const FrameTiming& timing) { update_windowed(context, timing); };
    callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) {
        bool atmosphere_changed = false;
        const cubey::render::CloudEnvironmentProbeSnapshot cloud_environment =
            atmosphere_runtime_.clouds().snapshot();
        const OceanMeshDrawPlan draw_plan = ocean_mesh_draw_plan(context.swapchain().extent());
        draw_ocean_ui({
            .config = ocean_config_,
            .diagnostics = diagnostics_,
            .spectrum_diagnostics = spectrum_diagnostics_,
            .surface_frame = draw_plan.surface_frame,
            .draw_stats = draw_plan.stats,
            .atmosphere = atmosphere_state_,
            .clouds = clouds_config_,
            .performance =
                {
                    .frame_stats = latest_frame_stats_,
                    .latest_fps = latest_fps_,
                    .latest_frame_ms = latest_frame_ms_,
                    .process = process_stats_.sample(),
                    .device_memory_budget = context.device().device_memory_budget(),
                    .gpu_timings = ocean_gpu_.latest_timings(),
                },
            .cloud_environment_generation = cloud_environment.generation,
            .cloud_environment_blend = cloud_environment.blend,
            .cloud_environment_age_seconds = atmosphere_runtime_.clouds().age_seconds(),
            .cloud_environment_valid = cloud_environment.valid,
            .render_view = render_view_,
            .camera_preset = camera_preset_,
            .paused = paused_,
            .reset_requested = reset_requested_,
            .step_requested = step_requested_,
            .camera_preset_requested = camera_preset_requested_,
            .atmosphere_changed = atmosphere_changed,
        });
        if (camera_preset_requested_) {
            apply_camera_preset(camera_preset_);
            camera_preset_requested_ = false;
        }
        if (atmosphere_changed) {
            refresh_atmosphere_lighting();
        }
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_windowed_frame(context, frame);
    };
    callbacks.frame_stats_sample =
        [this](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        return record_frame_stats(context.swapchain().extent(), timing);
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
        destroy_swapchain_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_.common,
            .app_name = "ocean",
            .ready_status = "rendering ocean project",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int OceanApp::run_headless() {
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_.common;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    host_config.require_dynamic_rendering = true;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        const cubey::host::HeadlessRenderTarget& target = context.render_target();
        create_pipeline(context.device(), context.gpu(), target.format, target.extent,
                        cubey::host::headless_capture_frame_slot_count(config_.common));
    };
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                    const cubey::host::HeadlessCaptureFrame& frame,
                                    VkCommandBuffer command_buffer,
                                    const cubey::host::HeadlessRenderTarget& target) {
        time_seconds_ = frame.timing.elapsed_seconds;
        last_delta_seconds_ =
            frame.timing.delta_seconds > 0.0 ? frame.timing.delta_seconds : (1.0 / 60.0);
        if (config_.common.capture_mode == CaptureMode::Video &&
            config_.capture.video_orbit_degrees.has_value()) {
            capture_orbit_offset_radians_ =
                config_.capture.video_orbit_degrees.value() *
                cubey::host::headless_capture_eased_progress(frame) *
                (std::numbers::pi_v<float> / 180.0F);
        } else {
            orbit_controller_.update(frame.timing.delta_seconds);
        }
        update_atmosphere_time(frame.timing.delta_seconds);
        atmosphere_runtime_.advance(frame.timing.delta_seconds);
        collect_gpu_timings(context.profile_recorder(), frame.index, frame.frame_slot,
                            frame.timing.elapsed_seconds);
        record_ocean_profile_metrics(context.profile_recorder(), frame.index, target.extent);
        record_ocean_target(command_buffer, context.device(), target, frame.frame_slot,
                            OceanRenderTargetMode::ColorAttachment);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
        destroy_swapchain_resources();
    };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

void OceanApp::update_windowed(cubey::host::WindowedAppContext& context,
                               const FrameTiming& timing) {
    const auto input = context.filtered_input();
    if (input.key_pressed(cubey::input::Key::Space)) {
        paused_ = !paused_;
    }
    if (input.key_pressed(cubey::input::Key::R)) {
        reset_requested_ = true;
    }
    if (input.key_pressed(cubey::input::Key::D)) {
        render_view_ = next_ocean_render_view(render_view_);
    }

    orbit_controller_.update_pointer_input(input, timing.delta_seconds);
    if (reset_requested_) {
        time_seconds_ = 0.0;
        foam_initialized_ = false;
        apply_camera_preset(camera_preset_);
        reset_requested_ = false;
    }
    if (paused_ && step_requested_) {
        time_seconds_ += 1.0 / 60.0;
        step_requested_ = false;
    } else if (!paused_) {
        time_seconds_ += timing.delta_seconds;
        step_requested_ = false;
    }
    last_delta_seconds_ = timing.delta_seconds > 0.0 ? timing.delta_seconds : (1.0 / 60.0);
    update_atmosphere_time(timing.delta_seconds);
    atmosphere_runtime_.advance(timing.delta_seconds);
    sync_gpu_resources(context);
}

void OceanApp::refresh_atmosphere_lighting() {
    atmosphere_lighting_ =
        cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment);
    if (atmosphere_runtime_.resources_created()) {
        atmosphere_runtime_.set_environment(atmosphere_state_.environment);
    }
}

void OceanApp::update_atmosphere_time(double delta_seconds) {
    if (cubey::atmosphere_environment_advance_time(atmosphere_state_, delta_seconds)) {
        refresh_atmosphere_lighting();
    }
}

std::optional<FrameStatsSample> OceanApp::record_frame_stats(VkExtent2D extent,
                                                             const FrameTiming& timing) {
    latest_frame_ms_ = timing.delta_seconds * 1000.0;
    latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

    const OceanMeshDrawPlan draw_plan = ocean_mesh_draw_plan(extent);
    const FrameStatsSample sample{
        .delta_seconds = timing.delta_seconds,
        .width = extent.width,
        .height = extent.height,
        .triangles = draw_plan.stats.submitted_triangles,
    };
    if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
        stats.has_value()) {
        latest_frame_stats_ = stats.value();
    }
    return sample;
}

void OceanApp::record_ocean_profile_metrics(cubey::profiling::ProfileRecorder* recorder,
                                            std::uint64_t frame_index, VkExtent2D extent) const {
    if (recorder == nullptr) {
        return;
    }

    const OceanMeshDrawPlan draw_plan = ocean_mesh_draw_plan(extent);
    const OceanConfig& mesh_config = draw_plan.surface_frame.mesh_config;
    const OceanHorizonDiagnostics& horizon = draw_plan.surface_frame.horizon;
    const auto metric = [recorder, frame_index](std::string_view name, double value) {
        recorder->record_metric(frame_index, "ocean.mesh", name, value);
    };

    metric("configured_cells", ocean_config_.mesh_cells);
    metric("effective_cells", mesh_config.mesh_cells);
    metric("effective_lod_levels", mesh_config.mesh_lod_levels);
    metric("mesh_half_extent_m", horizon.mesh_half_extent_m);
    metric("target_near_cell_m", horizon.target_near_cell_size_m);
    metric("effective_near_cell_m", horizon.near_cell_size_m);
    metric("camera_altitude_m", horizon.camera_altitude_m);
    metric("horizon_distance_m", horizon.horizon_distance_m);
    metric("generated_patches", draw_plan.stats.generated_patches);
    metric("submitted_patches", draw_plan.stats.submitted_patches);
    metric("culled_patches", draw_plan.stats.culled_patches);
    metric("generated_triangles", draw_plan.stats.generated_triangles);
    metric("submitted_triangles", draw_plan.stats.submitted_triangles);
    metric("reduced_filter_patches", draw_plan.stats.reduced_filter_patches);
    metric("reduced_filter_triangles", draw_plan.stats.reduced_filter_triangles);
    metric("reduced_shadow_patches", draw_plan.stats.reduced_shadow_patches);
    metric("reduced_shadow_triangles", draw_plan.stats.reduced_shadow_triangles);
}

void OceanApp::collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                                   std::uint64_t frame_index, cubey::render::FrameSlot frame_slot,
                                   double elapsed_seconds) {
    cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
    if (profiler == nullptr) {
        return;
    }
    profiler->collect(frame_slot.index);
    record_gpu_timings(profile_recorder, collected_profile_frame_index(frame_index, frame_slot),
                       ocean_gpu_.latest_timings());
    maybe_print_gpu_timings(elapsed_seconds);
}

void OceanApp::maybe_print_gpu_timings(double elapsed_seconds) {
    if (!config_.common.print_frame_stats) {
        return;
    }
    const std::vector<cubey::vulkan::GpuPassTiming>& timings = ocean_gpu_.latest_timings();
    if (timings.empty()) {
        return;
    }
    if (last_gpu_timing_print_seconds_ >= 0.0 &&
        elapsed_seconds - last_gpu_timing_print_seconds_ < 1.0) {
        return;
    }
    last_gpu_timing_print_seconds_ = elapsed_seconds;
    std::printf("ocean_gpu:");
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        std::printf(" %s=%.3fms", timing.label.c_str(), timing.milliseconds);
    }
    std::printf("\n");
}
} // namespace cubey::projects::ocean::detail

namespace cubey::projects::ocean {
using detail::OceanApp;

int run_ocean(const OceanProjectConfig& config) {
    OceanApp app(config);
    return app.run();
}

} // namespace cubey::projects::ocean
