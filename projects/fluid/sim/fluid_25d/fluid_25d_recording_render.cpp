#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

void RecordingApp::select_camera(const char* name, bool guided) {
    if (managed_demo_ && !guided)
        managed_demo_->manual_control(
            "Manual camera selection; guide stopped. Solver continues.");
    config_.recording_camera = name;
    config_.native_camera_target_x.reset();
    config_.native_camera_target_z.reset();
    config_.native_camera_span_m.reset();
    configure_camera();
}

void RecordingApp::configure_camera() {
    const float width =
        static_cast<float>(simulation_.grid_width - 1U) * simulation_.cell_size_m;
    const float height =
        static_cast<float>(simulation_.grid_height - 1U) * simulation_.cell_size_m;
    const float domain = std::max(width, height);
    const auto [lo, hi] = std::minmax_element(scenario_.terrain_height_m.begin(),
                                              scenario_.terrain_height_m.end());
    const float scale = *render_.terrain_height_scale;
    target_ = {0.0F, 0.5F * (*lo + *hi) * scale, 0.0F};
    float framing = domain;
    const bool explicit_target = config_.native_camera_target_x.has_value();
    if (explicit_target || config_.recording_camera != "overview") {
        // Observation-only normalized positions in the retained mountain.
        // Collection is the already surveyed B20 basin, not an authored drain.
        const float fx = config_.native_camera_target_x.value_or(
            config_.recording_camera == "collection" ? 474.0F / 511.0F : 392.0F / 511.0F);
        const float fz = config_.native_camera_target_z.value_or(
            config_.recording_camera == "collection" ? 105.0F / 511.0F : 311.0F / 511.0F);
        const auto col = static_cast<std::uint32_t>(
            std::round(fx * static_cast<float>(simulation_.grid_width - 1U)));
        const auto row = static_cast<std::uint32_t>(
            std::round(fz * static_cast<float>(simulation_.grid_height - 1U)));
        target_ = {
            (fx - 0.5F) * width,
            scenario_.terrain_height_m[static_cast<std::size_t>(row) * simulation_.grid_width +
                                       col] *
                scale,
            (fz - 0.5F) * height};
        framing =
            std::min(domain, config_.native_camera_span_m.value_or(
                                 config_.recording_camera == "collection" ? 2400.0F : 3200.0F));
    }
    const float maximum_distance = std::max(64.0F, domain * 4.0F);
    const float minimum_distance = std::max(16.0F, framing * 0.30F);
    const float home = render_.home_camera_distance_m.value_or(std::max(
        32.0F,
        framing *
            (render_.native_presentation >= 2U
                 ? (!explicit_target && config_.recording_camera == "overview" ? 1.50F : 1.18F)
                 : (!explicit_target && config_.recording_camera == "overview" ? 1.65F
                                                                               : 1.35F))));
    if (home < minimum_distance || home > maximum_distance)
        throw std::runtime_error("imported-state camera home distance exceeds orbit limits");
    orbit_.set_distance_limits(minimum_distance, maximum_distance);
    orbit_.set_pitch_limits(-0.38F, 0.38F);
    orbit_.set_home_distance(home);
    orbit_.reset();
    math::Vec3 scene_minimum{-0.5F * width, *lo * scale, -0.5F * height};
    math::Vec3 scene_maximum{0.5F * width, (*hi + 100.0F) * scale, 0.5F * height};
    const float far =
        fluid_25d_catchment_far_plane(maximum_distance, scene_minimum, scene_maximum, target_);
    camera_.set_projection(std::numbers::pi_v<float> / 3.0F, std::max(8.0F, framing * 0.10F),
                           far);
    std::printf("fluid_25d_camera_recipe: target=(%.6f,%.6f,%.6f) span=%.6f "
                "home=%.6f yaw=%.6f pitch=%.6f explicit=%u\n",
                target_.x, target_.y, target_.z, framing, home,
                config_.native_camera_yaw_radians, config_.native_camera_pitch_radians,
                explicit_target ? 1U : 0U);
    std::printf("fluid_25d_patch_scene: grid=%ux%u spacing_m=%.3f sampled_span=(%.3f,%.3f) "
                "display_subdivision=%u bounds=simulation_patch exterior_mesh_vertices=0 "
                "exterior_draws=0\n",
                simulation_.grid_width, simulation_.grid_height, simulation_.cell_size_m, width,
                height, render_.native_surface_subdivision);
}

Fluid25DRenderCamera RecordingApp::camera(VkExtent2D extent, std::uint64_t frame_index) const {
    float yaw = config_.native_camera_yaw_radians + orbit_.yaw();
    float distance = orbit_.distance();
    if (config_.native_camera_sweep_radians > 0.0F) {
        const auto count = host::headless_capture_frame_count(config_.common);
        const bool eased = config_.native_camera_motion == "eased";
        const float progress =
            eased
                ? host::headless_capture_eased_progress(
                      {.index = static_cast<std::uint32_t>(frame_index), .count = count})
                : std::clamp(float(frame_index) / float(std::max(count, 2U) - 1U), 0.0F, 1.0F);
        yaw += (progress - 0.5F) * config_.native_camera_sweep_radians;
        if (!eased)
            distance *= 1.0F + 0.08F * std::sin(progress * std::numbers::pi_v<float>);
        std::printf(
            "fluid_25d_terrain_camera: output_frame=%llu yaw=%.9f pitch=%.9f distance=%.9f\n",
            static_cast<unsigned long long>(frame_index), yaw,
            config_.native_camera_pitch_radians + orbit_.pitch(), distance);
    }
    const auto transform =
        orbit_camera_transform({.target = target_,
                                .distance = distance,
                                .yaw = yaw,
                                .pitch = config_.native_camera_pitch_radians + orbit_.pitch()});
    auto view_projection = camera_.view_projection_matrix(
        transform, static_cast<float>(extent.width) / static_cast<float>(extent.height));
    if (config_.native_camera_vertical_shift != 0.0F)
        for (int column = 0; column < 4; ++column)
            view_projection[column][1] -=
                2.0F * config_.native_camera_vertical_shift * view_projection[column][3];
    return {view_projection, transform.translation};
}

void RecordingApp::record(vulkan::Device& device, VkCommandBuffer commands,
                          render::ColorTargetView target, render::FrameSlot slot,
                          Fluid25DRenderTargetMode target_mode, profiling::ProfileRecorder* profile,
                          std::uint64_t frame_index) {
    synchronize_playback_boundary();
    // The host has waited for this slot's fence before reusing it. Measure
    // upload, presentation updates and drawing, not capture/encode cadence.
    if (native_profiler_ && profile) {
        collect_native_timings(slot.index, profile);
        native_profiler_->begin_frame(commands, slot.index);
        if (coverage_profiler_)
            coverage_profiler_->begin_frame(commands, slot.index);
        native_profile_frames_[slot.index] = frame_index;
        native_profile_pending_[slot.index] = true;
        native_profile_recorder_ = profile;
    }
    vulkan::GpuTimestampScope native_scope(
        native_profiler_ && profile ? &*native_profiler_ : nullptr, commands, slot.index,
        "fluid_25d native presentation total");
    if (upload_pending_) {
        auto span = profile ? profile->cpu_span(frame_index, "fluid25d.record_field_upload")
                            : profiling::ScopedCpuProfileSpan{};
        resources_.record_recording_upload(commands, slot.index, shown_->depth_m,
                                           packed_velocity_);
        upload_pending_ = false;
        if (external_session_ && external_snapshot_) {
            const double unix_s = std::chrono::duration<double>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
            const double freshness = std::max(0.0, unix_s - external_published_unix_s_);
            std::printf(
                "fluid_25d_external_display: session=%s generation=%llu sequence=%llu "
                "physical_s=%.9f lifecycle=%s published_unix_s=%.6f "
                "freshness_s=%.6f command_pending=%u command_recorded_unix_s=%.6f "
                "hydraulic_dispatches=0\n",
                backend_metadata_.session.session_id.c_str(),
                static_cast<unsigned long long>(external_snapshot_->header.reset_generation),
                static_cast<unsigned long long>(external_snapshot_->header.sequence),
                shown_->time_s, lifecycle_name(external_snapshot_->header.lifecycle),
                external_published_unix_s_, freshness,
                external_session_->command_pending() ? 1U : 0U,
                external_command_recorded_unix_s_);
            std::fflush(stdout);
        } else if (config_.stream_path) {
            const double unix_s = std::chrono::duration<double>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
            std::printf("fluid_25d_stream_display: session=%s revision=%llu saved_s=%.0f "
                        "producer=%s native_running=%u native_s=%.0f published_unix_s=%.6f "
                        "displayed_unix_s=%.6f "
                        "ready_to_display_s=%.6f hydraulic_dispatches=0\n",
                        recording_->session_id().c_str(),
                        static_cast<unsigned long long>(recording_->revision()), shown_->time_s,
                        recording_->producer_state().c_str(),
                        recording_->native_running() ? 1U : 0U,
                        recording_->latest_native_time_s(), shown_->published_unix_s, unix_s,
                        unix_s - shown_->published_unix_s);
            std::fflush(stdout);
        }
    }
    // Coverage residency is independent of hydraulic upload/publication.
    // A mode switch at a held saved state uploads the matching mask first.
    if (coverage_upload_pending_ && render_.native_display_coverage &&
        view_ != Fluid25DPresentationView::Diagnostics) {
        vulkan::GpuTimestampScope coverage_scope(
            coverage_profiler_ && profile ? &*coverage_profiler_ : nullptr, commands,
            slot.index, "fluid_25d display coverage upload");
        resources_.record_display_coverage_upload(commands, slot.index, active_coverage());
        coverage_resident_time_ = shown_->time_s;
        coverage_upload_pending_ = false;
        std::printf("fluid_25d_bank_mask_upload: saved_s=%.9f cache=%u\n", shown_->time_s,
                    coverage_cache_ ? 1U : 0U);
    }
    const bool quiver = view_ == Fluid25DPresentationView::Catchment &&
                        catchment_view_ == Fluid25DCatchmentView::FlowInspection;
    const bool had_marker_reset = marker_reset_;
    if (markers_available_ && marker_reset_) {
        markers_.record_reset(commands);
        marker_reset_ = false;
    }
    // Bound visual advection to one physical second per presentation step.
    // It reads imported h/u and changes only cue/marker buffers.
    if (visual_delta_ <= 0.0) {
        record_fluid_25d_recorded_presentation(commands, resources_, simulation_, 0.0F,
                                               cue_reset_, quiver_reset_, quiver,
                                               render_.native_presentation != 0U);
    } else {
        const auto steps = static_cast<std::uint32_t>(std::ceil(visual_delta_));
        const float delta = static_cast<float>(visual_delta_ / static_cast<double>(steps));
        for (std::uint32_t i = 0U; i < steps; ++i)
            record_fluid_25d_recorded_presentation(commands, resources_, simulation_, delta,
                                                   cue_reset_, quiver_reset_, quiver,
                                                   render_.native_presentation != 0U);
    }
    if (markers_available_) {
        marker_accumulator_ += visual_delta_;
        const auto steps = static_cast<std::uint32_t>(std::floor(marker_accumulator_));
        for (std::uint32_t i = 0U; i < steps; ++i)
            markers_.record_step(commands, true);
        marker_accumulator_ -= steps;
        if (!had_marker_reset && visual_delta_ > 0.0)
            marker_fraction_ = static_cast<float>(marker_accumulator_);
        else if (had_marker_reset)
            marker_fraction_ = 1.0F;
        vulkan::record_shader_write_barrier(
            commands,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    auto displayed_render = render_;
    if (view_ == Fluid25DPresentationView::Diagnostics)
        apply_fluid_25d_bank_view(displayed_render, Fluid25DBankView::Reference, false);
    // Decorative normal clock only. Held native hydraulic samples are never
    // interpolated. Pause keeps this frozen; replay/reset clears it explicitly.
    if (config_.recording_opening_frames || config_.recording_closing_frames)
        scenic_clock_s_ = double(frame_index) / double(config_.common.fps);
    else
        scenic_clock_s_ += std::max(0.0, visual_delta_) * 0.03;
    const bool scenic_active = scenic_material_active();
    if (native_profiler_ && profile)
        native_profile_scenic_[slot.index] = scenic_active;
    if (scenic_active) {
        ensure_scenic_resources(device, target);
        prepare_whitewater_sources();
        std::optional<Fluid25DScenicEnvironment> environment;
        std::optional<TerrainSurfaceWeatherFrame> weather_frame;
        if (config_.environment_mode == "shared") {
            const auto view = camera(target.extent, frame_index);
            weather_frame = weather_->frame(device, vulkan::CommandRecorder(commands), slot,
                                            {view.view_projection, view.position},
                                            scenic_.fallback_environment());
            environment = weather_frame->environment;
            if (profile) {
                profile->record_metric(frame_index, "fluid_25d.environment", "wall_s",
                                       weather_frame->elapsed_s);
                profile->record_metric(frame_index, "fluid_25d.environment", "time_hours",
                                       weather_frame->time_hours);
                profile->record_metric(frame_index, "fluid_25d.environment", "sky_generation",
                                       double(weather_frame->sky_generation));
            }
        } else if (scenic_material_.daylight_environment > 0.5F) {
            // Fixed sky: capture once, no ongoing updates or transitions.
            daylight_runtime_.record_pending_update(vulkan::CommandRecorder(commands), slot);
            environment.emplace(Fluid25DScenicEnvironment{
                daylight_runtime_.pbr_environment_bindings(scenic_.fallback_environment()),
                daylight_runtime_.lighting(), scenic_material_.daylight_exposure,
                daylight_runtime_.reflection_probe().sky_radiance_cube().sampler().handle(),
                daylight_runtime_.reflection_probe().sky_radiance_cube().view(),
                render::atmosphere_environment_frame_uniforms(daylight_runtime_.environment(),
                                                              {})});
        }
        scenic_.record(device, commands, graph_, slot, target, target_mode, resources_,
                       simulation_, camera(target.extent, frame_index), displayed_render,
                       scenic_clock_s_, scenic_material_,
                       config_.motion_markers ? &markers_ : nullptr, marker_fraction_,
                       profile != nullptr, scenic_flow_reset_,
                       fluid_25d_scenic_terrain_view(config_.native_scenic_terrain_view),
                       fluid_25d_scenic_water_view(config_.native_scenic_water_view),
                       fluid_25d_terrain_surface_mode(config_.native_scenic_surface_mode),
                       environment ? &*environment : nullptr, rain_visual_frame(),
                       whitewater_selection_.seeds, whitewater_generation_,
                       weather_frame ? &*weather_frame : nullptr);
        scenic_flow_reset_ = false;
    } else {
        const auto compiled = build_fluid_25d_frame_graph(
            target, resources_, simulation_, view_, catchment_view_, debug_view_,
            camera(target.extent, frame_index), target_mode, false, true,
            hydraulic_reset_never_used_, cue_reset_, quiver_reset_, nullptr, slot.index, {},
            displayed_render, config_.motion_markers ? &markers_ : nullptr, marker_fraction_,
            config_.motion_markers);
        graph_.record(
            {.device = &device,
             .command_buffer = commands,
             .frame_slot = slot,
             .label = "recorded SynxFlow presentation (draw-only)",
             .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
            compiled);
    }
    if (profile) {
        const double unix_s =
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        profile->record_metric(frame_index, "fluid_25d.study", "command_recorded_unix_s",
                               unix_s);
        profile->record_metric(frame_index, "fluid_25d.whitewater", "source_physical_s",
                               whitewater_source_s_);
        profile->record_metric(frame_index, "fluid_25d.whitewater",
                               "display_minus_source_physical_s",
                               std::max(0.0, shown_->time_s - whitewater_source_s_));
        profile->record_metric(frame_index, "fluid_25d.whitewater", "classification_cpu_ms",
                               whitewater_build_cpu_ms_);
        profile->record_metric(frame_index, "fluid_25d.whitewater", "requested_sources",
                               double(whitewater_selection_.requested));
        profile->record_metric(frame_index, "fluid_25d.whitewater", "allocated_sources",
                               double(whitewater_selection_.seeds.size()));
        if (config_.native_rain_visuals) {
            const auto rain = rain_visual_frame();
            const auto applied = applied_rain_mm_per_hour();
            profile->record_metric(frame_index, "fluid_25d.rain", "presentation_clock_s",
                                   rain.clock_s);
            profile->record_metric(frame_index, "fluid_25d.rain", "applied_mm_per_hour",
                                   applied.value_or(0.0));
            profile->record_metric(frame_index, "fluid_25d.rain", "rate_known",
                                   applied ? 1.0 : 0.0);
            profile->record_metric(frame_index, "fluid_25d.rain", "streak_count",
                                   rain.streak_count);
            profile->record_metric(frame_index, "fluid_25d.rain", "motion_phase_s",
                                   rain.motion_s);
            profile->record_metric(frame_index, "fluid_25d.rain", "fall_speed_multiplier",
                                   config_.native_rain_speed);
        }
        if (external_session_ && external_snapshot_) {
            const auto metric = [&](const char* name, double value) {
                profile->record_metric(frame_index, "fluid_25d.external", name, value);
            };
            metric("reset_generation",
                   static_cast<double>(external_snapshot_->header.reset_generation));
            metric("frame_sequence", static_cast<double>(external_snapshot_->header.sequence));
            metric("physical_time_s", shown_->time_s);
            metric("reader_last_completed_ms", external_read_ms_.load());
            metric("reader_completed_jobs", static_cast<double>(external_read_jobs_.load()));
            metric("lifecycle_code", static_cast<double>(external_snapshot_->header.lifecycle));
            metric("published_unix_s", external_published_unix_s_);
            metric("publication_freshness_s",
                   std::max(0.0, unix_s - external_published_unix_s_));
            metric("advancing_state_age_s",
                   external_last_advance_unix_s_ > 0
                       ? std::max(0.0, unix_s - external_last_advance_unix_s_)
                       : 0);
            metric("whitewater_source_age_s",
                   whitewater_source_published_unix_s_ > 0
                       ? std::max(0.0, unix_s - whitewater_source_published_unix_s_)
                       : 0);
            metric("command_pending", external_session_->command_pending() ? 1.0 : 0.0);
            metric("command_recorded_unix_s", external_command_recorded_unix_s_);
            metric("water_volume_m3", shown_->stats.water_volume_m3);
            metric("max_depth_m", shown_->stats.max_depth_m);
            metric("max_speed_m_per_s", shown_->stats.max_speed_m_per_s);
            metric("visual_delta_s_visual_only", visual_delta_);
            metric("hydraulic_dispatch_count", 0.0);
        } else {
            const auto metric = [&](const char* name, double value) {
                profile->record_metric(frame_index, "fluid_25d.recording", name, value);
            };
            metric("requested_time_s", clock_.time_s());
            metric("saved_state_time_s", shown_->time_s);
            metric("saved_frame_index", static_cast<double>(shown_index_));
            metric("water_volume_m3", shown_->stats.water_volume_m3);
            metric("max_depth_m", shown_->stats.max_depth_m);
            metric("max_speed_m_per_s", shown_->stats.max_speed_m_per_s);
            const Fluid25DRecordedRainStatus rain = recording_->rain_status_at(shown_->time_s);
            metric("recorded_rain_rate_mm_per_hour", rain.rate_mm_per_hour);
            metric("recorded_cumulative_scheduled_rain_depth_mm",
                   rain.cumulative_scheduled_rain_depth_mm);
            metric("recorded_rain_phase_code", static_cast<double>(rain.phase));
            metric("hydraulic_dispatch_count", 0.0);
            metric("visual_delta_s", visual_delta_);
        }
    }
    visual_delta_ = 0.0;
}

bool RecordingApp::scenic_material_active() const {
    return render_.native_presentation == 3U && view_ == Fluid25DPresentationView::Catchment &&
           catchment_view_ == Fluid25DCatchmentView::Composite &&
           render_.native_water_debug == 0U;
}

std::optional<double> RecordingApp::applied_rain_mm_per_hour() const {
    if (external_session_)
        return external_snapshot_
                   ? std::optional<double>{external_snapshot_->rain_m_per_s * 3.6e6}
                   : std::nullopt;
    // The forcing schedule is continuous even though water fields are held
    // at sparse saved knots. Match the explicitly labelled weather HUD.
    if (recording_)
        return recording_->rain_status_at(clock_.time_s()).rate_mm_per_hour;
    return std::nullopt;
}

Fluid25DRainVisualFrame RecordingApp::rain_visual_frame() const {
    const bool shaded = scenic_material_active() &&
                        config_.native_scenic_terrain_view == "shaded" &&
                        (config_.native_scenic_water_view == "shaded" ||
                         config_.native_scenic_water_view == "no-bank-edge");
    const auto applied_rate = applied_rain_mm_per_hour();
    return {fluid_25d_rain_visual_count(config_.native_rain_visuals && shaded, applied_rate,
                                        config_.native_rain_strength),
            rain_clock_.seconds(), rain_clock_.motion_seconds(), applied_rate};
}

void RecordingApp::log_rain_capture(std::uint32_t frame) const {
    const auto rain = rain_visual_frame();
    const auto rate = applied_rain_mm_per_hour();
    std::printf("fluid_25d_rain_capture: output_frame=%u applied_mm_h=%.9f known=%u "
                "clock_s=%.9f streaks=%u enabled=%u strength=%.3f "
                "fall_speed=%.3f motion_s=%.9f hydraulic_writes=0\n",
                frame, rate.value_or(0.0), rate ? 1U : 0U, rain.clock_s, rain.streak_count,
                config_.native_rain_visuals ? 1U : 0U, config_.native_rain_strength,
                config_.native_rain_speed, rain.motion_s);
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail
