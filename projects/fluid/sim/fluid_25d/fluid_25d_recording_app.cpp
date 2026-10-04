#include "fluid_25d_recording_app.h"

#include "fluid_25d_backend_adapters.h"
#include "fluid_25d_commands.h"
#include "fluid_25d_external_session.h"
#include "fluid_25d_local_session.h"
#include "fluid_25d_motion_markers.h"
#include "fluid_25d_project_config.h"
#include "fluid_25d_recording.h"

#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/memory_barriers.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <future>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

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

// This application intentionally has no hydraulic compute entry point. Its
// resources do not create solver pipelines; the frame graph is draw-only.
class RecordingApp {
  public:
    explicit RecordingApp(const Fluid25DProjectConfig& config)
        : config_(config),
          recording_(config.external_session_path
                         ? nullptr
                         : std::make_shared<Fluid25DRecording>(
                               config.stream_path ? *config.stream_path : *config.recording_path,
                               config.stream_path.has_value())),
          external_session_(
              config.external_session_path
                  ? std::make_unique<Fluid25DExternalSession>(*config.external_session_path)
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
        std::printf("fluid_25d_backend_startup_metadata: %s\n",
                    encode_fluid_25d_backend_metadata_json(backend_metadata_).c_str());
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
        render_ = config.catchment_render;
        render_.native_recording = true;
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

    int run() {
        const int status = config_.common.headless ? run_headless() : run_windowed();
        if (shutdown_failure_)
            std::rethrow_exception(shutdown_failure_);
        return stream_error_.empty() && external_error_.empty() ? status : 1;
    }

  private:
    [[nodiscard]] double external_publication_age_s() const {
        const double now_unix_s =
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        return std::max(0.0, now_unix_s - external_published_unix_s_);
    }

    [[nodiscard]] double external_seconds_since_valid_publication() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             external_last_valid_publication_steady_)
            .count();
    }

    [[nodiscard]] fluid_25d_project_config_detail::ExternalSessionHealth external_health() const {
        if (!external_snapshot_)
            return fluid_25d_project_config_detail::ExternalSessionHealth::Disconnected;
        return fluid_25d_project_config_detail::external_session_health(
            external_snapshot_->header.lifecycle, external_seconds_since_valid_publication(),
            external_publication_age_s());
    }

    [[nodiscard]] bool external_controls_allowed() const {
        return external_session_ && !external_terminal_ && external_error_.empty() &&
               !external_session_->command_pending() &&
               external_health() == fluid_25d_project_config_detail::ExternalSessionHealth::Healthy;
    }

    void fail_external_session(const std::string& message) {
        if (external_error_.empty()) {
            external_error_ = message;
            std::fprintf(stderr, "fluid_25d_external: %s\n", external_error_.c_str());
        }
    }

    void accept_frame(std::shared_ptr<const Fluid25DRecordedFrame> frame) {
        shown_ = std::move(frame);
        packed_velocity_.resize(shown_->velocity.size());
        for (std::size_t i = 0; i < packed_velocity_.size(); ++i)
            packed_velocity_[i] = {{shown_->velocity[i].x_m_per_s, shown_->velocity[i].y_m_per_s,
                                    shown_->depth_m[i] > 0.0F ? 1.0F : 0.0F, 0.0F}};
        upload_pending_ = true;
    }

    void reset_visual_history() {
        cue_reset_ = quiver_reset_ = marker_reset_ = true;
        reset_on_load_ = true;
        visual_delta_ = marker_accumulator_ = 0.0;
        if (playback_session_)
            playback_navigation_kind_ = Fluid25DCommandKind::Seek;
    }

    void restart_playback() {
        clock_.restart();
        live_playing_ = false;
        follow_latest_ = false;
        reset_visual_history();
        playback_navigation_kind_ = Fluid25DCommandKind::Reset;
    }

    void synchronize_playback_boundary() {
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

    void poll_frame_load() {
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

    void poll_stream() {
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

    void send_external_command(Fluid25DCommandKind kind, std::optional<double> value = {}) {
        if (!external_controls_allowed())
            return;
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

    void poll_external_session() {
        if (!external_session_ || !external_error_.empty() || external_terminal_)
            return;
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
                    backend_metadata_ = external_session_->metadata();
                    external_published_unix_s_ = external_snapshot_->fields->published_unix_s;
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
            external_pending_ = std::async(std::launch::async, [session = external_session_.get()] {
                return session->load_latest();
            });
        }
    }

    void update_view(double wall_delta_s) {
        if (external_session_) {
            poll_external_session();
            visual_delta_ = 0.0;
            if (external_snapshot_ && external_error_.empty() &&
                external_health() ==
                    fluid_25d_project_config_detail::ExternalSessionHealth::Healthy &&
                external_snapshot_->header.lifecycle == Fluid25DLifecycle::Running) {
                // Approximate presentation-only cue time while the native field
                // snapshot is held. Never advance the displayed source clock.
                visual_delta_ = std::min(wall_delta_s, 0.25) * external_snapshot_->pacing;
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
        if (shown_index_ == clock_.frame_index())
            clock_.advance(std::min(wall_delta_s, 0.25));
        visual_delta_ = clock_.time_s() - before;
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
    }

    void create_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots) {
        runtime_.attach_gpu_if_needed(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), simulation_, scenario_,
                                                     slots, true);
        if (config_.motion_markers)
            markers_.create(device, runtime_.gpu(), simulation_,
                            {&resources_.terrain(), &resources_.depth_a(), &resources_.depth_b(),
                             &resources_.velocity(), &resources_.source_rate(),
                             &resources_.presentation_cue_status()},
                            {-1.0F, -1.0F}, slots, false, Fluid25DMotionMarkerMode::Local);
        graph_.resize(slots);
    }

    void create_render_resources(vulkan::Device& device, render::ColorTargetView target) {
        resources_.create_render_pipelines(device, target.format, VK_FORMAT_D32_SFLOAT,
                                           target.extent);
        if (config_.motion_markers)
            markers_.create_render_pipeline(device, target.format, VK_FORMAT_D32_SFLOAT,
                                            target.extent);
    }

    void configure_camera() {
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
        if (config_.recording_camera != "overview") {
            // Observation-only normalized positions in the retained mountain.
            // Collection is the already surveyed B20 basin, not an authored drain.
            const float fx =
                config_.recording_camera == "collection" ? 474.0F / 511.0F : 392.0F / 511.0F;
            const float fz =
                config_.recording_camera == "collection" ? 105.0F / 511.0F : 311.0F / 511.0F;
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
                std::min(domain, config_.recording_camera == "collection" ? 2400.0F : 3200.0F);
        }
        const float maximum_distance = std::max(64.0F, domain * 4.0F);
        const float minimum_distance = std::max(16.0F, framing * 0.30F);
        const float home = render_.home_camera_distance_m.value_or(
            std::max(32.0F, framing * (config_.recording_camera == "overview" ? 1.65F : 1.35F)));
        if (home < minimum_distance || home > maximum_distance)
            throw std::runtime_error("imported-state camera home distance exceeds orbit limits");
        orbit_.set_distance_limits(minimum_distance, maximum_distance);
        orbit_.set_pitch_limits(-0.38F, 0.38F);
        orbit_.set_home_distance(home);
        orbit_.reset();
        const float far = fluid_25d_catchment_far_plane(
            maximum_distance, {-0.5F * width, *lo * scale, -0.5F * height},
            {0.5F * width, (*hi + 100.0F) * scale, 0.5F * height}, target_);
        camera_.set_projection(std::numbers::pi_v<float> / 3.0F, std::max(8.0F, framing * 0.10F),
                               far);
    }

    Fluid25DRenderCamera camera(VkExtent2D extent) const {
        const auto transform = orbit_camera_transform({.target = target_,
                                                       .distance = orbit_.distance(),
                                                       .yaw = -0.52F + orbit_.yaw(),
                                                       .pitch = -0.92F + orbit_.pitch()});
        return {camera_.view_projection_matrix(transform, static_cast<float>(extent.width) /
                                                              static_cast<float>(extent.height)),
                transform.translation};
    }

    void record(vulkan::Device& device, VkCommandBuffer commands, render::ColorTargetView target,
                render::FrameSlot slot, Fluid25DRenderTargetMode target_mode,
                profiling::ProfileRecorder* profile, std::uint64_t frame_index) {
        synchronize_playback_boundary();
        if (upload_pending_) {
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
        const bool quiver = view_ == Fluid25DPresentationView::Catchment &&
                            catchment_view_ == Fluid25DCatchmentView::FlowInspection;
        const bool had_marker_reset = marker_reset_;
        if (config_.motion_markers && marker_reset_) {
            markers_.record_reset(commands);
            marker_reset_ = false;
        }
        // Bound visual advection to one physical second per presentation step.
        // It reads imported h/u and changes only cue/marker buffers.
        if (visual_delta_ <= 0.0) {
            record_fluid_25d_recorded_presentation(commands, resources_, simulation_, 0.0F,
                                                   cue_reset_, quiver_reset_, quiver);
        } else {
            const auto steps = static_cast<std::uint32_t>(std::ceil(visual_delta_));
            const float delta = static_cast<float>(visual_delta_ / static_cast<double>(steps));
            for (std::uint32_t i = 0U; i < steps; ++i)
                record_fluid_25d_recorded_presentation(commands, resources_, simulation_, delta,
                                                       cue_reset_, quiver_reset_, quiver);
        }
        if (config_.motion_markers) {
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
        const auto compiled = build_fluid_25d_frame_graph(
            target, resources_, simulation_, view_, catchment_view_, debug_view_,
            camera(target.extent), target_mode, false, true, hydraulic_reset_never_used_,
            cue_reset_, quiver_reset_, nullptr, slot.index, {}, render_,
            config_.motion_markers ? &markers_ : nullptr, marker_fraction_, config_.motion_markers);
        graph_.record(
            {.device = &device,
             .command_buffer = commands,
             .frame_slot = slot,
             .label = "recorded SynxFlow presentation (draw-only)",
             .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
            compiled);
        if (profile) {
            if (external_session_ && external_snapshot_) {
                const auto metric = [&](const char* name, double value) {
                    profile->record_metric(frame_index, "fluid_25d.external", name, value);
                };
                const double unix_s = std::chrono::duration<double>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
                metric("reset_generation",
                       static_cast<double>(external_snapshot_->header.reset_generation));
                metric("frame_sequence", static_cast<double>(external_snapshot_->header.sequence));
                metric("physical_time_s", shown_->time_s);
                metric("lifecycle_code", static_cast<double>(external_snapshot_->header.lifecycle));
                metric("published_unix_s", external_published_unix_s_);
                metric("publication_freshness_s",
                       std::max(0.0, unix_s - external_published_unix_s_));
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

    template <typename Value>
    void check_buffer(const vulkan::Buffer& buffer, std::span<const Value> expected,
                      const char* name) {
        const auto bytes = runtime_.gpu().readback_buffer(buffer.handle(), buffer.size(), name);
        if (bytes.size() != expected.size_bytes() ||
            std::memcmp(bytes.data(), expected.data(), bytes.size()) != 0)
            throw std::runtime_error(std::string("recording GPU immutability mismatch: ") + name);
    }

    void shutdown() {
        // Preserve a late I/O/validation error, but always release resources
        // while the host device still exists. Report failure after host cleanup.
        try {
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
        graph_.clear();
        markers_.destroy();
        resources_.destroy_all_resources();
        runtime_.detach_gpu_if_attached();
    }

    void draw_external_ui() {
        ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("External service runoff")) {
            ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                               "EXTERNAL SERVICE - Cubey Vulkan viewer");
            const auto health = external_health();
            ImGui::Text("Lifecycle: %s | heartbeat: %s",
                        lifecycle_name(external_snapshot_->header.lifecycle),
                        external_health_name(health));
            ImGui::Text("Native physical time: %.6f s", shown_->time_s);
            const double publication_age_s = external_publication_age_s();
            const double now_unix_s =
                std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            ImGui::Text(
                "Generation %llu | sequence %llu",
                static_cast<unsigned long long>(external_snapshot_->header.reset_generation),
                static_cast<unsigned long long>(external_snapshot_->header.sequence));
            ImGui::Text("Publication age %.3f s", publication_age_s);
            if (external_session_->command_pending())
                ImGui::Text("Command pending %.2f s | recorded at Unix %.6f",
                            std::max(0.0, now_unix_s - external_command_recorded_unix_s_),
                            external_command_recorded_unix_s_);
            if (!external_session_->last_command_message().empty())
                ImGui::TextWrapped("Last command acknowledgement: %s",
                                   external_session_->last_command_message().c_str());
            if (external_snapshot_->acknowledgement) {
                const auto& ack = *external_snapshot_->acknowledgement;
                ImGui::Text("Ack %llu %s | generation %llu",
                            static_cast<unsigned long long>(ack.command_id),
                            acknowledgement_name(ack.state),
                            static_cast<unsigned long long>(ack.reset_generation));
                if (ack.application_time_s)
                    ImGui::Text("Applied at %.6f s | %s", *ack.application_time_s,
                                lifecycle_name(ack.lifecycle));
            }
            if (!external_error_.empty())
                ImGui::TextWrapped("VIEWER ERROR (fail-closed): %s", external_error_.c_str());
            if (!external_snapshot_->header.failure_message.empty())
                ImGui::TextWrapped("Service failure: %s",
                                   external_snapshot_->header.failure_message.c_str());

            const auto lifecycle = external_snapshot_->header.lifecycle;
            const bool terminal = lifecycle == Fluid25DLifecycle::Completed ||
                                  lifecycle == Fluid25DLifecycle::Failed ||
                                  lifecycle == Fluid25DLifecycle::Stopped;
            const bool busy =
                external_session_->command_pending() || !external_error_.empty() || terminal ||
                health != fluid_25d_project_config_detail::ExternalSessionHealth::Healthy;
            const auto& solver_caps = backend_metadata_.capabilities.solver;
            const auto supports = [&](Fluid25DCommandKind kind) {
                return fluid_25d_command_supported(backend_metadata_.capabilities,
                                                   Fluid25DControlDomain::Solver, kind);
            };
            ImGui::BeginDisabled(busy);
            if (supports(Fluid25DCommandKind::Pause) && lifecycle == Fluid25DLifecycle::Running &&
                ImGui::Button("Pause solver [Space]"))
                send_external_command(Fluid25DCommandKind::Pause);
            if (supports(Fluid25DCommandKind::Resume) &&
                (lifecycle == Fluid25DLifecycle::Paused || lifecycle == Fluid25DLifecycle::Ready) &&
                ImGui::Button("Resume solver [Space]"))
                send_external_command(Fluid25DCommandKind::Resume);
            if (supports(Fluid25DCommandKind::Step)) {
                ImGui::SameLine();
                if (ImGui::Button("Step"))
                    send_external_command(Fluid25DCommandKind::Step);
            }
            if (supports(Fluid25DCommandKind::Reset)) {
                ImGui::SameLine();
                if (ImGui::Button("Full reset [R]"))
                    send_external_command(Fluid25DCommandKind::Reset);
            }
            if (supports(Fluid25DCommandKind::Stop)) {
                if (ImGui::Button("Stop producer"))
                    send_external_command(Fluid25DCommandKind::Stop);
            }
            ImGui::EndDisabled();

            if (supports(Fluid25DCommandKind::SetRain)) {
                ImGui::BeginDisabled(busy);
                ImGui::SetNextItemWidth(140.0F);
                if (ImGui::InputFloat("Uniform rain override (mm/h)",
                                      &external_rain_input_mm_per_hour_, 0.1F, 1.0F, "%.3f"))
                    external_rain_dirty_ = true;
                ImGui::BeginDisabled(!external_rain_dirty_ ||
                                     !std::isfinite(external_rain_input_mm_per_hour_) ||
                                     external_rain_input_mm_per_hour_ < 0.0F);
                if (ImGui::Button("Apply rain"))
                    send_external_command(Fluid25DCommandKind::SetRain,
                                          static_cast<double>(external_rain_input_mm_per_hour_) /
                                              3.6e6);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::Text("active %.3f mm/h", external_snapshot_->rain_m_per_s * 3.6e6);
                ImGui::EndDisabled();
            }
            if (supports(Fluid25DCommandKind::SetTimeScale)) {
                ImGui::BeginDisabled(busy);
                ImGui::SetNextItemWidth(140.0F);
                if (ImGui::InputFloat("Producer time scale", &external_time_scale_input_, 0.125F,
                                      1.0F, "%.3f"))
                    external_time_scale_dirty_ = true;
                ImGui::BeginDisabled(
                    !external_time_scale_dirty_ || !std::isfinite(external_time_scale_input_) ||
                    external_time_scale_input_ < 0.125F || external_time_scale_input_ > 300.0F);
                if (ImGui::Button("Apply time scale"))
                    send_external_command(Fluid25DCommandKind::SetTimeScale,
                                          external_time_scale_input_);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::Text("active %.3fx", external_snapshot_->pacing);
                ImGui::EndDisabled();
            }
            if (external_rain_dirty_ || external_time_scale_dirty_) {
                if (ImGui::Button("Discard typed edits")) {
                    if (fluid_25d_project_config_detail::should_refresh_external_control_input(
                            true, false, false, true)) {
                        external_rain_input_mm_per_hour_ =
                            static_cast<float>(external_snapshot_->rain_m_per_s * 3.6e6);
                        external_time_scale_input_ = static_cast<float>(external_snapshot_->pacing);
                        external_rain_dirty_ = false;
                        external_time_scale_dirty_ = false;
                    }
                }
            }

            if (ImGui::CollapsingHeader("Backend details")) {
                ImGui::Text("Adapter: %s", backend_metadata_.id.c_str());
                ImGui::Text("Profile: %s | service ID: %s",
                            backend_profile_name(backend_metadata_.profile),
                            backend_metadata_.id.c_str());
                ImGui::Text("Input: %.12s | solver bed: %.12s",
                            backend_metadata_.grid.input_sha256.c_str(),
                            backend_metadata_.grid.solver_bed_sha256.c_str());
                ImGui::Text("Grid: %u x %u at %.6g m; row-major x-fastest/z rows",
                            backend_metadata_.grid.width, backend_metadata_.grid.height,
                            backend_metadata_.grid.spacing_m);
                ImGui::Text("Solver controls: pause=%d resume=%d reset=%d step=%d stop=%d",
                            solver_caps.pause, solver_caps.resume, solver_caps.reset,
                            solver_caps.step, solver_caps.stop);
                ImGui::Text("Set rain=%d time scale=%d", solver_caps.set_rain,
                            solver_caps.set_time_scale);
                ImGui::Text("Native fields: h=%d vx=%d vz=%d momentum=(%d,%d) face-Q=%d",
                            backend_metadata_.fields.depth_m,
                            backend_metadata_.fields.horizontal_velocity_x_m_per_s,
                            backend_metadata_.fields.horizontal_velocity_z_m_per_s,
                            backend_metadata_.fields.momentum_x_m2_per_s,
                            backend_metadata_.fields.momentum_z_m2_per_s,
                            backend_metadata_.fields.face_discharge_m3_per_s);
                ImGui::TextDisabled("No native ledger or tracer claim. Playback/seek controls are "
                                    "not producer controls.");
                ImGui::TextDisabled("Closing detaches the viewer; Stop is explicit.");
            }
            ImGui::Text("Water %.3f million m3", shown_->stats.water_volume_m3 / 1.0e6);
            ImGui::Text("Max depth %.3f m | max speed %.3f m/s", shown_->stats.max_depth_m,
                        shown_->stats.max_speed_m_per_s);
            ImGui::TextWrapped(
                "The published field is held until the next native publication. "
                "Source time above is the actual shown service time. Rain override "
                "is uniform; native fall boundary and terrain remain service-owned.");
            if (health == fluid_25d_project_config_detail::ExternalSessionHealth::Delayed ||
                health == fluid_25d_project_config_detail::ExternalSessionHealth::Disconnected)
                ImGui::TextWrapped("Last field is frozen; heartbeat is not healthy and solver "
                                   "controls are disabled.");

            for (const char* name : {"overview", "runoff", "collection"}) {
                if (ImGui::RadioButton(name, config_.recording_camera == name)) {
                    config_.recording_camera = name;
                    configure_camera();
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            int mode = static_cast<int>(catchment_view_);
            if (ImGui::Combo("3D reading", &mode,
                             "Composite\0Water isolation\0Flow inspection\0")) {
                catchment_view_ = static_cast<Fluid25DCatchmentView>(mode);
                view_ = Fluid25DPresentationView::Catchment;
                quiver_reset_ = true;
            }
            bool diagnostics = view_ == Fluid25DPresentationView::Diagnostics;
            if (ImGui::Checkbox("2D diagnostics [A]", &diagnostics))
                view_ = diagnostics ? Fluid25DPresentationView::Diagnostics
                                    : Fluid25DPresentationView::Catchment;
            if (diagnostics) {
                int debug = static_cast<int>(debug_view_);
                if (ImGui::Combo("Map [D]", &debug,
                                 "Terrain\0Depth\0Surface\0Speed\0Direction\0Wet/dry\0"))
                    debug_view_ = static_cast<Fluid25DDebugView>(debug);
            }
            ImGui::Text("Display time is native physical time; no playback speed or seek.");
            ImGui::TextWrapped("Arrows and optional dots/trails are local visual cues derived from "
                               "the held field, not native particles or conserved dye.");
            ImGui::Text("Esc detaches the viewer; it does not stop the producer.");
        }
        ImGui::End();
    }

    void draw_ui() {
        if (external_session_) {
            draw_external_ui();
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(config_.stream_path ? "Live external mountain runoff"
                                             : "Recorded mountain runoff")) {
            if (config_.stream_path) {
                ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                                   "LIVE EXTERNAL SYNXFLOW - Vulkan viewer only");
                ImGui::Text("Producer: %s | native %.0f s | available %.0f s",
                            recording_->producer_state().c_str(),
                            recording_->latest_native_time_s(), recording_->times_s().back());
                ImGui::Text("Native solver: %s", recording_->native_running()
                                                     ? "COMPUTING"
                                                     : "NOT COMPUTING (import/audit may continue)");
                ImGui::Text("Displayed %.0f s | import lag %.0f physical s", shown_->time_s,
                            recording_->latest_native_time_s() - recording_->times_s().back());
                ImGui::TextWrapped("%s", recording_->producer_message().c_str());
                if (!stream_error_.empty())
                    ImGui::TextWrapped("VIEWER ERROR: %s", stream_error_.c_str());
                if (recording_->producer_state() == "running" && clock_.ended())
                    ImGui::TextUnformatted("WAITING FOR NEXT SNAPSHOT");
                if (ImGui::Checkbox("Follow latest (may skip saved states)", &follow_latest_))
                    reset_visual_history();
                if (ImGui::Button(live_playing_ ? "Pause viewing [Space]"
                                                : "Resume viewing [Space]"))
                    live_playing_ = !live_playing_;
                ImGui::TextWrapped(
                    "Viewing controls never pause computation or change rainfall. Closing this "
                    "window detaches; the launcher owns the finite run.");
            } else {
                ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                                   "RECORDED SYNXFLOW - not a live Cubey solver");
            }
            if (ImGui::CollapsingHeader("Backend details")) {
                ImGui::TextWrapped("Adapter: %s", backend_metadata_.id.c_str());
                ImGui::Text("Input: %.12s | solver bed: %.12s",
                            backend_metadata_.grid.input_sha256.c_str(),
                            backend_metadata_.grid.solver_bed_sha256.c_str());
                ImGui::TextUnformatted("Fields: saved depth, momentum; derived velocity");
                ImGui::TextDisabled("No face discharge, native ledger or tracer fields.");
                ImGui::TextDisabled("Playback controls only; no numerical solver commands.");
                ImGui::Text(
                    "Viewer generation %llu | sequence %llu | playhead %.6f s",
                    static_cast<unsigned long long>(backend_metadata_.session.reset_generation),
                    static_cast<unsigned long long>(backend_metadata_.session.frame_sequence),
                    backend_metadata_.session.physical_time_s);
                ImGui::TextDisabled("Logical playback boundary, not the native producer clock; "
                                    "saved field time below.");
            }
            ImGui::Text("Saved state: %.0f min %.0f s / %.0f min",
                        std::floor(shown_->time_s / 60.0), std::fmod(shown_->time_s, 60.0),
                        recording_->protocol().value("duration_s", recording_->times_s().back()) /
                            60.0);
            const Fluid25DRecordedRainStatus rain = recording_->rain_status_at(shown_->time_s);
            ImGui::Text("Scheduled rain source: %s | %.2f mm/h | %.3f mm scheduled cumulative",
                        fluid_25d_recorded_rain_phase_name(rain.phase), rain.rate_mm_per_hour,
                        rain.cumulative_scheduled_rain_depth_mm);
            if (shown_index_ != clock_.frame_index())
                ImGui::Text("Loading requested state %.0f s...", clock_.time_s());
            else
                ImGui::Text("%s | playhead %.1f s | %.1fx viewing speed",
                            clock_.ended()
                                ? (config_.stream_path ? (recording_->producer_state() == "running"
                                                              ? "WAITING FOR NEXT SNAPSHOT"
                                                              : "END OF FINITE SOURCE")
                                                       : "END OF RECORDING")
                            : clock_.paused() ? "PAUSED"
                                              : "PLAYING",
                            clock_.time_s(), clock_.rate());
            ImGui::BeginDisabled(clock_.ended() || config_.stream_path.has_value());
            if (ImGui::Button(clock_.paused() ? "Play [Space]" : "Pause [Space]"))
                clock_.set_paused(!clock_.paused());
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Restart [R]")) {
                restart_playback();
            }
            ImGui::SameLine();
            if (ImGui::Button("<")) {
                clock_.previous();
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            ImGui::SameLine();
            if (ImGui::Button(">")) {
                clock_.next();
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
                if (playback_session_->metadata().session.lifecycle != Fluid25DLifecycle::Completed)
                    playback_navigation_kind_ = Fluid25DCommandKind::Step;
            }
            float time = static_cast<float>(clock_.time_s() / 60.0);
            if (ImGui::SliderFloat("Seek (minutes)", &time,
                                   static_cast<float>(recording_->times_s().front() / 60.0),
                                   static_cast<float>(recording_->times_s().back() / 60.0),
                                   "%.1f")) {
                clock_.seek(static_cast<double>(time) * 60.0);
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            double taper_start_s = -1.0;
            double rain_zero_s = -1.0;
            const auto rain_history = recording_->rainfall_history();
            for (std::size_t index = 0U; index + 1U < rain_history.size(); ++index) {
                const auto& start = rain_history[index];
                const auto& end = rain_history[index + 1U];
                if (taper_start_s < 0.0 && start.rate_m_per_s > end.rate_m_per_s &&
                    start.rate_m_per_s > 0.0) {
                    taper_start_s = start.time_s;
                }
                if (rain_zero_s < 0.0 && start.rate_m_per_s > 0.0 && end.rate_m_per_s == 0.0) {
                    rain_zero_s = end.time_s;
                }
            }
            const auto seek_to_recorded_time = [this](const char* label, double seek_time_s) {
                if (ImGui::Button(label)) {
                    clock_.seek(seek_time_s);
                    live_playing_ = false;
                    follow_latest_ = false;
                    reset_visual_history();
                }
            };
            if (taper_start_s >= 0.0) {
                seek_to_recorded_time("Rain taper start", taper_start_s);
                ImGui::SameLine();
            }
            if (rain_zero_s >= 0.0) {
                seek_to_recorded_time("Rain zero", rain_zero_s);
                ImGui::SameLine();
                seek_to_recorded_time("Zero +30m",
                                      std::min(rain_zero_s + 1800.0, recording_->times_s().back()));
                ImGui::SameLine();
                seek_to_recorded_time("Zero +1h",
                                      std::min(rain_zero_s + 3600.0, recording_->times_s().back()));
                ImGui::SameLine();
            }
            seek_to_recorded_time("End", recording_->times_s().back());
            float rate = static_cast<float>(clock_.rate());
            if (ImGui::SliderFloat("Playback x", &rate, 0.125F, 300.0F, "%.1fx",
                                   ImGuiSliderFlags_Logarithmic))
                clock_.set_rate(rate);
            ImGui::TextWrapped(
                "Fields are held between saved states (%.0f s here). Seeking pauses and "
                "clears visual history. End holds the final state; restart is explicit.",
                recording_->protocol().value("output_interval_s", 0.0));
            ImGui::Separator();
            for (const char* name : {"overview", "runoff", "collection"}) {
                if (ImGui::RadioButton(name, config_.recording_camera == name)) {
                    config_.recording_camera = name;
                    configure_camera();
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            int mode = static_cast<int>(catchment_view_);
            if (ImGui::Combo("3D reading", &mode,
                             "Composite\0Water isolation\0Flow inspection\0")) {
                catchment_view_ = static_cast<Fluid25DCatchmentView>(mode);
                view_ = Fluid25DPresentationView::Catchment;
                quiver_reset_ = true;
            }
            bool diagnostics = view_ == Fluid25DPresentationView::Diagnostics;
            if (ImGui::Checkbox("2D diagnostics [A]", &diagnostics))
                view_ = diagnostics ? Fluid25DPresentationView::Diagnostics
                                    : Fluid25DPresentationView::Catchment;
            if (diagnostics) {
                int debug = static_cast<int>(debug_view_);
                if (ImGui::Combo("Map [D]", &debug,
                                 "Terrain\0Depth\0Surface\0Speed\0Direction\0Wet/dry\0"))
                    debug_view_ = static_cast<Fluid25DDebugView>(debug);
            }
            ImGui::Text("Depth color: log scale 0.01 / 0.1 / 1 / 10 m");
            if (diagnostics)
                ImGui::Text("2D speed: 0..15 m/s (saturates above 15)");
            ImGui::TextWrapped(
                "Composite attenuates thin rain film. Water isolation shows all wet cells. Arrows: "
                "fixed grid, smoothed neighborhood velocity; 0.025..4 m/s (saturates above 4).");
            ImGui::Text("Storage %.3f million m3 | deepest %.3f m",
                        shown_->stats.water_volume_m3 / 1.0e6, shown_->stats.max_depth_m);
            ImGui::Text("Maximum saved speed %.3f m/s | grid %.0f m",
                        shown_->stats.max_speed_m_per_s, simulation_.cell_size_m);
            ImGui::Text("Peak scheduled rain %.2f mm/h | vertical scale %.2fx",
                        recording_->rain_rate_mm_per_hour(), *render_.terrain_height_scale);
            ImGui::TextWrapped("Source is rainfall over the whole map. Native 'fall' perimeter "
                               "lets runoff leave; there is no point source/drain. Terrain is "
                               "unchanged. No native face ledger or Cubey CFL/oracle claim.");
            if (config_.motion_markers)
                ImGui::TextWrapped("Dots/trails are local visual cues advected in held saved "
                                   "velocities, not native simulated parcels or conserved dye.");
            ImGui::Text("Drag to orbit; scroll to zoom; Esc closes.");
        }
        ImGui::End();
    }

    int run_windowed() {
        host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](host::WindowedAppContext& context) {
            create_resources(context.device(), context.gpu(), context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](host::WindowedAppContext& context) {
            create_render_resources(
                context.device(), render::ColorTargetView{.extent = context.swapchain().extent(),
                                                          .format = context.swapchain().format()});
            graph_.clear();
            graph_.resize(context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](host::WindowedAppContext&) {
            graph_.clear();
            markers_.destroy_render_pipeline();
            resources_.destroy_swapchain_resources();
        };
        callbacks.draw_ui = [this](host::WindowedAppContext&) { draw_ui(); };
        callbacks.update = [this](host::WindowedAppContext& context, const FrameTiming& timing) {
            const auto input = context.filtered_input();
            orbit_.update_pointer_input(input, timing.delta_seconds);
            if (external_session_) {
                const auto lifecycle = external_snapshot_->header.lifecycle;
                const bool terminal = lifecycle == Fluid25DLifecycle::Completed ||
                                      lifecycle == Fluid25DLifecycle::Failed ||
                                      lifecycle == Fluid25DLifecycle::Stopped;
                const bool controls_available = !terminal && external_controls_allowed();
                if (input.key_pressed(input::Key::Space)) {
                    if (controls_available && lifecycle == Fluid25DLifecycle::Running &&
                        fluid_25d_command_supported(backend_metadata_.capabilities,
                                                    Fluid25DControlDomain::Solver,
                                                    Fluid25DCommandKind::Pause))
                        send_external_command(Fluid25DCommandKind::Pause);
                    else if (controls_available &&
                             (lifecycle == Fluid25DLifecycle::Paused ||
                              lifecycle == Fluid25DLifecycle::Ready) &&
                             fluid_25d_command_supported(backend_metadata_.capabilities,
                                                         Fluid25DControlDomain::Solver,
                                                         Fluid25DCommandKind::Resume))
                        send_external_command(Fluid25DCommandKind::Resume);
                }
                if (controls_available && input.key_pressed(input::Key::R) &&
                    fluid_25d_command_supported(backend_metadata_.capabilities,
                                                Fluid25DControlDomain::Solver,
                                                Fluid25DCommandKind::Reset))
                    send_external_command(Fluid25DCommandKind::Reset);
            } else {
                if (input.key_pressed(input::Key::Space)) {
                    if (config_.stream_path)
                        live_playing_ = !live_playing_;
                    else if (!clock_.ended())
                        clock_.set_paused(!clock_.paused());
                }
                if (input.key_pressed(input::Key::R)) {
                    restart_playback();
                }
            }
            if (input.key_pressed(input::Key::A)) {
                view_ = view_ == Fluid25DPresentationView::Diagnostics
                            ? Fluid25DPresentationView::Catchment
                            : Fluid25DPresentationView::Diagnostics;
                quiver_reset_ = true;
            }
            if (input.key_pressed(input::Key::D))
                debug_view_ =
                    static_cast<Fluid25DDebugView>((static_cast<int>(debug_view_) + 1) % 6);
            update_view(timing.delta_seconds);
        };
        callbacks.record_frame = [this](host::WindowedAppContext& context,
                                        const host::WindowedRenderFrame& frame) {
            const vulkan::CommandRecorder recorder(frame.command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            record(context.device(), frame.command_buffer, frame.color_target, frame.frame_slot,
                   Fluid25DRenderTargetMode::Present, context.profile_recorder(),
                   frame.timing.frame_index);
            recorder.end("recorded SynxFlow window");
        };
        callbacks.shutdown = [this](host::WindowedAppContext&) { shutdown(); };
        return host::run_windowed_app(
            {.run_config = config_.common,
             .app_name = "fluid_25d",
             .ready_status =
                 external_session_ ? "attached to external fluid service (draw-only viewer)"
                 : config_.stream_path
                     ? "rendering live external SynxFlow snapshots (no hydraulic solve)"
                     : "rendering recorded SynxFlow mountain runoff (no hydraulic solve)",
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
             .close_on_escape = true},
            std::move(callbacks));
    }

    int run_headless() {
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](host::HeadlessPngContext& context) {
            create_resources(context.device(), context.gpu(),
                             host::headless_capture_frame_slot_count(config_.common));
            create_render_resources(context.device(), context.render_target());
        };
        callbacks.record_frame = [this](host::HeadlessPngContext& context,
                                        const host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer commands,
                                        const host::HeadlessRenderTarget& target) {
            if (external_session_) {
                visual_delta_ = 0.0;
                record(context.device(), commands, target, frame.frame_slot,
                       Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(),
                       frame.index);
                std::printf(
                    "fluid_25d_external_capture: output_frame=%u physical_s=%.9f "
                    "generation=%llu sequence=%llu camera=%s hydraulic_dispatches=0\n",
                    frame.index, shown_->time_s,
                    static_cast<unsigned long long>(external_snapshot_->header.reset_generation),
                    static_cast<unsigned long long>(external_snapshot_->header.sequence),
                    config_.recording_camera.c_str());
                return;
            }
            // Load by immutable capture index on the GPU-owner callback. The
            // video producer may already be preparing later output frames.
            const bool video = config_.common.capture_mode == CaptureMode::Video;
            const double time = std::min(recording_->times_s().back(),
                                         static_cast<double>(config_.recording_time_seconds) +
                                             (video ? static_cast<double>(frame.index) *
                                                          config_.recording_frame_interval_seconds
                                                    : 0.0));
            const double previous = shown_->time_s;
            clock_.seek(time);
            const auto index = clock_.frame_index();
            if (index != shown_index_) {
                accept_frame(recording_->frame(index));
                shown_index_ = index;
            }
            // A sparse recording can jump far in physical time. Bound only
            // the optional approximate visual history, never the saved fields.
            visual_delta_ = video ? std::clamp(shown_->time_s - previous, 0.0, 60.0) : 0.0;
            record(context.device(), commands, target, frame.frame_slot,
                   Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(),
                   frame.index);
            std::printf("fluid_25d_recording_capture: output_frame=%u requested_s=%.0f "
                        "saved_s=%.0f camera=%s hydraulic_dispatches=0\n",
                        frame.index, time, shown_->time_s, config_.recording_camera.c_str());
        };
        callbacks.shutdown = [this](host::HeadlessPngContext&) { shutdown(); };
        host::HeadlessPngHost host(
            {.run_config = config_.common,
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
            std::move(callbacks));
        return host.run();
    }

    Fluid25DProjectConfig config_;
    std::shared_ptr<Fluid25DRecording> recording_;
    std::unique_ptr<Fluid25DExternalSession> external_session_;
    Fluid25DBackendMetadata backend_metadata_;
    std::optional<Fluid25DLocalSession> playback_session_{};
    std::optional<Fluid25DCommandKind> playback_navigation_kind_{};
    double playback_applied_rate_ = 1.0;
    std::array<double, 1> static_clock_times_{0.0};
    Fluid25DRecordingPlayback clock_;
    std::shared_ptr<const Fluid25DRecordedFrame> shown_;
    std::future<std::shared_ptr<const Fluid25DRecordedFrame>> pending_;
    std::future<std::shared_ptr<Fluid25DRecording>> stream_pending_;
    std::future<std::optional<Fluid25DExternalSnapshot>> external_pending_;
    std::chrono::steady_clock::time_point next_stream_poll_{};
    std::chrono::steady_clock::time_point next_external_poll_{};
    std::chrono::steady_clock::time_point external_last_valid_publication_steady_{};
    std::string stream_error_{};
    std::string external_error_{};
    std::uint64_t external_next_command_id_ = 1U;
    std::optional<std::uint64_t> external_pending_command_id_{};
    std::optional<Fluid25DCommandKind> external_pending_command_kind_{};
    double external_published_unix_s_ = 0.0;
    double external_command_recorded_unix_s_ = 0.0;
    float external_rain_input_mm_per_hour_ = 0.0F;
    float external_time_scale_input_ = 1.0F;
    std::optional<Fluid25DExternalSnapshot> external_snapshot_{};
    bool external_terminal_ = false;
    bool external_rain_dirty_ = false;
    bool external_time_scale_dirty_ = false;
    bool live_playing_ = false, follow_latest_ = false;
    std::exception_ptr shutdown_failure_{};
    std::size_t shown_index_ = 0U, pending_index_ = 0U;
    std::uint64_t pending_viewer_generation_ = 0U;
    std::vector<Fluid25DVelocityGpu> packed_velocity_;
    Fluid25DConfig simulation_;
    Fluid25DScenarioData scenario_;
    Fluid25DCatchmentRenderOptions render_;
    ProjectRuntimeAdapter runtime_{1};
    Fluid25DGpuResources resources_;
    Fluid25DMotionMarkers markers_;
    render::RenderGraphFrameExecutor graph_;
    Camera3D camera_;
    OrbitController orbit_;
    math::Vec3 target_{};
    Fluid25DPresentationView view_;
    Fluid25DCatchmentView catchment_view_;
    Fluid25DDebugView debug_view_;
    bool cue_reset_ = true, quiver_reset_ = true, marker_reset_ = true, upload_pending_ = true;
    bool hydraulic_reset_never_used_ = false;
    bool reset_on_load_ = false;
    double visual_delta_ = 0.0, marker_accumulator_ = 0.0;
    float marker_fraction_ = 1.0F;
};
} // namespace

int run_fluid_25d_recording(const Fluid25DProjectConfig& config) {
    RecordingApp app(config);
    return app.run();
}
} // namespace cubey::projects::fluid::fluid_25d
