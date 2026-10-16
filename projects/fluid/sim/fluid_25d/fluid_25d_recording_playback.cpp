#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

void RecordingApp::activate_comparison_cache() {
    std::printf("fluid_25d_bank_cache: READY frames=%zu bytes=%zu preparation_ms=%.6f "
                "budget_bytes=%zu\n",
                coverage_cache_->frame_count(), coverage_cache_->bytes(),
                coverage_cache_->preparation_ms(), Fluid25DDisplayCoverageCache::kByteBudget);
    set_bank_view(bank_view_);
}

void RecordingApp::poll_comparison_cache() {
    if (!cache_pending_.valid() ||
        cache_pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try {
        coverage_cache_.emplace(cache_pending_.get());
        activate_comparison_cache();
    } catch (const std::exception& error) {
        comparison_error_ = error.what();
        clock_.set_paused(true);
        std::fprintf(stderr, "fluid_25d_bank_comparison: PREPARATION FAILED: %s\n",
                     comparison_error_.c_str());
    }
}

[[nodiscard]] bool RecordingApp::matching_mask_ready() const {
    return coverage_ && coverage_->has_frame(shown_->time_s) &&
           (!comparison_ || coverage_cache_.has_value());
}

[[nodiscard]] std::span<const float> RecordingApp::active_coverage() const {
    return coverage_cache_ ? coverage_cache_->frame(shown_->time_s)
                           : std::span<const float>(coverage_values_);
}

void RecordingApp::set_bank_view(Fluid25DBankView view) {
    const bool ready = matching_mask_ready();
    if (view == Fluid25DBankView::MarchingSquares && !ready)
        throw std::runtime_error("marching-squares coverage is unavailable at this saved time");
    if (view == Fluid25DBankView::MarchingSquares) {
        if (!coverage_cache_)
            coverage_values_ = coverage_->frame(shown_->time_s);
        coverage_upload_pending_ = coverage_resident_time_ != shown_->time_s;
    } else
        coverage_upload_pending_ = false;
    apply_fluid_25d_bank_view(render_, view, ready);
    bank_view_ = view;
    config_.native_bank_view = fluid_25d_bank_view_name(view);
    std::printf("fluid_25d_bank_view: mode=%s requested_s=%.9f saved_s=%.9f camera=%s "
                "paused=%u rate=%.3f cue_reset=%u quiver_reset=%u marker_reset=%u "
                "generation=%llu; fields/camera/clock/history unchanged\n",
                fluid_25d_bank_view_name(view), clock_.time_s(), shown_->time_s,
                config_.recording_camera.c_str(), clock_.paused() ? 1U : 0U, clock_.rate(),
                cue_reset_ ? 1U : 0U, quiver_reset_ ? 1U : 0U, marker_reset_ ? 1U : 0U,
                static_cast<unsigned long long>(backend_metadata_.session.reset_generation));
}

void RecordingApp::cycle_bank_view() {
    if (comparison_ && !coverage_cache_)
        return;
    const auto current = render_.native_display_coverage  ? Fluid25DBankView::MarchingSquares
                         : render_.native_bspline_surface ? Fluid25DBankView::Bspline2x
                                                          : Fluid25DBankView::Reference;
    auto next = static_cast<Fluid25DBankView>((static_cast<unsigned>(current) + 1U) % 3U);
    if (next == Fluid25DBankView::MarchingSquares && !matching_mask_ready())
        next = Fluid25DBankView::Reference;
    set_bank_view(next);
}

void RecordingApp::seek_playback(double time) {
    clock_.seek(comparison_ ? comparison_->map(time, false).time_s : time);
    if (comparison_)
        comparison_at_end_ = comparison_->at_end(clock_.time_s());
    comparison_end_reported_ = false;
    live_playing_ = follow_latest_ = false;
    reset_visual_history();
}

void RecordingApp::toggle_playback() {
    if (comparison_ && (!coverage_cache_ || !comparison_error_.empty()))
        return;
    if (comparison_ && comparison_->at_end(clock_.time_s())) {
        seek_playback(comparison_->first_s());
        clock_.set_paused(false);
    } else if (!clock_.ended())
        clock_.set_paused(!clock_.paused());
}

void RecordingApp::constrain_comparison_clock(bool was_playing) {
    if (!comparison_)
        return;
    const auto bounded = comparison_->map(clock_.time_s(), comparison_loop_ && was_playing);
    if (bounded.looped) {
        seek_playback(bounded.time_s);
        clock_.set_paused(false);
        std::printf(
            "fluid_25d_bank_comparison: REPLAY LOOP; recorded fields, not solver restart\n");
    } else if (bounded.at_end || bounded.time_s != clock_.time_s()) {
        clock_.seek(bounded.time_s);
        comparison_at_end_ = bounded.at_end;
        if (bounded.at_end && !comparison_end_reported_) {
            std::printf("fluid_25d_bank_comparison: END OF BAKED WINDOW; holding last saved "
                        "state, not end of simulation\n");
            comparison_end_reported_ = true;
        }
    }
    if (!bounded.at_end) {
        comparison_at_end_ = false;
        comparison_end_reported_ = false;
    }
}

void RecordingApp::accept_frame(std::shared_ptr<const Fluid25DRecordedFrame> frame) {
    if (coverage_ && !comparison_ && render_.native_display_coverage) {
        const auto start = std::chrono::steady_clock::now();
        coverage_values_ = coverage_->frame(frame->time_s);
        const double load_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::printf("fluid_25d_display_coverage_load: saved_s=%.9f cpu_ms=%.6f\n",
                    frame->time_s, load_ms);
    }
    shown_ = std::move(frame);
    if (coverage_ && render_.native_display_coverage)
        coverage_upload_pending_ = coverage_resident_time_ != shown_->time_s;
    packed_velocity_.resize(shown_->velocity.size());
    for (std::size_t i = 0; i < packed_velocity_.size(); ++i)
        packed_velocity_[i] = {{shown_->velocity[i].x_m_per_s, shown_->velocity[i].y_m_per_s,
                                shown_->depth_m[i] > 0.0F ? 1.0F : 0.0F, 0.0F}};
    upload_pending_ = true;
    whitewater_dirty_ = true;
}

void RecordingApp::prepare_whitewater_sources() {
    if (scenic_material_.water_whitewater_strength <= 0.0F)
        return;
    const auto accept = [&](WhitewaterBuild build) {
        if (build.epoch != whitewater_epoch_)
            return; // Seek/reset invalidates in-flight placement, not immutable fields.
        whitewater_selection_ = std::move(build.selection);
        whitewater_source_s_ = build.source_s;
        whitewater_build_cpu_ms_ = build.cpu_ms;
        whitewater_source_published_unix_s_ = build.published_unix_s;
        ++whitewater_generation_;
        std::printf(
            "fluid_25d_whitewater_sources: saved_s=%.9f displayed_s=%.9f generation=%llu "
            "requested=%zu allocated=%zu cutoff=%.6f cpu_ms=%.6f; render only\n",
            build.source_s, shown_->time_s,
            static_cast<unsigned long long>(whitewater_generation_),
            whitewater_selection_.requested, whitewater_selection_.seeds.size(),
            whitewater_selection_.priority_cutoff, build.cpu_ms);
    };
    if (whitewater_pending_.valid() &&
        whitewater_pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        accept(whitewater_pending_.get());
    if (!whitewater_dirty_ || whitewater_pending_.valid())
        return;
    // Bed is immutable and outlives the future. A shared native snapshot
    // pins h/u; at most one classification job runs, coalescing newer fields.
    const auto build =
        [frame = shown_, bed = std::span<const float>(scenario_.terrain_height_m),
         width = simulation_.grid_width, height = simulation_.grid_height,
         spacing = simulation_.cell_size_m, epoch = whitewater_epoch_,
         budget = static_cast<std::uint32_t>(scenic_material_.water_whitewater_budget)] {
            const auto started = std::chrono::steady_clock::now();
            auto selected = fluid_25d_whitewater_seeds(width, height, spacing, bed,
                                                       frame->depth_m, frame->velocity, budget);
            return WhitewaterBuild{std::move(selected), frame->time_s,
                                   std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - started)
                                       .count(),
                                   frame->published_unix_s, epoch};
        };
    whitewater_dirty_ = false;
    if (fluid_25d_project_config_detail::use_synchronous_whitewater_classification(
            config_.common))
        accept(build()); // Deterministic captures never use a stale candidate list.
    else
        whitewater_pending_ = std::async(std::launch::async, build);
}

void RecordingApp::reset_presentation_history() {
    ++whitewater_epoch_;
    ++whitewater_generation_;
    whitewater_selection_.seeds.clear();
    whitewater_source_s_ = whitewater_build_cpu_ms_ = whitewater_source_published_unix_s_ = 0;
    whitewater_dirty_ = true;
    scenic_clock_s_ = 0.0;
    rain_clock_.reset();
    scenic_flow_reset_ = true;
    cue_reset_ = quiver_reset_ = marker_reset_ = true;
    visual_delta_ = marker_accumulator_ = 0.0;
}

void RecordingApp::reset_visual_history() {
    reset_presentation_history();
    reset_on_load_ = true;
    if (playback_session_)
        playback_navigation_kind_ = Fluid25DCommandKind::Seek;
}

void RecordingApp::restart_playback() {
    if (comparison_) {
        seek_playback(comparison_->first_s());
        comparison_at_end_ = comparison_end_reported_ = false;
        playback_navigation_kind_ = Fluid25DCommandKind::Reset;
        return;
    }
    clock_.restart();
    live_playing_ = false;
    follow_latest_ = false;
    reset_visual_history();
    playback_navigation_kind_ = Fluid25DCommandKind::Reset;
}

void RecordingApp::synchronize_playback_boundary() {
    if (!playback_session_)
        return;
    if (config_.stream_path)
        clock_.set_paused(!live_playing_ || !stream_error_.empty());
    const auto apply = [&](Fluid25DCommandKind kind, Fluid25DLifecycle lifecycle,
                           std::optional<double> value = {}) {
        const auto ack = playback_session_->apply(Fluid25DControlDomain::Playback, kind,
                                                  clock_.time_s(), lifecycle, value);
        std::printf("fluid_25d_backend_ack: %s\n",
                    encode_fluid_25d_command_acknowledgement_json(ack).c_str());
    };
    if (playback_navigation_kind_) {
        // Navigation is rare, bounded local I/O. Load the selected state
        // before announcing its generation, rather than rendering an old
        // generation while an asynchronous seek is still pending.
        auto selected = recording_->frame(clock_.frame_index());
        const auto kind = *playback_navigation_kind_;
        apply(kind,
              kind == Fluid25DCommandKind::Reset ? Fluid25DLifecycle::Ready
                                                 : Fluid25DLifecycle::Paused,
              kind == Fluid25DCommandKind::Seek ? std::optional<double>(clock_.time_s())
                                                : std::nullopt);
        accept_frame(std::move(selected));
        shown_index_ = clock_.frame_index();
        playback_navigation_kind_.reset();
        reset_on_load_ = false;
        visual_delta_ = marker_accumulator_ = 0.0;
    }
    const auto current = playback_session_->metadata().session.lifecycle;
    const bool finite_end = clock_.ended() && shown_index_ == clock_.frame_index() &&
                            (!config_.stream_path || recording_->producer_state() != "running");
    const auto desired = finite_end                          ? Fluid25DLifecycle::Completed
                         : clock_.paused() || clock_.ended() ? Fluid25DLifecycle::Paused
                                                             : Fluid25DLifecycle::Running;
    if (current != Fluid25DLifecycle::Completed && current != Fluid25DLifecycle::Failed) {
        if (desired != Fluid25DLifecycle::Completed && current != desired)
            apply(desired == Fluid25DLifecycle::Paused ? Fluid25DCommandKind::Pause
                                                       : Fluid25DCommandKind::Resume,
                  desired);
        if (clock_.rate() != playback_applied_rate_) {
            apply(Fluid25DCommandKind::SetTimeScale,
                  desired == Fluid25DLifecycle::Completed ? current : desired, clock_.rate());
            playback_applied_rate_ = clock_.rate();
        }
        const auto frame = playback_session_->publish(clock_.time_s(), desired);
        if (upload_pending_)
            std::printf("fluid_25d_backend_frame: %s\n",
                        encode_fluid_25d_frame_header_json(frame).c_str());
    }
    backend_metadata_ = playback_session_->metadata();
}

void RecordingApp::poll_frame_load() {
    const auto wanted = clock_.frame_index();
    if (pending_.valid() &&
        pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto loaded =
            pending_.get(); // Integrity failures stop playback, never silently substitute.
        if (fluid_25d_local_frame_load_is_current(
                pending_index_, pending_viewer_generation_, wanted,
                playback_session_->metadata().session.reset_generation)) {
            accept_frame(std::move(loaded));
            shown_index_ = wanted;
            // Loading may finish after pause/end: initialize arrows from
            // the newly shown velocity instead of freezing the prior state.
            if (clock_.paused() || clock_.ended())
                quiver_reset_ = true;
            if (reset_on_load_) {
                cue_reset_ = quiver_reset_ = marker_reset_ = true;
                reset_on_load_ = false;
            }
        }
    }
    if (shown_index_ != wanted && !pending_.valid()) {
        pending_index_ = wanted;
        pending_viewer_generation_ = playback_session_->metadata().session.reset_generation;
        pending_ = std::async(std::launch::async,
                              [source = recording_, wanted] { return source->frame(wanted); });
    }
}

void RecordingApp::poll_stream() {
    if (!config_.stream_path || !stream_error_.empty())
        return;
    if (stream_pending_.valid() &&
        stream_pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto next = stream_pending_.get();
            next->validate_successor_of(*recording_);
            if (next->revision() != recording_->revision()) {
                clock_.extend(next->times_s());
                recording_ = std::move(next);
            }
        } catch (const std::exception& error) {
            stream_error_ = error.what();
            live_playing_ = false;
            clock_.set_paused(true);
            std::fprintf(stderr, "fluid_25d_stream: VIEWER ERROR: %s\n", stream_error_.c_str());
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (!stream_pending_.valid() && recording_->producer_state() == "running" &&
        now >= next_stream_poll_) {
        next_stream_poll_ = now + std::chrono::milliseconds(100);
        stream_pending_ = std::async(std::launch::async, [path = *config_.stream_path] {
            return std::make_shared<Fluid25DRecording>(path, true);
        });
    }
}

void RecordingApp::update_view(double wall_delta_s) {
    if (weather_ && config_.environment_mode == "shared")
        weather_->advance(std::max(0.0, wall_delta_s));
    if (managed_held_) {
        visual_delta_ = 0.0;
        pace_meter_.sample(0, shown_->time_s, false);
        return;
    }
    poll_comparison_cache();
    if (comparison_ && (!coverage_cache_ || !comparison_error_.empty())) {
        visual_delta_ = 0.0;
        return;
    }
    if (external_session_) {
        poll_external_session();
        pace_meter_.sample(
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count(),
            shown_->time_s,
            external_snapshot_->header.lifecycle == Fluid25DLifecycle::Running &&
                external_error_.empty() &&
                external_health() ==
                    fluid_25d_project_config_detail::ExternalSessionHealth::Healthy);
        visual_delta_ = 0.0;
        if (external_snapshot_ && external_error_.empty() &&
            external_health() ==
                fluid_25d_project_config_detail::ExternalSessionHealth::Healthy &&
            external_snapshot_->header.lifecycle == Fluid25DLifecycle::Running) {
            // Approximate presentation-only cue time while the native field
            // snapshot is held. Never advance the displayed source clock.
            visual_delta_ = std::min(wall_delta_s, 0.25) * external_snapshot_->pacing;
            rain_clock_.advance(wall_delta_s, true, config_.native_rain_speed);
        }
        return;
    }
    poll_stream();
    if (config_.stream_path) {
        clock_.set_paused(!live_playing_ || !stream_error_.empty());
        if (live_playing_ && follow_latest_ &&
            clock_.time_s() != recording_->times_s().back()) {
            clock_.seek(recording_->times_s().back());
            clock_.set_paused(false);
            reset_visual_history();
        }
    }
    try {
        poll_frame_load();
    } catch (const std::exception& error) {
        if (!config_.stream_path)
            throw;
        stream_error_ = error.what();
        live_playing_ = false;
        clock_.set_paused(true);
    }
    const double before = clock_.time_s();
    const bool was_playing = !clock_.paused();
    if (shown_index_ == clock_.frame_index())
        clock_.advance(std::min(wall_delta_s, 0.25));
    constrain_comparison_clock(was_playing);
    visual_delta_ = clock_.time_s() - before;
    if (visual_delta_ < 0.0)
        visual_delta_ = 0.0; // Explicit replay clears visual history.
    if (config_.stream_path && live_playing_ && clock_.ended() &&
        recording_->producer_state() == "running")
        clock_.set_paused(false); // Waiting for a growing prefix is not completion.
    try {
        if (stream_error_.empty())
            poll_frame_load();
    } catch (const std::exception& error) {
        if (!config_.stream_path)
            throw;
        stream_error_ = error.what();
        live_playing_ = false;
        clock_.set_paused(true);
    }
    if (shown_index_ != clock_.frame_index())
        visual_delta_ = 0.0;
    rain_clock_.advance(wall_delta_s,
                        !clock_.paused() && !clock_.ended() &&
                            shown_index_ == clock_.frame_index() && stream_error_.empty(),
                        config_.native_rain_speed);
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail
