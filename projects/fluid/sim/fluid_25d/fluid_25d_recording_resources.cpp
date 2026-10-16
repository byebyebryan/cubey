#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

void RecordingApp::create_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                    std::uint32_t slots) {
    scenic_gpu_ = &gpu;
    scenic_slots_ = slots;
    runtime_.attach_gpu_if_needed(gpu);
    if (config_.recording_gpu_validation && render_.native_presentation != 0U)
        validate_fluid_25d_native_cue_gpu_controls(device, runtime_.gpu());
    if (config_.recording_gpu_validation &&
        (render_.native_bilinear_water || render_.native_bspline_surface))
        validate_fluid_25d_bank_gpu_controls(device, runtime_.gpu());
    resources_.create_global_resources_if_needed(device, runtime_.gpu(), simulation_, scenario_,
                                                 slots, true,
                                                 coverage_ ? coverage_->subdivision() : 0U);
    markers_available_ = config_.motion_markers || comparison_.has_value() ||
                         config_.terrain_demo_control_path.has_value();
    if (markers_available_)
        markers_.create(device, runtime_.gpu(), simulation_,
                        {&resources_.terrain(), &resources_.depth_a(), &resources_.depth_b(),
                         &resources_.velocity(), &resources_.source_rate(),
                         &resources_.presentation_cue_status()},
                        {-1.0F, -1.0F}, slots, false, Fluid25DMotionMarkerMode::Local);
    graph_.resize(slots);
    if (!config_.common.profile_output_prefix.empty()) {
        native_profiler_.emplace(device, slots, 1U);
        if (coverage_)
            coverage_profiler_.emplace(device, slots, 1U);
        native_profile_frames_.resize(slots);
        native_profile_pending_.resize(slots, false);
        native_profile_scenic_.resize(slots, false);
    }
}

void RecordingApp::ensure_scenic_resources(vulkan::Device& device, render::ColorTargetView target) {
    if (scenic_.ensure_resources(device, *scenic_gpu_, scenic_slots_, target, simulation_,
                                 scenario_, resources_,
                                 scenic_material_.terrain_diffuse_convolution > 0.0F,
                                 terrain_surface_ ? &*terrain_surface_ : nullptr,
                                 scenic_material_.daylight_environment > 0.5F) &&
        external_session_)
        external_refresh_after_render_setup_ = true;
    if (scenic_material_.daylight_environment > 0.5F && config_.environment_mode == "fixed" &&
        !daylight_runtime_.resources_created()) {
        const auto setup_started = std::chrono::steady_clock::now();
        // Fixed daytime spike reuses Water3D's shared runtime. No clouds,
        // time-of-day controls or atlas generation are introduced here.
        daylight_atlases_.emplace(
            render::create_atmosphere_background_placeholder_textures(device, *scenic_gpu_));
        daylight_runtime_.create_resources(
            device, {.reflection_extent = 64U,
                     .reflection_mip_levels = 5U,
                     .frame_slot_count = scenic_slots_,
                     .atmosphere_textures = daylight_atlases_->bindings()});
        const std::filesystem::path shaders{CUBEY_FLUID_25D_SHADER_DIR};
        daylight_runtime_.create_pipelines(
            device, {.atmosphere_vertex_shader = shaders / "atmosphere.vert.spv",
                     .atmosphere_fragment_shader = shaders / "atmosphere.frag.spv",
                     .reflection_prefilter_vertex_shader = shaders / "atmosphere.vert.spv",
                     .reflection_prefilter_fragment_shader =
                         shaders / "atmosphere_reflection_prefilter.frag.spv"});
        daylight_runtime_.set_environment(fluid_25d_fixed_daylight());
        const auto setup_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - setup_started)
                                  .count();
        std::printf("fluid_25d_scenic_environment: shared fixed daylight setup_wall_ms=%.6f; "
                    "probe captured once; no clouds or ongoing updates\n",
                    setup_ms);
        if (external_session_)
            external_refresh_after_render_setup_ = true;
    }
    if (config_.environment_mode == "shared" && !defer_weather_setup_) {
        if (!weather_)
            weather_ = std::make_unique<TerrainSurfaceEnvironment>(
                TerrainSurfaceEnvironmentSettings{config_.atmosphere});
        if (weather_->ensure_resources(device, *scenic_gpu_, scenic_slots_, target,
                                       CUBEY_FLUID_25D_SHADER_DIR) &&
            external_session_)
            external_refresh_after_render_setup_ = true;
    }
}

void RecordingApp::create_render_resources(vulkan::Device& device, render::ColorTargetView target) {
    resources_.create_render_pipelines(device, target.format, VK_FORMAT_D32_SFLOAT,
                                       target.extent, true);
    if (markers_available_)
        markers_.create_render_pipeline(device, target.format, VK_FORMAT_D32_SFLOAT,
                                        target.extent, true);
    if (render_.native_presentation == 3U)
        ensure_scenic_resources(device, target);
}

void RecordingApp::collect_native_timings(std::uint32_t slot, profiling::ProfileRecorder* profile) {
    if (!native_profiler_ || !profile || !native_profile_pending_[slot])
        return;
    native_profiler_->collect(slot);
    for (const auto& timing : native_profiler_->latest_timings())
        profile->record_gpu_span(native_profile_frames_[slot], timing.label,
                                 timing.milliseconds);
    if (native_profile_scenic_[slot])
        for (const auto& timing : scenic_.collect_timings(slot))
            profile->record_gpu_span(native_profile_frames_[slot], timing.label,
                                     timing.milliseconds);
    if (coverage_profiler_) {
        coverage_profiler_->collect(slot);
        for (const auto& timing : coverage_profiler_->latest_timings())
            profile->record_gpu_span(native_profile_frames_[slot], timing.label,
                                     timing.milliseconds);
    }
    native_profile_pending_[slot] = false;
}

void RecordingApp::shutdown() {
    // Preserve a late I/O/validation error, but always release resources
    // while the host device still exists. Report failure after host cleanup.
    try {
        // Shutdown follows the host's wait for all submitted frame slots.
        for (std::uint32_t slot = 0U; slot < native_profile_pending_.size(); ++slot)
            collect_native_timings(slot, native_profile_recorder_);
        if (config_.recording_gpu_validation) {
            if (external_session_)
                check_buffer<float>(resources_.terrain(), external_session_->bed(),
                                    "external immutable solver bed");
            else
                check_buffer<float>(resources_.terrain(), recording_->bed(),
                                    "native numerical bed");
            check_buffer<float>(resources_.depth_a(), shown_->depth_m, "imported depth A");
            check_buffer<float>(resources_.depth_b(), shown_->depth_m, "imported depth B");
            check_buffer<Fluid25DVelocityGpu>(resources_.velocity(), packed_velocity_,
                                              "derived velocity");
            if (external_session_)
                std::printf(
                    "fluid_25d_external_upload: PASS bit-exact bed/h/velocity at source "
                    "t=%.9f s; hydraulic dispatches=0 "
                    "(upload test, not native conservation oracle)\n",
                    shown_->time_s);
            else
                std::printf(
                    "fluid_25d_recording_upload: PASS bit-exact bed/h/velocity at saved "
                    "t=%.0f s; hydraulic dispatches=0 "
                    "(upload test, not native conservation oracle)\n",
                    shown_->time_s);
        }
    } catch (...) {
        shutdown_failure_ = std::current_exception();
    }
    try {
        if (pending_.valid())
            static_cast<void>(pending_.get());
    } catch (...) {
        if (!shutdown_failure_)
            shutdown_failure_ = std::current_exception();
    }
    try {
        if (stream_pending_.valid())
            static_cast<void>(stream_pending_.get());
    } catch (...) {
        if (!shutdown_failure_)
            shutdown_failure_ = std::current_exception();
    }
    try {
        if (external_pending_.valid())
            static_cast<void>(external_pending_.get());
    } catch (...) {
        if (!shutdown_failure_)
            shutdown_failure_ = std::current_exception();
    }
    try {
        if (whitewater_pending_.valid())
            static_cast<void>(whitewater_pending_.get());
    } catch (...) {
        if (!shutdown_failure_)
            shutdown_failure_ = std::current_exception();
    }
    graph_.clear();
    scenic_.destroy();
    daylight_runtime_.destroy();
    if (weather_)
        weather_->destroy();
    daylight_atlases_.reset();
    native_profiler_.reset();
    coverage_profiler_.reset();
    markers_.destroy();
    resources_.destroy_all_resources();
    runtime_.detach_gpu_if_attached();
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail
