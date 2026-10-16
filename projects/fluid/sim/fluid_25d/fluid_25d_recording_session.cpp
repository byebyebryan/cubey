#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

const char* lifecycle_name(Fluid25DLifecycle lifecycle) {
    switch (lifecycle) {
    case Fluid25DLifecycle::Ready:
        return "READY";
    case Fluid25DLifecycle::Running:
        return "RUNNING";
    case Fluid25DLifecycle::Paused:
        return "PAUSED";
    case Fluid25DLifecycle::Completed:
        return "COMPLETED";
    case Fluid25DLifecycle::Failed:
        return "FAILED";
    case Fluid25DLifecycle::Stopped:
        return "STOPPED";
    }
    return "UNKNOWN";
}

const char* backend_profile_name(Fluid25DBackendProfile profile) {
    switch (profile) {
    case Fluid25DBackendProfile::BuiltinVirtualPipes:
        return "builtin-vp";
    case Fluid25DBackendProfile::BuiltinFiniteVolume:
        return "builtin-fv";
    case Fluid25DBackendProfile::RecordingPlayback:
        return "recording-playback";
    case Fluid25DBackendProfile::StockExternalBridge:
        return "stock-external-bridge";
    case Fluid25DBackendProfile::ExternalService:
        return "external-service";
    }
    return "unknown";
}

const char* acknowledgement_name(Fluid25DAcknowledgementState state) {
    switch (state) {
    case Fluid25DAcknowledgementState::Accepted:
        return "accepted";
    case Fluid25DAcknowledgementState::Applied:
        return "applied";
    case Fluid25DAcknowledgementState::Rejected:
        return "rejected";
    }
    return "unknown";
}

const char* external_health_name(fluid_25d_project_config_detail::ExternalSessionHealth health) {
    using Health = fluid_25d_project_config_detail::ExternalSessionHealth;
    switch (health) {
    case Health::Healthy:
        return "HEALTHY";
    case Health::Delayed:
        return "HEARTBEAT DELAYED";
    case Health::Disconnected:
        return "STALE / DISCONNECTED";
    case Health::Completed:
        return "COMPLETED";
    case Health::Stopped:
        return "STOPPED";
    case Health::Failed:
        return "FAILED";
    }
    return "UNKNOWN";
}

RecordingApp::RecordingApp(const Fluid25DProjectConfig& config)
    : config_(config),
      recording_(config.external_session_path
                     ? nullptr
                     : std::make_shared<Fluid25DRecording>(
                           config.stream_path ? *config.stream_path : *config.recording_path,
                           config.stream_path.has_value())),
      external_session_(config.external_session_path ? std::make_unique<Fluid25DExternalSession>(
                                                           *config.external_session_path)
                                                     : nullptr),
      clock_(recording_ ? recording_->times_s() : std::span<const double>(static_clock_times_)),
      view_(fluid_25d_presentation_view_from_name(config.view)),
      catchment_view_(fluid_25d_catchment_view_from_name(config.catchment_view)),
      debug_view_(fluid_25d_debug_view_from_name(config.debug_view)) {
    if (external_session_) {
        auto initial = external_session_->load_latest();
        if (!initial || !external_session_->accept(*initial))
            throw std::runtime_error("external service has no initial valid publication");
        external_snapshot_ = std::move(initial);
        backend_metadata_ = external_session_->metadata();
        external_published_unix_s_ = external_snapshot_->fields->published_unix_s;
        const double initial_age_s = external_publication_age_s();
        external_last_valid_publication_steady_ =
            std::chrono::steady_clock::now() -
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(initial_age_s));
        const auto initial_lifecycle = external_snapshot_->header.lifecycle;
        external_terminal_ = initial_lifecycle == Fluid25DLifecycle::Completed ||
                             initial_lifecycle == Fluid25DLifecycle::Stopped;
        if (initial_lifecycle == Fluid25DLifecycle::Failed) {
            external_error_ = "external service reported FAILED: " +
                              external_snapshot_->header.failure_message;
        } else if (!external_terminal_ &&
                   initial_age_s >=
                       fluid_25d_project_config_detail::kExternalDisconnectAfterSeconds) {
            throw std::runtime_error("external service initial publication is stale");
        }
        if (external_snapshot_->acknowledgement &&
            external_snapshot_->acknowledgement->command_id <
                std::numeric_limits<std::uint64_t>::max())
            external_next_command_id_ = external_snapshot_->acknowledgement->command_id + 1U;
        accept_frame(external_snapshot_->fields);
        shown_index_ = static_cast<std::size_t>(external_snapshot_->header.sequence);
        external_rain_input_mm_per_hour_ =
            static_cast<float>(external_snapshot_->rain_m_per_s * 3.6e6);
        external_time_scale_input_ = static_cast<float>(external_snapshot_->pacing);
    } else {
        simulation_.grid_width = recording_->grid_width();
        simulation_.grid_height = recording_->grid_height();
        simulation_.cell_size_m = recording_->cell_size_m();
        if (config.recording_time_seconds > recording_->times_s().back())
            throw std::runtime_error("recording start time exceeds the final saved state");
        clock_.seek(config.recording_time_seconds);
        if (config.common.headless &&
            recording_->times_s()[clock_.frame_index()] != config.recording_time_seconds)
            throw std::runtime_error(
                "headless recording start time must match a saved state exactly");
        clock_.set_paused(config.common.headless || config.recording_time_seconds != 0.0F);
        backend_metadata_ = make_fluid_25d_recording_backend_metadata(
            *recording_, fluid_25d_new_backend_session_id());
        backend_metadata_.session.physical_time_s = clock_.time_s();
        shown_index_ = clock_.frame_index();
        accept_frame(recording_->frame(shown_index_));
        playback_session_.emplace(backend_metadata_);
    }
    simulation_.scenario = Fluid25DScenario::DryBed; // Allocation only, never a solver scene.
    simulation_.fixed_delta_seconds = 1.0F;          // Visual marker clock only.
    simulation_.minimum_wet_depth_m = 0.000001F;
    if (recording_)
        clock_.set_rate(config.recording_speed);
    playback_applied_rate_ = clock_.rate();
    live_playing_ =
        recording_ && !config.common.headless && config.recording_time_seconds == 0.0F;
    follow_latest_ = config.stream_follow_latest;
    if (external_session_) {
        simulation_.grid_width = backend_metadata_.grid.width;
        simulation_.grid_height = backend_metadata_.grid.height;
        simulation_.cell_size_m = static_cast<float>(backend_metadata_.grid.spacing_m);
    } else {
        simulation_.grid_width = recording_->grid_width();
        simulation_.grid_height = recording_->grid_height();
        simulation_.cell_size_m = recording_->cell_size_m();
    }
    scenario_.width = simulation_.grid_width;
    scenario_.height = simulation_.grid_height;
    scenario_.cell_size_m = simulation_.cell_size_m;
    if (external_session_)
        scenario_.terrain_height_m.assign(external_session_->bed().begin(),
                                          external_session_->bed().end());
    else
        scenario_.terrain_height_m.assign(recording_->bed().begin(), recording_->bed().end());
    scenario_.initial_water_depth_m = shown_->depth_m;
    scenario_.source_depth_rate_m_per_s.assign(shown_->depth_m.size(), 0.0F);
    scenario_.sink_depth_rate_m_per_s.assign(shown_->depth_m.size(), 0.0F);
    scenario_.boundary_outflow_face_mask.assign(shown_->depth_m.size(), 0U);
    if (config_.native_scenic_surface_grid) {
        terrain_surface_ = fluid_25d_load_sampled_terrain_surface(
            *config_.native_scenic_surface_grid, simulation_.grid_width,
            simulation_.grid_height, simulation_.cell_size_m, scenario_.terrain_height_m);
        std::printf("fluid_25d_terrain_surface: %s\n", terrain_surface_->receipt.c_str());
    } else if (config_.native_scenic_surface_source) {
        terrain_surface_ = fluid_25d_load_terrain_surface(
            *config_.native_scenic_surface_source,
            recording_->provenance().at("terrain_source_identity"), simulation_.grid_width,
            simulation_.grid_height, simulation_.cell_size_m, scenario_.terrain_height_m,
            recording_->source_bed());
        std::printf("fluid_25d_terrain_surface: %s\n", terrain_surface_->receipt.c_str());
    }
    render_ = config.catchment_render;
    scenic_material_ = fluid_25d_scenic_material(config_.native_scenic_material);
    if (config_.native_scenic_tuning_path)
        scenic_material_ = fluid_25d_load_scenic_material(*config_.native_scenic_tuning_path,
                                                          scenic_material_);
    report_scenic_material();
    render_.native_recording = true;
    render_.native_presentation = config.native_presentation == "scenic"     ? 3U
                                  : config.native_presentation == "readable" ? 2U
                                  : config.native_presentation == "motion"   ? 1U
                                                                             : 0U;
    render_.native_surface_highlights = fluid_25d_native_surface_highlights_enabled(
        fluid_25d_native_surface_highlights_policy(config.native_surface_highlights),
        render_.native_presentation);
    const std::array<std::string_view, 8U> water_debug_modes{
        "shaded",    "solid",    "unlit",        "normals",
        "wireframe", "wet-mask", "no-occlusion", "contours"};
    render_.native_water_debug =
        static_cast<std::uint32_t>(std::find(water_debug_modes.begin(), water_debug_modes.end(),
                                             config.native_water_debug) -
                                   water_debug_modes.begin());
    render_.native_bilinear_water = config.native_water_sampling == "bilinear";
    render_.native_bspline_surface = config.native_water_sampling == "bspline";
    render_.native_surface_subdivision = config.native_surface_subdivision;
    render_.native_shadow_scene_fit = config.native_shadow_projection == "scene";
    render_.native_shadow_extent = config.native_shadow_extent;
    if (config.native_display_coverage_path) {
        coverage_ = std::make_shared<Fluid25DDisplayCoverage>(
            *config.native_display_coverage_path, *recording_);
        if (config.bank_comparison) {
            const auto times = coverage_->times_s();
            comparison_.emplace(recording_->times_s(), times);
            const double initial = config.recording_time_seconds == 0.0F
                                       ? comparison_->first_s()
                                       : config.recording_time_seconds;
            if (!comparison_->contains(initial))
                throw std::runtime_error(
                    "bank comparison start time is outside its baked window");
            clock_.seek(initial);
            config_.recording_time_seconds = static_cast<float>(initial);
            shown_index_ = clock_.frame_index();
            accept_frame(recording_->frame(shown_index_));
            backend_metadata_.session.physical_time_s = clock_.time_s();
            playback_session_.emplace(backend_metadata_);
            live_playing_ = false;
            comparison_loop_ = config.bank_comparison_loop;
            // Show a labelled reference while windowed preparation runs.
            // No requested coverage view is activated until validation completes.
            apply_fluid_25d_bank_view(render_, Fluid25DBankView::Reference, false);
            bank_view_ = config.native_bank_view ? fluid_25d_bank_view(*config.native_bank_view)
                                                 : Fluid25DBankView::Reference;
            std::printf("fluid_25d_bank_comparison: PREPARING recorded window %.9f..%.9f s, "
                        "%zu masks; no live reconstruction\n",
                        comparison_->first_s(), comparison_->end_s(),
                        comparison_->frame_count());
            cache_progress_ = std::make_shared<std::atomic_size_t>(0U);
            if (config.common.headless) {
                coverage_cache_.emplace(*coverage_, times);
                activate_comparison_cache();
            } else {
                cache_pending_ = std::async(std::launch::async, [source = coverage_, times,
                                                                 progress = cache_progress_] {
                    return Fluid25DDisplayCoverageCache(
                        *source, times,
                        [progress](std::size_t count) { progress->store(count); });
                });
            }
        } else {
            bank_view_ = config.native_bank_view ? fluid_25d_bank_view(*config.native_bank_view)
                                                 : Fluid25DBankView::MarchingSquares;
            if (bank_view_ == Fluid25DBankView::MarchingSquares) {
                coverage_values_ = coverage_->frame(shown_->time_s);
                coverage_upload_pending_ = true;
            }
            apply_fluid_25d_bank_view(render_, bank_view_,
                                      coverage_->has_frame(shown_->time_s));
        }
        std::printf("fluid_25d_display_coverage: %s, display only, no live support or temporal "
                    "interpolation\n",
                    coverage_->label().c_str());
    } else if (config.native_bank_view) {
        bank_view_ = fluid_25d_bank_view(*config.native_bank_view);
        apply_fluid_25d_bank_view(render_, bank_view_, false);
    }
    std::printf("fluid_25d_backend_startup_metadata: %s\n",
                encode_fluid_25d_backend_metadata_json(backend_metadata_).c_str());
    render_.hillside_depth_cues = true;
    render_.terrain_thin_water_composite = true;
    render_.terrain_height_scale = render_.terrain_height_scale.value_or(1.0F);
    render_.quiver_speed_upper_m_per_s = 4.0F;
    if (!render_.terrain_palette_low_m) {
        const auto [lo, hi] = std::minmax_element(scenario_.terrain_height_m.begin(),
                                                  scenario_.terrain_height_m.end());
        render_.terrain_palette_low_m = *lo;
        render_.terrain_palette_high_m = std::max(*lo + 1.0F, *hi);
    }
    configure_camera();
    if (external_session_) {
        std::printf("fluid_25d: EXTERNAL SERVICE session=%s profile=%s id=%s, %ux%u @ %.3f m; "
                    "native time %.6f s lifecycle=%u; fields held between publications\n",
                    backend_metadata_.session.session_id.c_str(),
                    backend_profile_name(backend_metadata_.profile),
                    backend_metadata_.id.c_str(), simulation_.grid_width,
                    simulation_.grid_height, simulation_.cell_size_m, shown_->time_s,
                    static_cast<unsigned>(external_snapshot_->header.lifecycle));
    } else {
        std::printf("fluid_25d: %s SynxFlow states, %ux%u @ %.3f m, %zu frames, "
                    "%.0f..%.0f s; no hydraulic solver/reset/forcing dispatches\n",
                    config_.stream_path ? "LIVE EXTERNAL" : "RECORDED", simulation_.grid_width,
                    simulation_.grid_height, simulation_.cell_size_m, recording_->frame_count(),
                    recording_->times_s().front(), recording_->times_s().back());
        std::printf(
            "fluid_25d: recording=%s; initial scheduled rain=%.3f mm/h; native boundary=%s; "
            "h/q are held between saved times, velocity=q/h, qz=-native hUy\n",
            recording_->manifest_path().string().c_str(), recording_->rain_rate_mm_per_hour(),
            recording_->protocol().value("boundary", "unspecified").c_str());
    }
}

[[nodiscard]] double RecordingApp::external_publication_age_s() const {
    const double now_unix_s =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    return std::max(0.0, now_unix_s - external_published_unix_s_);
}

[[nodiscard]] double RecordingApp::external_seconds_since_valid_publication() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         external_last_valid_publication_steady_)
        .count();
}

[[nodiscard]] fluid_25d_project_config_detail::ExternalSessionHealth
RecordingApp::external_health() const {
    if (!external_snapshot_)
        return fluid_25d_project_config_detail::ExternalSessionHealth::Disconnected;
    return fluid_25d_project_config_detail::external_session_health(
        external_snapshot_->header.lifecycle, external_seconds_since_valid_publication(),
        external_publication_age_s(), external_reset_started_.has_value(),
        external_reset_started_
            ? std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                            *external_reset_started_)
                  .count()
            : 0.0);
}

[[nodiscard]] bool RecordingApp::external_controls_allowed() const {
    return external_session_ && !managed_held_ && !external_terminal_ &&
           external_error_.empty() && !external_session_->command_pending() &&
           external_health() == fluid_25d_project_config_detail::ExternalSessionHealth::Healthy;
}

void RecordingApp::fail_external_session(const std::string& message) {
    if (external_error_.empty()) {
        external_error_ = message;
        std::fprintf(stderr, "fluid_25d_external: %s\n", external_error_.c_str());
    }
}

void RecordingApp::send_external_command(Fluid25DCommandKind kind, std::optional<double> value,
                                         bool guided) {
    if (!external_controls_allowed())
        return;
    if (managed_demo_ && !guided)
        managed_demo_->manual_control("Guide stopped; manual solver control now applies.");
    const std::uint64_t command_id = external_next_command_id_;
    try {
        external_session_->send(kind, value);
        external_pending_command_id_ = command_id;
        external_pending_command_kind_ = kind;
        if (external_next_command_id_ < std::numeric_limits<std::uint64_t>::max())
            ++external_next_command_id_;
        external_command_recorded_unix_s_ =
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                .count();
    } catch (const std::exception& error) {
        fail_external_session(std::string("CONTROL ERROR: ") + error.what());
    }
}

void RecordingApp::poll_external_session() {
    if (!external_session_ || !external_error_.empty() || external_terminal_)
        return;
    if (external_refresh_after_render_setup_) {
        external_refresh_after_render_setup_ = false;
        try {
            // Cold IBL/pipeline creation can take longer than the service
            // timeout. Discard only the now-outdated I/O result (still
            // propagate its integrity errors), then read a current snapshot
            // before evaluating health. Never re-date a stale publication
            // or extend the worker's existing three-second timeout.
            if (external_pending_.valid())
                static_cast<void>(external_pending_.get());
            external_pending_ =
                std::async(std::launch::async, [this] { return timed_external_read(); });
            if (external_pending_.wait_for(std::chrono::seconds(1)) !=
                std::future_status::ready) {
                fail_external_session(
                    "current snapshot read timed out after local render setup");
                return;
            }
        } catch (const std::exception& error) {
            fail_external_session(std::string("VIEWER ERROR: ") + error.what());
            return;
        }
    }
    if (external_pending_.valid() &&
        external_pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto next = external_pending_.get();
            if (next && external_session_->accept(*next)) {
                const bool generation_changed =
                    external_snapshot_ && next->header.reset_generation !=
                                              external_snapshot_->header.reset_generation;
                const bool matching_own_ack =
                    next->acknowledgement && external_pending_command_id_ &&
                    next->acknowledgement->command_id == *external_pending_command_id_;
                const bool own_rain_applied =
                    matching_own_ack &&
                    external_pending_command_kind_ == Fluid25DCommandKind::SetRain &&
                    next->acknowledgement->state == Fluid25DAcknowledgementState::Applied;
                const bool own_time_scale_applied =
                    matching_own_ack &&
                    external_pending_command_kind_ == Fluid25DCommandKind::SetTimeScale &&
                    next->acknowledgement->state == Fluid25DAcknowledgementState::Applied;
                if (next->acknowledgement && next->acknowledgement->command_id <
                                                 std::numeric_limits<std::uint64_t>::max())
                    external_next_command_id_ = std::max(
                        external_next_command_id_, next->acknowledgement->command_id + 1U);
                if (matching_own_ack &&
                    next->acknowledgement->state != Fluid25DAcknowledgementState::Accepted) {
                    external_pending_command_id_.reset();
                    external_pending_command_kind_.reset();
                }
                external_snapshot_ = std::move(next);
                if (external_session_->reset_reinitialization_pending()) {
                    if (!external_reset_started_)
                        external_reset_started_ = std::chrono::steady_clock::now();
                } else {
                    external_reset_started_.reset();
                }
                backend_metadata_ = external_session_->metadata();
                external_published_unix_s_ = external_snapshot_->fields->published_unix_s;
                if (generation_changed || !shown_ ||
                    external_snapshot_->fields->time_s > shown_->time_s)
                    external_last_advance_unix_s_ = external_published_unix_s_;
                external_last_valid_publication_steady_ = std::chrono::steady_clock::now();
                accept_frame(external_snapshot_->fields);
                shown_index_ = static_cast<std::size_t>(external_snapshot_->header.sequence);
                if (generation_changed)
                    reset_visual_history();
                const bool refresh_rain =
                    fluid_25d_project_config_detail::should_refresh_external_control_input(
                        external_rain_dirty_, generation_changed, own_rain_applied);
                if (refresh_rain) {
                    external_rain_input_mm_per_hour_ =
                        static_cast<float>(external_snapshot_->rain_m_per_s * 3.6e6);
                    external_rain_dirty_ = false;
                }
                const bool refresh_time_scale =
                    fluid_25d_project_config_detail::should_refresh_external_control_input(
                        external_time_scale_dirty_, generation_changed, own_time_scale_applied);
                if (refresh_time_scale) {
                    external_time_scale_input_ = static_cast<float>(external_snapshot_->pacing);
                    external_time_scale_dirty_ = false;
                }
                const auto lifecycle = external_snapshot_->header.lifecycle;
                if (lifecycle == Fluid25DLifecycle::Failed) {
                    const std::string reason =
                        external_snapshot_->header.failure_message.empty()
                            ? "external service reported FAILED"
                            : "external service reported FAILED: " +
                                  external_snapshot_->header.failure_message;
                    fail_external_session(reason);
                } else if (lifecycle == Fluid25DLifecycle::Completed ||
                           lifecycle == Fluid25DLifecycle::Stopped) {
                    external_terminal_ = true;
                } else if (external_health() == fluid_25d_project_config_detail::
                                                    ExternalSessionHealth::Disconnected) {
                    fail_external_session("external service publication is stale");
                }
            }
        } catch (const std::exception& error) {
            fail_external_session(std::string("VIEWER ERROR: ") + error.what());
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (external_error_.empty() && !external_terminal_ &&
        external_health() ==
            fluid_25d_project_config_detail::ExternalSessionHealth::Disconnected)
        fail_external_session("no changed valid service publication for 3 seconds");
    if (external_error_.empty() && !external_terminal_ && !external_pending_.valid() &&
        now >= next_external_poll_) {
        next_external_poll_ = now + std::chrono::milliseconds(33);
        // load_latest only reads the immutable handshake/bed identity and
        // atomically published files. accept/send remain on the UI thread.
        external_pending_ =
            std::async(std::launch::async, [this] { return timed_external_read(); });
    }
}

std::optional<Fluid25DExternalSnapshot> RecordingApp::timed_external_read() {
    const auto started = std::chrono::steady_clock::now();
    auto result = external_session_->load_latest();
    external_read_ms_.store(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count());
    ++external_read_jobs_;
    return result;
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail
