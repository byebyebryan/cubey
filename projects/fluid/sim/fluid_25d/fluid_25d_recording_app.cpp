#include "fluid_25d_recording_app.h"

#include "fluid_25d_backend_adapters.h"
#include "fluid_25d_bank_comparison.h"
#include "fluid_25d_bank_controls.h"
#include "fluid_25d_commands.h"
#include "fluid_25d_display_coverage.h"
#include "fluid_25d_external_session.h"
#include "fluid_25d_local_session.h"
#include "fluid_25d_motion_markers.h"
#include "fluid_25d_native_presentation_controls.h"
#include "fluid_25d_project_config.h"
#include "fluid_25d_recording.h"
#include "fluid_25d_scenic.h"
#include "fluid_25d_scenic_environment.h"
#include "fluid_25d_terrain_demo.h"
#include "fluid_25d_terrain_surface.h"

#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/engine/terrain_surface_environment.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/performance_ui.h>
#include <cubey/host/terrain_surface_environment_ui.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/memory_barriers.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

struct WhitewaterBuild {
    Fluid25DWhitewaterSelection selection;
    double source_s = 0.0, cpu_ms = 0.0, published_unix_s = 0.0;
    std::uint64_t epoch = 0U;
};

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
class RecordingApp;
class ManagedSceneReload {
  public:
    explicit ManagedSceneReload(RecordingApp& initial);
    ~ManagedSceneReload();
    RecordingApp& scene() {
        return *active_;
    }
    Fluid25DTerrainDemoControl* control() {
        return control_.get();
    }
    const std::string& current_case_id() const {
        return current_case_id_;
    }
    const std::string& stage() const {
        return stage_;
    }
    std::string selected_case_id;
    bool restart_allowed() const;
    void request_case(const std::string& id);
    void poll(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots,
              render::ColorTargetView target, const std::function<void()>& wait_frames);
    void shutdown(vulkan::GpuRuntime& gpu);
    bool guide_available() const;
    bool guide_active() const {
        return !guide_actions_.empty() && !guide_finished_;
    }
    bool guide_finished() const {
        return guide_finished_;
    }
    const std::string& guide_note() const {
        return guide_note_;
    }
    void start_guide();
    void cancel_guide(const char* reason);

  private:
    void run_script();
    void run_sequence(const std::vector<Fluid25DTerrainDemoAction>& actions, std::size_t& index,
                      bool& waiting, std::chrono::steady_clock::time_point& started,
                      std::optional<std::uint64_t>& expected_ack, bool guided);
    void run_guide();
    void log(const std::string& event);
    RecordingApp* active_;
    std::unique_ptr<RecordingApp> installed_;
    std::unique_ptr<Fluid25DTerrainDemoControl> control_;
    std::future<std::unique_ptr<RecordingApp>> prepared_;
    std::optional<vulkan::GpuJobHandle<std::unique_ptr<RecordingApp>>> uploaded_;
    std::string current_case_id_, stage_;
    std::uint64_t preparing_request_id_ = 0;
    std::chrono::steady_clock::time_point next_poll_{}, action_started_{};
    std::vector<Fluid25DTerrainDemoAction> script_;
    std::size_t action_index_ = 0;
    bool action_waiting_ = false;
    std::vector<Fluid25DTerrainDemoAction> guide_actions_;
    std::size_t guide_index_ = 0;
    bool guide_waiting_ = false, guide_finished_ = false;
    std::chrono::steady_clock::time_point guide_action_started_{};
    std::string guide_note_;
    std::optional<std::uint64_t> action_ack_id_, guide_ack_id_;
};

class RecordingApp {
    friend class ManagedSceneReload;

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

    int run() {
        const int status = config_.common.headless ? run_headless() : run_windowed();
        if (shutdown_failure_)
            std::rethrow_exception(shutdown_failure_);
        return stream_error_.empty() && external_error_.empty() && comparison_error_.empty()
                   ? status
                   : 1;
    }

  private:
    void activate_comparison_cache() {
        std::printf("fluid_25d_bank_cache: READY frames=%zu bytes=%zu preparation_ms=%.6f "
                    "budget_bytes=%zu\n",
                    coverage_cache_->frame_count(), coverage_cache_->bytes(),
                    coverage_cache_->preparation_ms(), Fluid25DDisplayCoverageCache::kByteBudget);
        set_bank_view(bank_view_);
    }

    void poll_comparison_cache() {
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

    [[nodiscard]] bool matching_mask_ready() const {
        return coverage_ && coverage_->has_frame(shown_->time_s) &&
               (!comparison_ || coverage_cache_.has_value());
    }

    [[nodiscard]] std::span<const float> active_coverage() const {
        return coverage_cache_ ? coverage_cache_->frame(shown_->time_s)
                               : std::span<const float>(coverage_values_);
    }

    void set_bank_view(Fluid25DBankView view) {
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

    void cycle_bank_view() {
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

    void seek_playback(double time) {
        clock_.seek(comparison_ ? comparison_->map(time, false).time_s : time);
        if (comparison_)
            comparison_at_end_ = comparison_->at_end(clock_.time_s());
        comparison_end_reported_ = false;
        live_playing_ = follow_latest_ = false;
        reset_visual_history();
    }

    void toggle_playback() {
        if (comparison_ && (!coverage_cache_ || !comparison_error_.empty()))
            return;
        if (comparison_ && comparison_->at_end(clock_.time_s())) {
            seek_playback(comparison_->first_s());
            clock_.set_paused(false);
        } else if (!clock_.ended())
            clock_.set_paused(!clock_.paused());
    }

    void constrain_comparison_clock(bool was_playing) {
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
            external_publication_age_s(), external_reset_started_.has_value(),
            external_reset_started_
                ? std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                *external_reset_started_)
                      .count()
                : 0.0);
    }

    [[nodiscard]] bool external_controls_allowed() const {
        return external_session_ && !managed_held_ && !external_terminal_ &&
               external_error_.empty() && !external_session_->command_pending() &&
               external_health() == fluid_25d_project_config_detail::ExternalSessionHealth::Healthy;
    }

    void fail_external_session(const std::string& message) {
        if (external_error_.empty()) {
            external_error_ = message;
            std::fprintf(stderr, "fluid_25d_external: %s\n", external_error_.c_str());
        }
    }

    void accept_frame(std::shared_ptr<const Fluid25DRecordedFrame> frame) {
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

    void prepare_whitewater_sources() {
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

    void reset_presentation_history() {
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

    void reset_visual_history() {
        reset_presentation_history();
        reset_on_load_ = true;
        if (playback_session_)
            playback_navigation_kind_ = Fluid25DCommandKind::Seek;
    }

    void restart_playback() {
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

    void send_external_command(Fluid25DCommandKind kind, std::optional<double> value = {},
                               bool guided = false) {
        if (!external_controls_allowed())
            return;
        if (managed_demo_ && !guided)
            managed_demo_->cancel_guide("Guide stopped; manual solver control now applies.");
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

    std::optional<Fluid25DExternalSnapshot> timed_external_read() {
        const auto started = std::chrono::steady_clock::now();
        auto result = external_session_->load_latest();
        external_read_ms_.store(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count());
        ++external_read_jobs_;
        return result;
    }

    void update_view(double wall_delta_s) {
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

    void create_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots) {
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

    void ensure_scenic_resources(vulkan::Device& device, render::ColorTargetView target) {
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

    void create_render_resources(vulkan::Device& device, render::ColorTargetView target) {
        resources_.create_render_pipelines(device, target.format, VK_FORMAT_D32_SFLOAT,
                                           target.extent, true);
        if (markers_available_)
            markers_.create_render_pipeline(device, target.format, VK_FORMAT_D32_SFLOAT,
                                            target.extent, true);
        if (render_.native_presentation == 3U)
            ensure_scenic_resources(device, target);
    }

    void select_camera(const char* name, bool guided = false) {
        if (managed_demo_ && !guided)
            managed_demo_->cancel_guide(
                "Manual camera selection; guide stopped. Solver continues.");
        config_.recording_camera = name;
        config_.native_camera_target_x.reset();
        config_.native_camera_target_z.reset();
        config_.native_camera_span_m.reset();
        configure_camera();
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

    Fluid25DRenderCamera camera(VkExtent2D extent, std::uint64_t frame_index = 0U) const {
        float yaw = config_.native_camera_yaw_radians + orbit_.yaw();
        float distance = orbit_.distance();
        if (config_.native_camera_sweep_radians > 0.0F) {
            const auto count = host::headless_capture_frame_count(config_.common);
            const float progress =
                std::clamp(float(frame_index) / float(std::max(count, 2U) - 1U), 0.0F, 1.0F);
            yaw += (progress - 0.5F) * config_.native_camera_sweep_radians;
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
        return {camera_.view_projection_matrix(transform, static_cast<float>(extent.width) /
                                                              static_cast<float>(extent.height)),
                transform.translation};
    }

    void record(vulkan::Device& device, VkCommandBuffer commands, render::ColorTargetView target,
                render::FrameSlot slot, Fluid25DRenderTargetMode target_mode,
                profiling::ProfileRecorder* profile, std::uint64_t frame_index) {
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

    void collect_native_timings(std::uint32_t slot, profiling::ProfileRecorder* profile) {
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

    bool scenic_material_active() const {
        return render_.native_presentation == 3U && view_ == Fluid25DPresentationView::Catchment &&
               catchment_view_ == Fluid25DCatchmentView::Composite &&
               render_.native_water_debug == 0U;
    }

    std::optional<double> applied_rain_mm_per_hour() const {
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

    Fluid25DRainVisualFrame rain_visual_frame() const {
        const bool shaded = scenic_material_active() &&
                            config_.native_scenic_terrain_view == "shaded" &&
                            (config_.native_scenic_water_view == "shaded" ||
                             config_.native_scenic_water_view == "no-bank-edge");
        const auto applied_rate = applied_rain_mm_per_hour();
        return {fluid_25d_rain_visual_count(config_.native_rain_visuals && shaded, applied_rate,
                                            config_.native_rain_strength),
                rain_clock_.seconds(), rain_clock_.motion_seconds(), applied_rate};
    }

    void log_rain_capture(std::uint32_t frame) const {
        const auto rain = rain_visual_frame();
        const auto rate = applied_rain_mm_per_hour();
        std::printf("fluid_25d_rain_capture: output_frame=%u applied_mm_h=%.9f known=%u "
                    "clock_s=%.9f streaks=%u enabled=%u strength=%.3f "
                    "fall_speed=%.3f motion_s=%.9f hydraulic_writes=0\n",
                    frame, rate.value_or(0.0), rate ? 1U : 0U, rain.clock_s, rain.streak_count,
                    config_.native_rain_visuals ? 1U : 0U, config_.native_rain_strength,
                    config_.native_rain_speed, rain.motion_s);
    }

    void draw_native_color_legend() const {
        if (scenic_material_active())
            ImGui::TextWrapped(
                "Scenic color = materials/reflection/optical depth, not a depth scale. "
                "Dots/trails = approximate velocity, not water parcels.");
        else
            ImGui::TextWrapped(
                "Blue = depth. Dots/trails = approximate velocity, not water parcels.");
    }

    void select_native_style(std::uint32_t style) {
        const bool compare_materials = render_.native_presentation >= 2U && style >= 2U;
        render_.native_presentation = style;
        config_.native_presentation = style == 3U   ? "scenic"
                                      : style == 2U ? "readable"
                                      : style == 1U ? "motion"
                                                    : "original";
        render_.native_surface_highlights = fluid_25d_native_surface_highlights_enabled(
            fluid_25d_native_surface_highlights_policy(config_.native_surface_highlights), style);
        // Readable/Scenic comparisons preserve the orbit and dot history.
        // Original/Motion retain their historical framing/reset behavior.
        if (!compare_materials) {
            reset_presentation_history();
            configure_camera();
        }
        std::printf("fluid_25d_native_style: mode=%s\n", config_.native_presentation.c_str());
        std::fflush(stdout);
    }

    void report_scenic_material() const {
        const auto json = fluid_25d_scenic_material_json(scenic_material_);
        const bool dots_draw = config_.motion_markers &&
                               config_.native_scenic_terrain_view == "shaded" &&
                               config_.native_scenic_water_view == "shaded";
        std::printf("fluid_25d_scenic_terrain: view=%s water_draw=%u dots_draw=%u\n",
                    config_.native_scenic_terrain_view.c_str(),
                    config_.native_scenic_terrain_view == "shaded" ? 1U : 0U, dots_draw ? 1U : 0U);
        std::printf("fluid_25d_scenic_material: profile=%s%s%s settings=%s\n",
                    config_.native_scenic_material.c_str(),
                    config_.native_scenic_tuning_path ? "+tuning" : "",
                    scenic_material_edited_ ? "+edited" : "", json.c_str());
        std::printf("fluid_25d_scenic_surface: mode=%s source_bound=%u\n",
                    config_.native_scenic_surface_mode.c_str(), terrain_surface_ ? 1U : 0U);
        std::printf(
            "fluid_25d_scenic_water: shading=wet-ground view=%s pipeline=normal dots_draw=%u "
            "coverage=retained\n",
            config_.native_scenic_water_view.c_str(), dots_draw ? 1U : 0U);
        std::fflush(stdout);
    }

    void draw_native_presentation_ui() {
        if (ImGui::TreeNode("Environment (render only)")) {
            bool shared = config_.environment_mode == "shared";
            if (ImGui::Checkbox("Shared clear sky and day/night", &shared))
                config_.environment_mode = shared ? "shared" : "fixed";
            if (shared && weather_)
                host::draw_terrain_surface_environment_controls(*weather_);
            if (!shared)
                ImGui::TextDisabled("Accepted fixed daylight reference. No environment clock.");
            ImGui::TreePop();
        }
        int style = static_cast<int>(render_.native_presentation);
        ImGui::SetNextItemWidth(-100.0F);
        if (ImGui::Combo("Style", &style,
                         "Original reference\0Continuous motion\0Readable terrain/water\0Scenic "
                         "(opt-in HDR)\0")) {
            select_native_style(static_cast<std::uint32_t>(style));
        }
        int highlight_policy = static_cast<int>(
            fluid_25d_native_surface_highlights_policy(config_.native_surface_highlights));
        ImGui::SetNextItemWidth(-100.0F);
        ImGui::BeginDisabled(render_.native_presentation == 3U);
        if (ImGui::Combo("Highlights", &highlight_policy,
                         "Auto (off in Readable/Scenic)\0On\0Off\0")) {
            config_.native_surface_highlights = highlight_policy == 1   ? "on"
                                                : highlight_policy == 2 ? "off"
                                                                        : "auto";
            render_.native_surface_highlights = fluid_25d_native_surface_highlights_enabled(
                fluid_25d_native_surface_highlights_policy(config_.native_surface_highlights),
                render_.native_presentation);
        }
        ImGui::EndDisabled();
        if (render_.native_presentation == 3U)
            ImGui::TextWrapped(
                "Scenic: HDR materials and decorative flow normals; V compares Readable. "
                "Raw maps and inspection views retain diagnostic shading.");
        if (render_.native_presentation == 3U &&
            ImGui::CollapsingHeader("Visible rain (render only)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show falling rain", &config_.native_rain_visuals);
            ImGui::SliderFloat("Rain visual strength", &config_.native_rain_strength, 0.0F, 2.0F);
            ImGui::SliderFloat("Rain fall speed", &config_.native_rain_speed, 0.25F, 8.0F, "%.2fx");
            const auto rain = applied_rain_mm_per_hour();
            if (rain)
                ImGui::Text("Applied weather %.2f mm/h | %u streaks", *rain,
                            rain_visual_frame().streak_count);
            else
                ImGui::TextUnformatted("Applied weather unavailable; no rain drawn");
            ImGui::TextWrapped("Artistic macro-scale streaks, not water parcels. Strength only "
                               "changes their density, never the solver supply. Fall speed is "
                               "independent of simulation speed; 1x is the original slow look. "
                               "Pause freezes motion. Component views hide rain.");
        }
        if (render_.native_presentation == 3U &&
            ImGui::CollapsingHeader("Scenic materials (render only)")) {
            int profile = config_.native_scenic_material == "macro"     ? 3
                          : config_.native_scenic_material == "terrain" ? 2
                          : config_.native_scenic_material == "refined" ? 1
                                                                        : 0;
            ImGui::SetNextItemWidth(-100.0F);
            if (ImGui::Combo(
                    "Material", &profile,
                    "V1 reference\0Refined\0Terrain study (opt-in)\0Mountain demo daylight\0")) {
                config_.native_scenic_material = profile == 3   ? "macro"
                                                 : profile == 2 ? "terrain"
                                                 : profile == 1 ? "refined"
                                                                : "v1";
                config_.native_scenic_tuning_path.reset();
                scenic_material_edited_ = false;
                scenic_material_ = fluid_25d_scenic_material(config_.native_scenic_material);
                report_scenic_material();
            }
            // Edits only affect the next frame's material uniforms, not playback history.
            int surface_mode =
                int(fluid_25d_terrain_surface_mode(config_.native_scenic_surface_mode));
            ImGui::BeginDisabled(!terrain_surface_);
            if (ImGui::Combo(
                    "Terrain organization", &surface_mode,
                    "Legacy\0Landform masks\0Climate + landform\0Source-aligned masks (study)\0")) {
                constexpr std::array names{"legacy", "landform", "climate", "correlated"};
                config_.native_scenic_surface_mode = names[std::size_t(surface_mode)];
                report_scenic_material();
            }
            ImGui::EndDisabled();
            if (terrain_surface_ && terrain_surface_->site) {
                ImGui::TextWrapped("World site: %s. Patch-only view: rain, water and terrain "
                                   "cover the %.3f km simulation square. Wider backdrop terrain "
                                   "belongs to the Terrain application.",
                                   terrain_surface_->site->id.c_str(),
                                   float(simulation_.grid_width) * simulation_.cell_size_m /
                                       1000.0F);
            }
            ImGui::TextWrapped(terrain_surface_
                                   ? "Climate is a long-term material prior, not current rain or "
                                     "wetness. Masks are "
                                     "baked once; elevations and hydraulic fields are unchanged."
                                   : "Terrain organization study requires a matching "
                                     "--fluid25d-scenic-surface-source.");
            constexpr std::array water_views{
                "shaded",           "environment-only", "direct-only",   "transmission-only",
                "no-environment",   "no-direct",        "no-clarity",    "no-detail",
                "depth-bands",      "coverage",         "film-weight",   "roughness",
                "rapid-activity",   "rapid-foam",       "no-rapid-foam", "cascade-weight",
                "landing-activity", "no-landing-foam",  "bank-edge",     "no-bank-edge"};
            int water_view =
                static_cast<int>(fluid_25d_scenic_water_view(config_.native_scenic_water_view));
            if (ImGui::Combo("Water component", &water_view, water_views.data(),
                             static_cast<int>(water_views.size()))) {
                config_.native_scenic_water_view =
                    water_views[static_cast<std::size_t>(water_view)];
                if (water_view != 0)
                    config_.native_scenic_terrain_view = "shaded";
                report_scenic_material();
            }
            if (water_view != 0)
                ImGui::TextWrapped(
                    "Water-only contribution over terrain; dots hidden. Depth bands: yellow below "
                    "1cm, orange 1-5cm, cyan above 5cm. Not a simulation mode.");
            bool edited =
                ImGui::SliderFloat("Wet roughness", &scenic_material_.wet_roughness, 0.2F, 1.0F);
            edited |=
                ImGui::SliderFloat("Shallow bed tint", &scenic_material_.water_clarity, 0.0F, 1.0F);
            if (ImGui::TreeNode("Water appearance and daylight")) {
                ImGui::TextDisabled("Optional bank-edge treatment (off by default)");
                edited |=
                    ImGui::SliderFloat("Bank irregularity (m)",
                                       &scenic_material_.water_bank_irregularity_m, 0.0F, 24.0F);
                edited |= ImGui::SliderFloat("Bank water motion (m)",
                                             &scenic_material_.water_bank_motion_m, 0.0F, 12.0F);
                if (scenic_material_.water_bank_irregularity_m > 0.0F ||
                    scenic_material_.water_bank_motion_m > 0.0F) {
                    edited |= ImGui::SliderFloat(
                        "Bank noise scale (m)", &scenic_material_.water_bank_scale_m, 8.0F, 128.0F);
                    edited |= ImGui::SliderFloat("Bank edge band (m)",
                                                 &scenic_material_.water_bank_band_m, 1.0F, 64.0F);
                    if (ImGui::SmallButton("Disable bank-edge treatment")) {
                        scenic_material_.water_bank_irregularity_m = 0.0F;
                        scenic_material_.water_bank_motion_m = 0.0F;
                        edited = true;
                    }
                    ImGui::TextWrapped(
                        "Experimental water-edge trimming only; terrain stays fixed. "
                        "B-spline mode follows reconstructed depth and slope, not cell edges. "
                        "Motion follows filtered flow and pauses with playback. "
                        "Insets are capped relative to the native cell and local depth. "
                        "Above 8/3 m, caps relax for an exaggerated comparison; narrow "
                        "streams can visibly retreat or break. "
                        "No new wet area; raw/component views bypass it.");
                }
                edited |= ImGui::SliderFloat("Flow surface agitation",
                                             &scenic_material_.water_flow_agitation, 0.0F, 1.0F);
                edited |= ImGui::SliderFloat("Rain surface agitation",
                                             &scenic_material_.water_rain_agitation, 0.0F, 1.0F);
                edited |= ImGui::SliderFloat("Steep-flow rapid material",
                                             &scenic_material_.water_rapid_strength, 0.0F, 1.0F);
                edited |= ImGui::SliderFloat("Cascade streaks",
                                             &scenic_material_.water_cascade_strength, 0.0F, 1.0F);
                edited |= ImGui::SliderFloat("Landing foam cue",
                                             &scenic_material_.water_landing_strength, 0.0F, 1.0F);
                const float old_whitewater = scenic_material_.water_whitewater_strength;
                edited |= ImGui::SliderFloat(
                    "Whitewater flecks", &scenic_material_.water_whitewater_strength, 0.0F, 1.0F);
                if (old_whitewater == 0.0F && scenic_material_.water_whitewater_strength > 0.0F)
                    whitewater_dirty_ = true;
                edited |=
                    ImGui::SliderFloat("Gentler-stream foam",
                                       &scenic_material_.water_stream_foam_strength, 0.0F, 1.0F);
                if (scenic_material_.water_stream_foam_strength > 0.0F) {
                    edited |= ImGui::SliderFloat("Gentle foam patchiness",
                                                 &scenic_material_.water_stream_foam_patchiness,
                                                 0.0F, 1.0F);
                    edited |= ImGui::SliderFloat("Gentle foam brightness",
                                                 &scenic_material_.water_stream_foam_brightness,
                                                 0.0F, 1.0F);
                    ImGui::TextWrapped(
                        "Patchiness leaves irregular clear-water gaps; brightness changes "
                        "the lit foam tint. Neither changes current or fleck speed. "
                        "Patchiness 0 / brightness 1 restores retained dense foam.");
                }
                if (scenic_material_.water_whitewater_strength > 0.0F)
                    ImGui::TextWrapped("3D flecks: %zu/%zu sources; %.1fx cosmetic travel. "
                                       "Speed/size are startup tuning settings.",
                                       whitewater_selection_.seeds.size(),
                                       whitewater_selection_.requested,
                                       double(scenic_material_.water_whitewater_speed));
                if (scenic_material_.water_rapid_strength > 0.0F ||
                    scenic_material_.water_cascade_strength > 0.0F ||
                    scenic_material_.water_landing_strength > 0.0F)
                    edited |=
                        ImGui::SliderFloat("Rapid texture period (m)",
                                           &scenic_material_.water_rapid_scale_m, 64.0F, 384.0F);
                ImGui::TextWrapped("Downhill-water appearance. Cascade motion is artist "
                                   "exaggerated; landing foam is a local steep-to-flat heuristic. "
                                   "Texture period is an "
                                   "artist scale, not wave height or transported foam. "
                                   "Water footprint and numerical fields stay unchanged.");
                ImGui::TextWrapped("Render-only activity proxies. Rain response uses applied "
                                   "weather even when streaks are hidden. Zero restores retained "
                                   "shading; no water mass, banks or solver changes.");
                edited |= ImGui::SliderFloat("Wet-only water normals",
                                             &scenic_material_.water_wet_normal, 0.0F, 1.0F);
                edited |= ImGui::SliderFloat("Wind ripple slope",
                                             &scenic_material_.water_ripple_strength, 0.0F, 0.3F);
                if (scenic_material_.water_ripple_strength > 0.0F)
                    edited |=
                        ImGui::SliderFloat("Ripple wavelength (m)",
                                           &scenic_material_.water_ripple_scale_m, 8.0F, 128.0F);
                ImGui::TextWrapped(
                    "Wind ripples are decorative normal detail, not simulated water motion.");
                bool shared_daylight = scenic_material_.daylight_environment > 0.5F;
                ImGui::BeginDisabled(config_.environment_mode == "shared");
                if (ImGui::Checkbox("Shared fixed daylight", &shared_daylight)) {
                    scenic_material_.daylight_environment = shared_daylight ? 1.0F : 0.0F;
                    edited = true;
                }
                if (shared_daylight) {
                    edited |= ImGui::SliderFloat("Daylight exposure (EV)",
                                                 &scenic_material_.daylight_exposure, -6.0F, 4.0F);
                }
                ImGui::EndDisabled();
                if (shared_daylight || config_.environment_mode == "shared") {
                    edited |= ImGui::SliderFloat(config_.environment_mode == "shared"
                                                     ? "Direct-light artistic gain"
                                                     : "Daylight sun strength",
                                                 &scenic_material_.daylight_sun_scale, 0.0F, 2.0F);
                    ImGui::TextWrapped("Sky diffuse and reflections share one captured atmosphere. "
                                       "Sun strength is an artistic balance, not rainfall.");
                }
                ImGui::TreePop();
            }
            edited |=
                ImGui::SliderFloat("Film roughness", &scenic_material_.film_roughness, 0.2F, 0.8F);
            edited |= ImGui::SliderFloat("Film ground emphasis", &scenic_material_.film_ground_mix,
                                         0.0F, 1.0F);
            edited |=
                ImGui::SliderFloat("Film transition end (m)", &scenic_material_.film_end_m,
                                   std::max(0.002F, scenic_material_.film_begin_m + 0.001F), 1.0F);
            edited |=
                ImGui::SliderFloat("Shallow coverage fade",
                                   &scenic_material_.water_shallow_coverage_strength, 0.0F, 1.0F);
            if (scenic_material_.water_shallow_coverage_strength > 0.0F)
                edited |=
                    ImGui::SliderFloat("Coverage full depth (m)",
                                       &scenic_material_.water_shallow_coverage_end_m, 0.05F, 1.0F);
            ImGui::TextWrapped("Shallow coverage fade exposes existing wet ground; it does not "
                               "create smaller streams or change simulated water. Zero retains "
                               "coverage. Raw depth/component views ignore this artist fade.");
            edited |= ImGui::SliderFloat("Neutral terrain",
                                         &scenic_material_.terrain_material_blend, 0.0F, 1.0F);
            bool corrected = scenic_material_.terrain_normal_strength >= 0.0F;
            if (ImGui::Checkbox("Surface-gradient terrain normals", &corrected)) {
                scenic_material_.terrain_normal_strength = corrected ? 0.12F : -1.0F;
                edited = true;
            }
            if (corrected)
                edited |= ImGui::SliderFloat("Terrain normal strength",
                                             &scenic_material_.terrain_normal_strength, 0.0F, 0.5F);
            edited |= ImGui::SliderFloat("Terrain ambient softening",
                                         &scenic_material_.terrain_ambient_softening, 0.0F, 1.0F);
            bool integrated = scenic_material_.terrain_diffuse_convolution > 0.0F;
            if (ImGui::Checkbox("Integrated terrain diffuse", &integrated)) {
                scenic_material_.terrain_diffuse_convolution = integrated ? 1.0F : 0.0F;
                edited = true;
            }
            edited |= ImGui::SliderFloat("Terrain specular",
                                         &scenic_material_.terrain_specular_scale, 0.0F, 1.0F);
            edited |= ImGui::SliderFloat("Terrain shadows", &scenic_material_.terrain_shadow_scale,
                                         0.0F, 1.0F);
            if (ImGui::Checkbox("Whole-scene shadow fit", &render_.native_shadow_scene_fit))
                config_.native_shadow_projection =
                    render_.native_shadow_scene_fit ? "scene" : "legacy";
            edited |= ImGui::SliderFloat("Slope color contrast",
                                         &scenic_material_.terrain_slope_color_scale, 0.0F, 1.0F);
            int readability = int(scenic_material_.terrain_readability);
            if (ImGui::Combo(
                    "Terrain detail", &readability,
                    "Previous reference\0Periodic detail off\0Readable (Macro default)\0")) {
                scenic_material_.terrain_readability = float(readability);
                edited = true;
            }
            constexpr std::array terrain_views{
                "shaded",        "terrain-only", "albedo",      "weights",    "base-normal",
                "detail-normal", "roughness",    "direct",      "ambient",    "constant-albedo",
                "no-detail",     "face-normal",  "no-specular", "no-shadows", "specular-only"};
            int terrain_view =
                static_cast<int>(fluid_25d_scenic_terrain_view(config_.native_scenic_terrain_view));
            if (ImGui::Combo("Terrain component", &terrain_view, terrain_views.data(),
                             static_cast<int>(terrain_views.size()))) {
                config_.native_scenic_terrain_view =
                    terrain_views[static_cast<std::size_t>(terrain_view)];
                if (terrain_view != 0)
                    config_.native_scenic_water_view = "shaded";
                report_scenic_material();
            }
            if (terrain_view != 0)
                ImGui::TextWrapped("Terrain-only diagnostic: water and dots hidden, native fields "
                                   "unchanged. Colors retain Scenic tonemapping.");
            if (edited) {
                scenic_material_edited_ = true;
                report_scenic_material();
            }
            if (config_.native_scenic_tuning_path || scenic_material_edited_)
                ImGui::TextWrapped("Custom settings; selecting a preset discards these overrides.");
            ImGui::TextWrapped(
                "Thin water uses wet-ground shading. Shallow bed tint is an artistic "
                "color cue, not transparency or a depth reading. Material edits "
                "do not move terrain or modify water fields. Bank controls trim only "
                "the displayed water edge. JSON overrides load "
                "once; effective settings are logged.");
        }
        const auto selected = render_.native_display_coverage  ? Fluid25DBankView::MarchingSquares
                              : render_.native_bspline_surface ? Fluid25DBankView::Bspline2x
                                                               : Fluid25DBankView::Reference;
        constexpr std::array<const char*, 3> names{"Triangular reference",
                                                   "B-spline 2x (experimental)",
                                                   "Marching-squares coverage (recorded)"};
        const bool advanced =
            render_.native_bilinear_water ||
            (render_.native_bspline_surface && render_.native_surface_subdivision != 2U);
        ImGui::BeginDisabled(comparison_ && !coverage_cache_);
        ImGui::SetNextItemWidth(-100.0F);
        if (ImGui::BeginCombo("Banks [S]", advanced ? "Advanced reconstruction"
                                                    : names[static_cast<std::size_t>(selected)])) {
            for (std::size_t i = 0; i < names.size(); ++i) {
                const auto mode = static_cast<Fluid25DBankView>(i);
                const bool available =
                    mode != Fluid25DBankView::MarchingSquares || matching_mask_ready();
                ImGui::BeginDisabled(!available);
                if (ImGui::Selectable(names[i], !advanced && mode == selected))
                    set_bank_view(mode);
                ImGui::EndDisabled();
                if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Requires a validated completed-recording mask at the "
                                      "current saved time. No live reconstruction.");
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (selected == Fluid25DBankView::Reference)
            ImGui::TextWrapped("Original height geometry and native field sampling; cell-scale "
                               "angular banks remain.");
        else if (selected == Fluid25DBankView::Bspline2x)
            ImGui::TextWrapped("Rounds displayed terrain and water. Can widen/bead streams or "
                               "falsely connect thin gaps; no extra simulated water.");
        else
            ImGui::TextWrapped("Marching squares + bounded point smoothing changes coverage only. "
                               "Height geometry stays triangular. Baked %s; not live.",
                               coverage_->label().c_str());
        if (!matching_mask_ready())
            ImGui::TextWrapped("Marching squares: recorded masks only; unavailable here.");
        if (view_ == Fluid25DPresentationView::Diagnostics)
            ImGui::TextWrapped("RAW 2D FIELDS: bank reconstruction is bypassed; selected bank view "
                               "resumes in 3D.");
        else
            ImGui::TextWrapped("Bank views are display-only; native inputs stay unchanged.");
        ImGui::BeginDisabled(!markers_available_);
        ImGui::Checkbox("Dots/trails [W] - approximate velocity", &config_.motion_markers);
        ImGui::EndDisabled();
        if (!markers_available_)
            ImGui::TextWrapped(
                "Enable dots at launch; comparison mode prepares them automatically.");
        if (ImGui::CollapsingHeader("Advanced reconstruction / display diagnostics")) {
            int sampling = render_.native_bspline_surface  ? 2
                           : render_.native_bilinear_water ? 1
                                                           : 0;
            ImGui::BeginDisabled(comparison_.has_value());
            ImGui::SetNextItemWidth(-100.0F);
            if (ImGui::Combo("Surface", &sampling,
                             "Triangular reference\0Bilinear depth only\0Matched B-spline "
                             "terrain/water\0")) {
                render_.native_bilinear_water = sampling == 1;
                render_.native_bspline_surface = sampling == 2;
                render_.native_display_coverage = false;
                coverage_upload_pending_ = false;
                config_.native_water_sampling = sampling == 2   ? "bspline"
                                                : sampling == 1 ? "bilinear"
                                                                : "triangular";
            }
            if (render_.native_bspline_surface) {
                int level = render_.native_surface_subdivision == 4U   ? 2
                            : render_.native_surface_subdivision == 2U ? 1
                                                                       : 0;
                ImGui::TextWrapped("Display subdivision only; simulation resolution is unchanged.");
                ImGui::SetNextItemWidth(-100.0F);
                if (ImGui::Combo("Subdivisions", &level, "1x\0 2x\0 4x\0"))
                    config_.native_surface_subdivision = render_.native_surface_subdivision =
                        1U << level;
            }
            ImGui::EndDisabled();
            int diagnostic = static_cast<int>(render_.native_water_debug);
            ImGui::SetNextItemWidth(-100.0F);
            if (ImGui::Combo(
                    "Water diagnostic", &diagnostic,
                    "Shaded\0Solid coverage\0Unlit depth\0Normals\0Wireframe\0Native/display "
                    "wet mask\0No terrain occlusion (diagnostic)\0Fixed depth contours\0"))
                render_.native_water_debug = static_cast<std::uint32_t>(diagnostic);
        }
    }

    void draw_external_rain_ui(bool busy) {
        ImGui::SeparatorText("Rainfall - uniform across the map");
        ImGui::Text("Active supply: %.3f mm/h (%s)", external_snapshot_->rain_m_per_s * 3.6e6,
                    external_snapshot_->rain_m_per_s > 0.0 ? "ON" : "OFF");
        if (backend_metadata_.capabilities.solver.set_rain) {
            ImGui::BeginDisabled(busy);
            if (ImGui::BeginCombo("Rain preset", "Choose")) {
                for (const int rate : {0, 128, 256, 512, 1024}) {
                    const auto label =
                        rate == 0 ? std::string("Off") : std::to_string(rate) + " mm/h";
                    if (ImGui::Selectable(label.c_str()))
                        send_external_command(Fluid25DCommandKind::SetRain, double(rate) / 3.6e6);
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            // Typing is local, not a solver command. Keep edits available across
            // paused heartbeat phases; Apply alone requires healthy authority.
            // A matching applied acknowledgement refreshes the input. Do not
            // allow a newer draft to be overwritten while a command is in flight.
            ImGui::BeginDisabled(external_session_->command_pending() || managed_held_);
            ImGui::SetNextItemWidth(140.0F);
            if (ImGui::InputFloat("Intensity (mm/h)", &external_rain_input_mm_per_hour_, 0.1F, 1.0F,
                                  "%.3f"))
                external_rain_dirty_ = true;
            ImGui::EndDisabled();
            ImGui::BeginDisabled(busy || !external_rain_dirty_ ||
                                 !std::isfinite(external_rain_input_mm_per_hour_) ||
                                 external_rain_input_mm_per_hour_ < 0.0F ||
                                 external_rain_input_mm_per_hour_ > 36000.0F);
            if (ImGui::Button("Apply rain"))
                send_external_command(Fluid25DCommandKind::SetRain,
                                      static_cast<double>(external_rain_input_mm_per_hour_) /
                                          3.6e6);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(external_session_->command_pending()
                                       ? "Command pending acknowledgement"
                                   : external_rain_dirty_ ? "Typed value not applied"
                                                          : "Showing acknowledged supply");
        } else {
            ImGui::TextDisabled("This backend does not support changing rainfall.");
        }
        if (busy)
            ImGui::TextDisabled("Apply unavailable while busy, unhealthy or terminal; pending "
                                "commands also lock typing.");
        ImGui::TextWrapped(
            "Set zero to stop new rain; existing water still flows and drains. "
            "Intensity is depth added per hour, not retained water or playback speed.");
        ImGui::TextDisabled(
            "128-1024 mm/h presets are exaggerated demo rainfall, not typical weather.");
        ImGui::Separator();
    }

    void draw_terrain_selection_ui(ManagedSceneReload& demo) {
        auto* control = demo.control();
        if (!control)
            return;
        const auto& current = control->case_by_id(demo.current_case_id());
        ImGui::SeparatorText("Terrain and simulation grid");
        ImGui::Text("Current: %s | %u x %u | %.0f m cells", current.site_label.c_str(),
                    current.width, current.height, current.spacing_m);
        const auto& selected = control->case_by_id(demo.selected_case_id);
        ImGui::BeginDisabled(control->busy());
        if (ImGui::BeginCombo("Site", selected.site_label.c_str())) {
            for (const auto& entry : control->cases()) {
                if (entry.configuration != selected.configuration)
                    continue;
                ImGui::BeginDisabled(!entry.available);
                if (ImGui::Selectable(entry.site_label.c_str(), selected.site == entry.site))
                    demo.selected_case_id = entry.id;
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        const auto& edited = control->case_by_id(demo.selected_case_id);
        if (ImGui::BeginCombo("Grid / area", edited.label.c_str())) {
            for (const bool same_area : {true, false}) {
                ImGui::SeparatorText(same_area ? "Same area: 16.384 km square"
                                               : "Advanced: different coverage");
                for (const auto& entry : control->cases()) {
                    if (entry.site != edited.site || (entry.extent == "same") != same_area)
                        continue;
                    ImGui::BeginDisabled(!entry.available);
                    const std::string label =
                        entry.label + " | " + std::to_string(entry.width) + " | " +
                        std::to_string(static_cast<int>(entry.spacing_m)) + " m";
                    if (ImGui::Selectable(label.c_str(), entry.id == edited.id))
                        demo.selected_case_id = entry.id;
                    if (!entry.available &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", entry.error.c_str());
                    ImGui::EndDisabled();
                }
            }
            ImGui::EndCombo();
        }
        const auto& pending = control->case_by_id(demo.selected_case_id);
        ImGui::Text("Selected: %u x %u | %.0f m | %.3f km square", pending.width, pending.height,
                    pending.spacing_m, double(pending.width) * pending.spacing_m / 1000);
        ImGui::TextDisabled(
            "Source terrain: %.0f m. Finer sim grids interpolate, not add terrain detail.",
            pending.source_spacing_m);
        if (pending.width >= 2048)
            ImGui::TextWrapped("Fine/large grids run the solver substantially slower; 60x is a "
                               "request, not a guarantee.");
        ImGui::BeginDisabled(!pending.available || pending.id == current.id ||
                             !demo.restart_allowed());
        if (ImGui::Button("Apply & restart"))
            demo.request_case(pending.id);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard selection"))
            demo.selected_case_id = current.id;
        ImGui::EndDisabled();
        ImGui::TextWrapped(
            "Clears water/time; keeps rainfall, pace and appearance. Starts dry and paused.");
        if (control->busy())
            ImGui::TextWrapped("LOADING: %s. Previous scene held; no old/new field mixing.",
                               demo.stage().empty() ? control->status().phase.c_str()
                                                    : demo.stage().c_str());
        if (!control->status().error.empty()) {
            ImGui::TextWrapped("Load failed: %s", control->status().error.c_str());
            ImGui::BeginDisabled(!demo.restart_allowed());
            if (ImGui::Button("Retry selected"))
                demo.request_case(demo.selected_case_id);
            ImGui::SameLine();
            if (ImGui::Button("Restart previous case (dry)"))
                demo.request_case(current.id);
            ImGui::EndDisabled();
        }
    }

    void draw_external_ui(ManagedSceneReload* demo = nullptr) {
        ImGui::SetNextWindowSize(
            ImVec2(450.0F, std::min(790.0F, ImGui::GetIO().DisplaySize.y - 32.0F)),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Terrain Water")) {
            ImGui::End();
            return;
        }
        const auto lifecycle = external_snapshot_->header.lifecycle;
        const auto health = external_health();
        const bool terminal = lifecycle == Fluid25DLifecycle::Completed ||
                              lifecycle == Fluid25DLifecycle::Failed ||
                              lifecycle == Fluid25DLifecycle::Stopped;
        const bool busy = managed_held_ || external_session_->command_pending() ||
                          !external_error_.empty() || terminal ||
                          health != fluid_25d_project_config_detail::ExternalSessionHealth::Healthy;
        auto* control = demo ? demo->control() : nullptr;
        const bool loading = control && control->busy();
        const char* state = loading         ? "LOADING"
                            : managed_held_ ? "HELD - NOT LIVE"
                                            : lifecycle_name(lifecycle);
        ImGui::TextColored(ImVec4(.25F, .85F, 1.F, 1.F), "LIVE SynxFlow | %s", state);
        if (control) {
            const auto& current = control->case_by_id(demo->current_case_id());
            ImGui::Text("%s | %u x %u | %.0f m cells", current.site_label.c_str(), current.width,
                        current.height, current.spacing_m);
            ImGui::Text("Simulation patch %.3f km square",
                        current.width * current.spacing_m / 1000);
            ImGui::Text("Simulated: %s / %s", fluid_25d_demo_time_label(shown_->time_s).c_str(),
                        fluid_25d_demo_time_label(current.duration_s).c_str());
        } else {
            ImGui::Text("%u x %u | %.0f m cells | simulated %s", simulation_.grid_width,
                        simulation_.grid_height, simulation_.cell_size_m,
                        fluid_25d_demo_time_label(shown_->time_s).c_str());
        }
        ImGui::BeginDisabled(busy);
        if ((lifecycle == Fluid25DLifecycle::Ready || lifecycle == Fluid25DLifecycle::Paused) &&
            backend_metadata_.capabilities.solver.resume &&
            ImGui::Button(shown_->time_s == 0 ? "Start [Space]" : "Resume [Space]"))
            send_external_command(Fluid25DCommandKind::Resume);
        if (lifecycle == Fluid25DLifecycle::Running &&
            backend_metadata_.capabilities.solver.pause && ImGui::Button("Pause [Space]"))
            send_external_command(Fluid25DCommandKind::Pause);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(control ? !demo->restart_allowed()
                                     : busy || !backend_metadata_.capabilities.solver.reset);
        if (ImGui::Button("Reset water [R]")) {
            if (control)
                demo->request_case(demo->current_case_id());
            else if (backend_metadata_.capabilities.solver.reset)
                send_external_command(Fluid25DCommandKind::Reset);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Fit simulation patch")) {
            select_camera("overview");
        }
        if (loading)
            ImGui::TextWrapped("Loading: %s. Previous scene and motion held.",
                               demo->stage().empty() ? control->status().phase.c_str()
                                                     : demo->stage().c_str());
        else if (managed_held_)
            ImGui::TextWrapped("Last scene held. No running producer is attached; retry or restart "
                               "in Scene setup.");
        else if (lifecycle == Fluid25DLifecycle::Completed)
            ImGui::TextWrapped("Simulation horizon reached. The final water state is held; Reset "
                               "water starts a new run.");
        else if ((lifecycle == Fluid25DLifecycle::Paused ||
                  lifecycle == Fluid25DLifecycle::Ready) &&
                 shown_->time_s == 0)
            ImGui::TextWrapped("Dry and paused. Press Start to supply rain across the terrain.");
        if (external_session_->command_pending())
            ImGui::TextDisabled("Applying solver command...");
        if (!external_error_.empty())
            ImGui::TextWrapped("Service/viewer error: %s", external_error_.c_str());
        if (control && !control->status().error.empty())
            ImGui::TextWrapped("Selection/restart error: %s", control->status().error.c_str());
        if (!loading && !managed_held_ &&
            (health == fluid_25d_project_config_detail::ExternalSessionHealth::Delayed ||
             health == fluid_25d_project_config_detail::ExternalSessionHealth::Disconnected))
            ImGui::TextWrapped(
                "Published field frozen; producer heartbeat is delayed/disconnected.");
        if (shown_->stats.water_volume_m3 >= 1.0e6)
            ImGui::Text("Water %.2f million m3 | max depth %.2f m",
                        shown_->stats.water_volume_m3 / 1.0e6, shown_->stats.max_depth_m);
        else
            ImGui::Text("Water %.0f m3 | max depth %.2f m", shown_->stats.water_volume_m3,
                        shown_->stats.max_depth_m);

        if (control && fluid_25d_rain_guide_site(control->case_by_id(demo->current_case_id()))) {
            ImGui::SeparatorText("Rain-response demonstration (optional)");
            if (demo->guide_active()) {
                ImGui::TextWrapped(
                    "%s",
                    lifecycle == Fluid25DLifecycle::Running
                        ? fluid_25d_rain_guide_caption(shown_->time_s,
                                                       external_snapshot_->rain_m_per_s > 0)
                        : "Preparing acknowledged rain/pace settings, then starting the solver.");
                if (ImGui::Button("Leave guided demonstration"))
                    demo->cancel_guide(
                        "Guide stopped. Rain/pace stay as acknowledged; solver continues.");
            } else {
                ImGui::BeginDisabled(!demo->guide_available());
                if (ImGui::Button("Run rain-response demonstration"))
                    demo->start_guide();
                ImGui::EndDisabled();
                if (!demo->guide_note().empty())
                    ImGui::TextWrapped("%s", demo->guide_note().c_str());
                else
                    ImGui::TextWrapped(
                        "Starts from dry/paused: 512 mm/h at 60x, rain off after 2 "
                        "simulated hours, drainage through 6 hours. About 6 wall minutes.");
                if (!demo->guide_available() && !demo->guide_finished())
                    ImGui::TextDisabled(
                        "Reset water to dry/paused first; wait for a healthy worker.");
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped(
                "Physical-time milestones; manual solver/camera input exits the guide.");
            ImGui::TextWrapped(
                "End of guide does not pause the simulation. Rain/foam are visual cues.");
            ImGui::PopStyleColor();
        }

        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
            draw_external_rain_ui(busy);
            ImGui::Text("Requested pace: %.1fx", external_snapshot_->pacing);
            if (const auto achieved = pace_meter_.achieved())
                ImGui::Text("Achieved: %.1fx (recent simulated seconds / wall seconds)", *achieved);
            else
                ImGui::TextDisabled("Achieved pace: paused / collecting a running window");
            if (backend_metadata_.capabilities.solver.set_time_scale) {
                ImGui::BeginDisabled(busy);
                if (ImGui::BeginCombo("Pace preset", "Choose")) {
                    for (const int pace : {1, 10, 30, 60, 120}) {
                        const auto label = std::to_string(pace) + "x";
                        if (ImGui::Selectable(label.c_str()))
                            send_external_command(Fluid25DCommandKind::SetTimeScale, pace);
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputFloat("Custom pace (x)", &external_time_scale_input_, .125F, 1.F,
                                      "%.2f"))
                    external_time_scale_dirty_ = true;
                ImGui::BeginDisabled(
                    !external_time_scale_dirty_ || !std::isfinite(external_time_scale_input_) ||
                    external_time_scale_input_ < .125F || external_time_scale_input_ > 300);
                if (ImGui::Button("Apply pace"))
                    send_external_command(Fluid25DCommandKind::SetTimeScale,
                                          external_time_scale_input_);
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                if (external_time_scale_dirty_)
                    ImGui::TextDisabled("Custom pace edit is not applied yet.");
            }
            ImGui::TextWrapped("Pace accelerates simulation time, not rain intensity or FPS. Fine "
                               "grids may not keep up.");
            if ((external_rain_dirty_ || external_time_scale_dirty_) &&
                !external_session_->command_pending() && ImGui::Button("Discard control edits")) {
                external_rain_input_mm_per_hour_ = float(external_snapshot_->rain_m_per_s * 3.6e6);
                external_time_scale_input_ = float(external_snapshot_->pacing);
                external_rain_dirty_ = external_time_scale_dirty_ = false;
            }
        }

        if (control && ImGui::CollapsingHeader("Scene setup")) {
            draw_terrain_selection_ui(*demo);
            ImGui::TextDisabled("Closing this managed demo stops only its owned worker.");
        }
        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            int reading = static_cast<int>(catchment_view_);
            if (ImGui::Combo("3D view", &reading,
                             "Scenic composite\0Water isolation\0Flow inspection\0")) {
                catchment_view_ = static_cast<Fluid25DCatchmentView>(reading);
                view_ = Fluid25DPresentationView::Catchment;
                quiver_reset_ = true;
            }
            bool diagnostic = view_ == Fluid25DPresentationView::Diagnostics;
            if (ImGui::Checkbox("2D diagnostic map [A]", &diagnostic))
                view_ = diagnostic ? Fluid25DPresentationView::Diagnostics
                                   : Fluid25DPresentationView::Catchment;
            if (diagnostic) {
                int map = static_cast<int>(debug_view_);
                if (ImGui::Combo("Map [D]", &map,
                                 "Terrain\0Depth\0Surface\0Speed\0Direction\0Wet/dry\0"))
                    debug_view_ = static_cast<Fluid25DDebugView>(map);
                draw_native_color_legend();
            }
            // Existing runoff/collection observation points were authored for
            // the mountain crop. Do not pretend they identify another site's basin.
            if (!control || control->case_by_id(demo->current_case_id()).site == "mountain") {
                for (const char* camera : {"overview", "runoff", "collection"}) {
                    const char* label = camera == std::string_view("overview") ? "Whole patch"
                                        : camera == std::string_view("runoff") ? "Downhill runoff"
                                                                               : "Basin collection";
                    if (ImGui::RadioButton(label, config_.recording_camera == camera)) {
                        select_camera(camera);
                    }
                }
            }
            ImGui::BeginDisabled(!markers_available_);
            if (ImGui::Checkbox("Dots / trails [W]", &config_.motion_markers))
                marker_reset_ = true;
            ImGui::EndDisabled();
            ImGui::TextDisabled(
                "Dots/trails and foam illustrate motion; they are not native particles.");
            if (ImGui::CollapsingHeader("Appearance - render only"))
                draw_native_presentation_ui();
        }
        if (ImGui::CollapsingHeader("How to read this")) {
            ImGui::TextWrapped(
                "Only the full-resolution simulation patch is rendered. Rain supplies that "
                "rectangle. Water follows "
                "slopes, collects in depressions and "
                "can leave through the open perimeter. There is no authored source/drain here.");
            ImGui::TextWrapped(
                "Rain off stops new supply; existing water still flows. Small streams can appear "
                "before large pools. Visible rain density/fall speed only affect rendering.");
            ImGui::TextWrapped("Live runs the solver. Replay elsewhere only displays saved fields. "
                               "A healthy paused heartbeat does not mean water is advancing.");
        }
        if (ImGui::CollapsingHeader("Diagnostics / advanced")) {
            ImGui::Text("Backend %s | %s", backend_metadata_.id.c_str(),
                        backend_profile_name(backend_metadata_.profile));
            ImGui::Text("Heartbeat %s | field age %.3f s", external_health_name(health),
                        external_publication_age_s());
            ImGui::Text(
                "Generation %llu | sequence %llu",
                static_cast<unsigned long long>(external_snapshot_->header.reset_generation),
                static_cast<unsigned long long>(external_snapshot_->header.sequence));
            ImGui::Text("Session %.16s", backend_metadata_.session.session_id.c_str());
            ImGui::Text("Input %.12s | bed %.12s", backend_metadata_.grid.input_sha256.c_str(),
                        backend_metadata_.grid.solver_bed_sha256.c_str());
            ImGui::Text("Max speed %.3f m/s", shown_->stats.max_speed_m_per_s);
            if (!external_session_->last_command_message().empty())
                ImGui::TextWrapped("Last acknowledgement: %s",
                                   external_session_->last_command_message().c_str());
            if (external_snapshot_->acknowledgement)
                ImGui::Text("Command %llu: %s",
                            static_cast<unsigned long long>(
                                external_snapshot_->acknowledgement->command_id),
                            acknowledgement_name(external_snapshot_->acknowledgement->state));
            ImGui::BeginDisabled(busy);
            if (backend_metadata_.capabilities.solver.step && ImGui::Button("Step solver"))
                send_external_command(Fluid25DCommandKind::Step);
            if (backend_metadata_.capabilities.solver.stop && ImGui::Button("Stop producer"))
                send_external_command(Fluid25DCommandKind::Stop);
            ImGui::EndDisabled();
            ImGui::TextDisabled(
                "No native ledger/tracer claim. Viewer performs no hydraulic solve.");
            ImGui::Text("Renderer cap %u Hz (0 = unpaced)", config_.common.render_fps);
            host::draw_performance_ui(
                {.frame_stats = frame_stats_snapshot_,
                 .process = process_stats_,
                 .device_memory_budget = device_memory_budget_,
                 .gpu_timings = resources_.latest_timings(),
                 .config = {.label = "Renderer performance", .default_open = false}});
            if (!control)
                ImGui::TextWrapped("Standalone viewer: Esc closes/detaches; the launcher owns the "
                                   "producer. Closing alone does not stop it.");
        }
        ImGui::End();
    }

    void draw_ui(ManagedSceneReload* demo = nullptr) {
        if (external_session_) {
            draw_external_ui(demo);
            return;
        }
        ImGui::SetNextWindowSize(
            ImVec2(510.0F, std::min(850.0F, ImGui::GetIO().DisplaySize.y - 32.0F)),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(comparison_           ? "Mountain Rain - BANKS recorded comparison"
                         : config_.stream_path ? "Live external mountain runoff"
                                               : "Mountain Rain - REPLAY recorded fields")) {
            const Fluid25DRecordedRainStatus rain = recording_->rain_status_at(shown_->time_s);
            ImGui::SeparatorText("Rainfall - recorded forcing (read only)");
            ImGui::Text("Saved rain: %s | %.2f mm/h",
                        fluid_25d_recorded_rain_phase_name(rain.phase), rain.rate_mm_per_hour);
            ImGui::Text("Weather at playhead %.2f mm/h (water held at %.0f s)",
                        applied_rain_mm_per_hour().value_or(0.0), shown_->time_s);
            ImGui::Text("Scheduled total %.3f mm (not retained water)",
                        rain.cumulative_scheduled_rain_depth_mm);
            ImGui::TextWrapped(
                "Fixed recorded history. Use LIVE solver mode to change intensity. "
                "Rain off stops supply, not runoff; viewing speed does not change rain.");
            ImGui::Separator();
            if (comparison_) {
                ImGui::TextColored(ImVec4(.25F, .85F, 1.F, 1.F),
                                   "RECORDED BANK COMPARISON - prebaked, not live");
                ImGui::Text("Window %.0f..%.3f s | %zu saved masks", comparison_->first_s(),
                            comparison_->end_s(), comparison_->frame_count());
                if (coverage_cache_)
                    ImGui::Text("Prepared %zu masks | %.1f MiB | %.1f ms once",
                                coverage_cache_->frame_count(),
                                static_cast<double>(coverage_cache_->bytes()) / (1024. * 1024.),
                                coverage_cache_->preparation_ms());
                else if (comparison_error_.empty())
                    ImGui::TextWrapped("Preparing coverage %zu/%zu; reference shown, playback held",
                                       cache_progress_->load(), comparison_->frame_count());
                else
                    ImGui::TextWrapped("PREPARATION FAILED: %s", comparison_error_.c_str());
                ImGui::Checkbox("Loop recorded window (not simulation)", &comparison_loop_);
                if (comparison_at_end_)
                    ImGui::TextWrapped(
                        "END OF COMPARISON WINDOW: last saved state held. Replay/Restart is "
                        "explicit; the solver has not been stopped.");
                draw_native_presentation_ui();
                draw_native_color_legend();
                ImGui::Separator();
            }
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
            } else if (!comparison_) {
                ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                                   "REPLAY - recorded SynxFlow; no solver running");
                draw_native_color_legend();
                ImGui::TextWrapped("Space pauses viewing, not computation.");
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
            if (shown_index_ != clock_.frame_index())
                ImGui::Text("Loading requested state %.0f s...", clock_.time_s());
            else
                ImGui::TextWrapped("%s | playhead %.1f s | %.1fx viewing speed",
                                   comparison_at_end_ ? "END OF COMPARISON WINDOW"
                                   : clock_.ended()
                                       ? (config_.stream_path
                                              ? (recording_->producer_state() == "running"
                                                     ? "WAITING FOR NEXT SNAPSHOT"
                                                     : "END OF FINITE SOURCE")
                                              : "END OF RECORDING")
                                   : clock_.paused() ? "PAUSED"
                                                     : "PLAYING",
                                   clock_.time_s(), clock_.rate());
            ImGui::BeginDisabled((!comparison_ && clock_.ended()) ||
                                 config_.stream_path.has_value() ||
                                 (comparison_ && !coverage_cache_));
            if (ImGui::Button(comparison_at_end_ ? "Replay window [Space]"
                              : clock_.paused()  ? "Play viewing [Space]"
                                                 : "Pause viewing [Space]"))
                toggle_playback();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Restart [R]")) {
                restart_playback();
            }
            ImGui::SameLine();
            if (ImGui::Button("<")) {
                clock_.previous();
                constrain_comparison_clock(false);
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            ImGui::SameLine();
            if (ImGui::Button(">")) {
                clock_.next();
                constrain_comparison_clock(false);
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
                if (playback_session_->metadata().session.lifecycle != Fluid25DLifecycle::Completed)
                    playback_navigation_kind_ = Fluid25DCommandKind::Step;
            }
            float time = static_cast<float>(clock_.time_s() / 60.0);
            if (ImGui::SliderFloat(
                    "Seek (minutes)", &time,
                    static_cast<float>(
                        (comparison_ ? comparison_->first_s() : recording_->times_s().front()) /
                        60.0),
                    static_cast<float>(
                        (comparison_ ? comparison_->end_s() : recording_->times_s().back()) / 60.0),
                    "%.1f")) {
                seek_playback(static_cast<double>(time) * 60.0);
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
                    seek_playback(seek_time_s);
                }
            };
            if (comparison_) {
                seek_to_recorded_time("Window start", comparison_->first_s());
                ImGui::SameLine();
                seek_to_recorded_time("Window end", comparison_->end_s());
            } else {
                if (taper_start_s >= 0.0) {
                    seek_to_recorded_time("Rain taper start", taper_start_s);
                    ImGui::SameLine();
                }
                if (rain_zero_s >= 0.0) {
                    seek_to_recorded_time("Rain zero", rain_zero_s);
                    ImGui::SameLine();
                    seek_to_recorded_time(
                        "Zero +30m", std::min(rain_zero_s + 1800.0, recording_->times_s().back()));
                    ImGui::SameLine();
                    seek_to_recorded_time(
                        "Zero +1h", std::min(rain_zero_s + 3600.0, recording_->times_s().back()));
                    ImGui::SameLine();
                }
                seek_to_recorded_time("End", recording_->times_s().back());
            }
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
                if (ImGui::RadioButton(name == std::string_view("overview") ? "Full patch" : name,
                                       config_.recording_camera == name)) {
                    select_camera(name);
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
            if (scenic_material_active())
                ImGui::TextWrapped("Scenic color is not a depth palette; use raw maps to measure.");
            else
                ImGui::Text("Depth color: log scale 0.01 / 0.1 / 1 / 10 m");
            if (!comparison_)
                draw_native_presentation_ui();
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
        ManagedSceneReload reload(*this);
        host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [&reload](host::WindowedAppContext& context) {
            reload.scene().create_resources(context.device(), context.gpu(),
                                            context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [&reload](host::WindowedAppContext& context) {
            auto& app = reload.scene();
            app.create_render_resources(
                context.device(), render::ColorTargetView{.extent = context.swapchain().extent(),
                                                          .format = context.swapchain().format()});
            app.graph_.clear();
            app.graph_.resize(context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [&reload](host::WindowedAppContext&) {
            auto& app = reload.scene();
            app.graph_.clear();
            app.scenic_.destroy_swapchain_resources();
            app.markers_.destroy_render_pipeline();
            app.resources_.destroy_swapchain_resources();
        };
        callbacks.draw_ui = [&reload](host::WindowedAppContext&) {
            reload.scene().draw_ui(&reload);
        };
        callbacks.update = [&reload](host::WindowedAppContext& context, const FrameTiming& timing) {
            reload.poll(
                context.device(), context.gpu(), context.frame_slot_count(),
                {.extent = context.swapchain().extent(), .format = context.swapchain().format()},
                [&] {
                    for (std::uint32_t slot = 0; slot < context.frame_slot_count(); ++slot) {
                        context.frame_resources().wait_for_frame(slot);
                        context.gpu().mark_submission_completed(
                            context.frame_resources().submitted_ticket(slot));
                    }
                });
            reload.scene().update_windowed(context, timing, &reload);
        };
        callbacks.record_frame = [&reload](host::WindowedAppContext& context,
                                           const host::WindowedRenderFrame& frame) {
            const vulkan::CommandRecorder recorder(frame.command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            reload.scene().record(context.device(), frame.command_buffer, frame.color_target,
                                  frame.frame_slot, Fluid25DRenderTargetMode::Present,
                                  context.profile_recorder(), frame.timing.frame_index);
            recorder.end("terrain water window");
        };
        callbacks.shutdown = [&reload](host::WindowedAppContext& context) {
            reload.shutdown(context.gpu());
        };
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

    void update_windowed(host::WindowedAppContext& context, const FrameTiming& timing,
                         ManagedSceneReload* reload) {
        if (auto sample =
                frame_stats_.record_frame({.delta_seconds = timing.delta_seconds,
                                           .width = context.swapchain().extent().width,
                                           .height = context.swapchain().extent().height})) {
            frame_stats_snapshot_ = *sample;
            process_stats_ = process_sampler_.sample();
            device_memory_budget_ = context.device().device_memory_budget();
        }
        const auto input = context.filtered_input();
        if (reload->guide_active() &&
            (input.scroll_delta().y != 0 ||
             (input.mouse_button_down(input::MouseButton::Left) &&
              (input.cursor_delta().x != 0 || input.cursor_delta().y != 0))))
            reload->cancel_guide("Manual orbit/zoom; guide stopped. Solver continues.");
        orbit_.update_pointer_input(input, timing.delta_seconds);
        if (input.key_pressed(input::Key::S))
            cycle_bank_view();
        if (input.key_pressed(input::Key::W) && markers_available_)
            config_.motion_markers = !config_.motion_markers;
        if (input.key_pressed(input::Key::V))
            select_native_style(render_.native_presentation == 3U ? 2U : 3U);
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
            if (reload->control() && reload->restart_allowed() && input.key_pressed(input::Key::R))
                reload->request_case(reload->current_case_id());
            else if (controls_available && input.key_pressed(input::Key::R) &&
                     fluid_25d_command_supported(backend_metadata_.capabilities,
                                                 Fluid25DControlDomain::Solver,
                                                 Fluid25DCommandKind::Reset))
                send_external_command(Fluid25DCommandKind::Reset);
        } else {
            if (input.key_pressed(input::Key::Space)) {
                if (config_.stream_path)
                    live_playing_ = !live_playing_;
                else
                    toggle_playback();
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
            debug_view_ = static_cast<Fluid25DDebugView>((static_cast<int>(debug_view_) + 1) % 6);
        update_view(timing.delta_seconds);
    }

    int run_headless() {
        ManagedSceneReload reload(*this);
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&reload](host::HeadlessPngContext& context) {
            reload.scene().create_resources(
                context.device(), context.gpu(),
                host::headless_capture_frame_slot_count(context.config()));
            reload.scene().create_render_resources(context.device(), context.render_target());
        };
        callbacks.before_frame = [&reload](host::HeadlessPngContext& context,
                                           const host::HeadlessCaptureFrame& frame) {
            reload.poll(context.device(), context.gpu(),
                        host::headless_capture_frame_slot_count(context.config()),
                        context.render_target(),
                        [&] { context.gpu().wait_queue_idle("terrain scene replacement"); });
            if (context.config().headless_render_only) {
                auto span =
                    context.profile_recorder()
                        ? context.profile_recorder()->cpu_span(frame.index, "fluid25d.update_view")
                        : profiling::ScopedCpuProfileSpan{};
                reload.scene().update_view(frame.timing.delta_seconds);
            }
        };
        callbacks.record_frame =
            [&reload](host::HeadlessPngContext& context, const host::HeadlessCaptureFrame& frame,
                      VkCommandBuffer commands, const host::HeadlessRenderTarget& target) {
                reload.scene().record_headless_frame(context, frame, commands, target);
            };
        callbacks.shutdown = [&reload](host::HeadlessPngContext& context) {
            reload.shutdown(context.gpu());
        };
        host::HeadlessPngHost host(
            {.run_config = config_.common,
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
            std::move(callbacks));
        return host.run();
    }

    void record_headless_frame(host::HeadlessPngContext& context,
                               const host::HeadlessCaptureFrame& frame, VkCommandBuffer commands,
                               const host::HeadlessRenderTarget& target) {
        if (!config_.common.headless_render_only && weather_ && frame.index > 0U)
            weather_->advance(1.0 / double(config_.common.fps));
        if (config_.common.headless_render_only) {
            record(context.device(), commands, target, frame.frame_slot,
                   Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(),
                   frame.index);
            return;
        }
        if (external_session_) {
            visual_delta_ = 0.0;
            rain_clock_.capture(frame.index, config_.common.fps,
                                config_.common.capture_mode == CaptureMode::Video &&
                                    external_snapshot_->header.lifecycle ==
                                        Fluid25DLifecycle::Running,
                                config_.native_rain_speed);
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
            log_rain_capture(frame.index);
            return;
        }
        // Load by immutable capture index on the GPU-owner callback. The
        // video producer may already be preparing later output frames.
        const bool video = config_.common.capture_mode == CaptureMode::Video;
        const double unbounded =
            static_cast<double>(config_.recording_time_seconds) +
            (video ? static_cast<double>(frame.index) * config_.recording_frame_interval_seconds
                   : 0.0);
        const double time =
            comparison_ ? unbounded : std::min(recording_->times_s().back(), unbounded);
        const auto bounded =
            comparison_ ? comparison_->map(time, comparison_loop_) : Fluid25DBankWindowTime{time};
        const double presented = bounded.time_s;
        const double previous = shown_->time_s;
        double previous_requested = frame.index == 0U
                                        ? time
                                        : static_cast<double>(config_.recording_time_seconds) +
                                              static_cast<double>(frame.index - 1U) *
                                                  config_.recording_frame_interval_seconds;
        if (comparison_)
            previous_requested = comparison_->map(previous_requested, comparison_loop_).time_s;
        else
            previous_requested = std::min(recording_->times_s().back(), previous_requested);
        if (frame.index > 0U && presented < previous_requested)
            seek_playback(presented);
        else
            clock_.seek(presented);
        comparison_at_end_ = bounded.at_end;
        const auto index = clock_.frame_index();
        if (index != shown_index_) {
            accept_frame(recording_->frame(index));
            shown_index_ = index;
        }
        if (config_.bank_comparison_cycle_frames != 0U) {
            const auto mode = static_cast<Fluid25DBankView>(
                (frame.index / config_.bank_comparison_cycle_frames) % 3U);
            if (mode != bank_view_)
                set_bank_view(mode);
        }
        // A sparse recording can jump far in physical time. Bound only
        // the optional approximate visual history, never the saved fields.
        visual_delta_ = !video || presented < previous_requested ? 0.0
                        : render_.native_presentation != 0U
                            ? fluid_25d_recorded_visual_delta(previous_requested, presented,
                                                              recording_->times_s().back())
                            : std::clamp(shown_->time_s - previous, 0.0, 60.0);
        const double captured_visual_delta = visual_delta_;
        if (frame.index == 0U || presented > previous_requested)
            rain_clock_.capture(frame.index, config_.common.fps, video, config_.native_rain_speed);
        record(context.device(), commands, target, frame.frame_slot,
               Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(), frame.index);
        std::printf("fluid_25d_recording_capture: output_frame=%u requested_s=%.9f "
                    "saved_s=%.9f camera=%s hydraulic_dispatches=0 visual_delta_s=%.9f "
                    "presentation=%s\n",
                    frame.index, presented, shown_->time_s, config_.recording_camera.c_str(),
                    captured_visual_delta, config_.native_presentation.c_str());
        log_rain_capture(frame.index);
        if (comparison_)
            std::printf(
                "fluid_25d_bank_capture: output_frame=%u mode=%s requested_unbounded_s=%.9f "
                "presented_s=%.9f saved_s=%.9f end=%u looped=%u cache_hit=%u\n",
                frame.index, fluid_25d_bank_view_name(bank_view_), time, presented, shown_->time_s,
                bounded.at_end ? 1U : 0U, bounded.looped ? 1U : 0U, coverage_cache_ ? 1U : 0U);
    }

    Fluid25DProjectConfig config_;
    ManagedSceneReload* managed_demo_ = nullptr; // Owned by the host, survives scene replacement.
    std::shared_ptr<Fluid25DRecording> recording_;
    std::unique_ptr<Fluid25DExternalSession> external_session_;
    std::atomic<double> external_read_ms_{0.0};
    std::atomic<std::uint64_t> external_read_jobs_{0U};
    std::optional<std::chrono::steady_clock::time_point> external_reset_started_{};
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
    bool external_refresh_after_render_setup_ = false;
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
    bool managed_held_ = false;
    Fluid25DDemoPaceMeter pace_meter_;
    host::FrameStats frame_stats_;
    std::optional<host::FrameStatsSnapshot> frame_stats_snapshot_;
    host::ProcessResourceStatsSampler process_sampler_;
    host::ProcessResourceStats process_stats_;
    std::optional<vulkan::DeviceMemoryBudgetInfo> device_memory_budget_;
    bool external_rain_dirty_ = false;
    bool external_time_scale_dirty_ = false;
    bool live_playing_ = false, follow_latest_ = false;
    std::exception_ptr shutdown_failure_{};
    std::size_t shown_index_ = 0U, pending_index_ = 0U;
    std::uint64_t pending_viewer_generation_ = 0U;
    std::vector<Fluid25DVelocityGpu> packed_velocity_;
    std::shared_ptr<const Fluid25DDisplayCoverage> coverage_;
    std::vector<float> coverage_values_;
    std::optional<Fluid25DBankComparisonWindow> comparison_;
    std::optional<Fluid25DDisplayCoverageCache> coverage_cache_;
    std::shared_ptr<std::atomic_size_t> cache_progress_;
    std::future<Fluid25DDisplayCoverageCache> cache_pending_;
    std::string comparison_error_;
    Fluid25DBankView bank_view_ = Fluid25DBankView::Reference;
    bool comparison_loop_ = false, comparison_at_end_ = false, comparison_end_reported_ = false;
    bool coverage_upload_pending_ = false, markers_available_ = false;
    double coverage_resident_time_ = -1.0;
    Fluid25DConfig simulation_;
    Fluid25DScenarioData scenario_;
    Fluid25DCatchmentRenderOptions render_;
    ProjectRuntimeAdapter runtime_{1};
    Fluid25DGpuResources resources_;
    Fluid25DMotionMarkers markers_;
    render::RenderGraphFrameExecutor graph_;
    Fluid25DScenic scenic_;
    std::optional<Fluid25DTerrainSurface> terrain_surface_;
    Fluid25DScenicMaterial scenic_material_;
    AtmosphereEnvironmentRuntime daylight_runtime_;
    std::unique_ptr<TerrainSurfaceEnvironment> weather_;
    bool defer_weather_setup_ = false;
    std::optional<render::AtmosphereBackgroundAtlasResources> daylight_atlases_;
    bool scenic_material_edited_ = false;
    vulkan::GpuRuntime* scenic_gpu_ = nullptr;
    std::uint32_t scenic_slots_ = 0U;
    double scenic_clock_s_ = 0.0;
    Fluid25DRainVisualClock rain_clock_{};
    bool scenic_flow_reset_ = true;
    bool whitewater_dirty_ = true;
    Fluid25DWhitewaterSelection whitewater_selection_;
    double whitewater_source_s_ = 0, whitewater_build_cpu_ms_ = 0;
    double whitewater_source_published_unix_s_ = 0, external_last_advance_unix_s_ = 0;
    std::uint64_t whitewater_generation_ = 0U;
    std::uint64_t whitewater_epoch_ = 0U;
    // Destroyed/waited before the earlier-declared immutable scenario bed.
    std::future<WhitewaterBuild> whitewater_pending_;
    std::optional<vulkan::GpuTimestampProfiler> native_profiler_;
    std::optional<vulkan::GpuTimestampProfiler> coverage_profiler_;
    std::vector<std::uint64_t> native_profile_frames_;
    std::vector<bool> native_profile_pending_;
    std::vector<bool> native_profile_scenic_;
    profiling::ProfileRecorder* native_profile_recorder_ = nullptr;
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

ManagedSceneReload::ManagedSceneReload(RecordingApp& initial) : active_(&initial) {
    initial.managed_demo_ = this;
    if (!initial.config_.terrain_demo_control_path)
        return;
    control_ =
        std::make_unique<Fluid25DTerrainDemoControl>(*initial.config_.terrain_demo_control_path);
    current_case_id_ = selected_case_id = control_->initial_case_id();
    const auto& entry = control_->case_by_id(current_case_id_);
    const auto& metadata = initial.backend_metadata_;
    if (metadata.session.session_id != control_->status().session_id ||
        metadata.grid.width != entry.width || metadata.grid.height != entry.height ||
        metadata.grid.spacing_m != entry.spacing_m ||
        metadata.grid.input_sha256 != entry.input_sha256 ||
        metadata.grid.solver_bed_sha256 != entry.solver_bed_sha256 ||
        !initial.config_.native_scenic_surface_grid ||
        *initial.config_.native_scenic_surface_grid != entry.surface_grid)
        throw std::runtime_error(
            "managed initial scene does not match the prepared catalogue/session");
    if (initial.config_.terrain_demo_script_path)
        script_ = fluid_25d_load_terrain_demo_script(*initial.config_.terrain_demo_script_path);
    log("initial");
}

ManagedSceneReload::~ManagedSceneReload() = default;

void ManagedSceneReload::log(const std::string& event) {
    if (control_)
        control_->event(event, current_case_id_, active_->backend_metadata_.session.session_id,
                        active_->shown_->time_s, active_->external_snapshot_->rain_m_per_s * 3.6e6,
                        active_->simulation_.grid_width, active_->simulation_.grid_height,
                        active_->backend_metadata_.grid.solver_bed_sha256);
}

bool ManagedSceneReload::restart_allowed() const {
    return control_ && fluid_25d_demo_restart_allowed(control_->busy(),
                                                      active_->external_session_->command_pending(),
                                                      control_->status().worker_available);
}

void ManagedSceneReload::request_case(const std::string& id) {
    if (!restart_allowed())
        return;
    cancel_guide("Scene reset/replacement; guide stopped.");
    guide_actions_.clear();
    guide_finished_ = false;
    guide_note_.clear();
    control_->request(id, active_->external_snapshot_->rain_m_per_s * 3.6e6,
                      active_->external_snapshot_->pacing);
    selected_case_id = id;
    active_->managed_held_ = true;
    active_->visual_delta_ = 0;
    stage_ = "Validating selected case";
    log("switch-request");
}

void ManagedSceneReload::run_script() {
    if (script_.empty() || !control_)
        return;
    if (control_->status().phase == "error")
        throw std::runtime_error("managed regression switch failed: " + control_->status().error);
    if (control_->busy() || active_->managed_held_ || active_->external_session_->command_pending())
        return;
    if (!active_->external_error_.empty())
        throw std::runtime_error("managed regression service failed: " + active_->external_error_);
    run_sequence(script_, action_index_, action_waiting_, action_started_, action_ack_id_, false);
}

void ManagedSceneReload::run_sequence(const std::vector<Fluid25DTerrainDemoAction>& actions,
                                      std::size_t& index, bool& waiting,
                                      std::chrono::steady_clock::time_point& started,
                                      std::optional<std::uint64_t>& expected_ack, bool guided) {
    if (control_->busy() || active_->managed_held_ ||
        active_->external_session_->command_pending() || !active_->external_error_.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (started == std::chrono::steady_clock::time_point{}) {
        started = now; // Cold setup time is not part of an action's delay.
        return;
    }
    if (active_->external_refresh_after_render_setup_)
        return; // Let update_view validate a post-setup publication first.
    if (waiting) {
        if (expected_ack) {
            const auto& ack = active_->external_snapshot_->acknowledgement;
            if (!ack || ack->command_id != *expected_ack ||
                ack->state != Fluid25DAcknowledgementState::Applied)
                throw std::runtime_error(
                    "scheduled solver command did not receive its applied acknowledgement");
            expected_ack.reset();
        }
        log(guided ? "guide-applied" : "action-applied");
        waiting = false;
        started = now;
    }
    if (index >= actions.size())
        return;
    const auto& action = actions[index];
    if (!fluid_25d_demo_action_due(action, std::chrono::duration<double>(now - started).count(),
                                   active_->shown_->time_s))
        return;
    if (action.kind == "guide-start") {
        if (!guide_available())
            throw std::runtime_error(
                "rain guide requires the healthy dry/paused Mountain 1k/16m case");
        start_guide();
    } else if (action.kind == "guide-cancel") {
        cancel_guide("Guide explicitly stopped. Solver settings are retained.");
    } else if (action.kind == "camera") {
        if (control_->case_by_id(current_case_id_).site != "mountain")
            throw std::runtime_error("observation cameras require the mountain site");
        active_->select_camera(action.camera.c_str(), guided);
    } else if (action.kind == "switch") {
        cancel_guide("Scene replacement; guide stopped.");
        control_->request(action.case_id, action.rain_mm_per_hour, action.pacing);
        selected_case_id = action.case_id;
        active_->managed_held_ = true;
        stage_ = "Validating selected case";
    } else if (action.kind == "reset") {
        request_case(current_case_id_);
    } else {
        const auto kind = action.kind == "resume"     ? Fluid25DCommandKind::Resume
                          : action.kind == "pause"    ? Fluid25DCommandKind::Pause
                          : action.kind == "set-rain" ? Fluid25DCommandKind::SetRain
                                                      : Fluid25DCommandKind::SetTimeScale;
        auto value = action.value;
        if (action.kind == "set-rain")
            value = *value / 3.6e6;
        if (!active_->external_controls_allowed())
            return; // Health/pending transitions are observed, never bypassed.
        const auto id = active_->external_next_command_id_;
        active_->send_external_command(kind, value, guided);
        if (active_->external_pending_command_id_ != id || !active_->external_error_.empty())
            throw std::runtime_error("scheduled solver command was not submitted");
        expected_ack = id;
    }
    log(std::string(guided ? "guide-" : "action-") + action.kind);
    ++index;
    waiting = true;
}

bool ManagedSceneReload::guide_available() const {
    if (!control_ || !fluid_25d_rain_guide_site(control_->case_by_id(current_case_id_)) ||
        guide_active() || active_->shown_->time_s != 0 ||
        active_->shown_->stats.water_volume_m3 != 0 || !active_->external_controls_allowed())
        return false;
    const auto lifecycle = active_->external_snapshot_->header.lifecycle;
    const auto& capabilities = active_->backend_metadata_.capabilities.solver;
    return (lifecycle == Fluid25DLifecycle::Ready || lifecycle == Fluid25DLifecycle::Paused) &&
           capabilities.resume && capabilities.set_rain && capabilities.set_time_scale;
}

void ManagedSceneReload::start_guide() {
    if (!guide_available())
        return;
    guide_actions_ = fluid_25d_rain_guide_actions();
    guide_index_ = 0;
    guide_waiting_ = guide_finished_ = false;
    guide_action_started_ = {};
    guide_ack_id_.reset();
    guide_note_.clear();
    active_->select_camera("overview", true);
    log("guide-started");
}

void ManagedSceneReload::cancel_guide(const char* reason) {
    if (!guide_active())
        return;
    guide_actions_.clear();
    guide_waiting_ = false;
    guide_ack_id_.reset();
    guide_note_ = reason;
    log("guide-cancelled");
}

void ManagedSceneReload::run_guide() {
    if (!guide_active())
        return;
    const auto lifecycle = active_->external_snapshot_->header.lifecycle;
    if (!active_->external_error_.empty() || active_->managed_held_ || control_->busy() ||
        lifecycle == Fluid25DLifecycle::Completed || lifecycle == Fluid25DLifecycle::Stopped ||
        lifecycle == Fluid25DLifecycle::Failed) {
        cancel_guide("Guide stopped: producer is held, failed, replaced or at its horizon.");
        return;
    }
    try {
        run_sequence(guide_actions_, guide_index_, guide_waiting_, guide_action_started_,
                     guide_ack_id_, true);
    } catch (const std::exception& error) {
        cancel_guide(error.what());
        return;
    }
    if (guide_index_ == guide_actions_.size() && !guide_waiting_) {
        if (lifecycle != Fluid25DLifecycle::Running ||
            active_->external_snapshot_->rain_m_per_s != 0)
            throw std::runtime_error("guide ended without continued rain-off hydraulic evolution");
        guide_finished_ = true;
        guide_note_ = "Guide complete. The guide did not pause the solver; Reset water to repeat.";
        log("guide-complete-running");
    }
}

void ManagedSceneReload::poll(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots,
                              render::ColorTargetView target,
                              const std::function<void()>& wait_frames) {
    if (!control_)
        return;
    const auto now = std::chrono::steady_clock::now();
    bool status_changed = false;
    if (now >= next_poll_) {
        next_poll_ = now + std::chrono::milliseconds(100);
        if (control_->poll()) {
            status_changed = true;
            log("status-" + control_->status().phase);
        }
    }
    const auto& status = control_->status();
    active_->managed_held_ =
        control_->busy() ||
        (!status.worker_available && status.phase != "completed" && status.phase != "stopped") ||
        status.session_id != active_->backend_metadata_.session.session_id;
    if (status_changed && !control_->busy() && status.phase == "error" && status.worker_available &&
        status.session_id == active_->backend_metadata_.session.session_id) {
        // Failed preflight never stopped the current worker. Refresh its actual
        // publication rather than re-dating a held field after the loading pause.
        active_->external_refresh_after_render_setup_ = true;
    }
    if (status.phase == "ready" && status.request_id != preparing_request_id_ &&
        !prepared_.valid() && !uploaded_) {
        preparing_request_id_ = status.request_id;
        auto config = active_->config_;
        const auto& entry = control_->case_by_id(status.case_id);
        const auto& previous = control_->case_by_id(current_case_id_);
        config.external_session_path = control_->ready_session_directory();
        config.native_scenic_surface_grid = entry.surface_grid;
        if (entry.site != previous.site || entry.extent != previous.extent) {
            config.recording_camera = "overview";
            config.native_camera_target_x.reset();
            config.native_camera_target_z.reset();
            config.native_camera_span_m.reset();
        }
        const auto render = active_->render_;
        const auto material = active_->scenic_material_;
        const auto view = active_->view_;
        const auto reading = active_->catchment_view_;
        const auto debug = active_->debug_view_;
        const auto bank = active_->bank_view_;
        const auto expected_id = status.session_id;
        stage_ = "Loading terrain, climate and dry water fields";
        prepared_ = std::async(std::launch::async, [config, render, material, view, reading, debug,
                                                    bank, entry, expected_id] {
            auto replacement = std::make_unique<RecordingApp>(config);
            const auto& metadata = replacement->backend_metadata_;
            if (metadata.session.session_id != expected_id || metadata.grid.width != entry.width ||
                metadata.grid.height != entry.height ||
                metadata.grid.spacing_m != entry.spacing_m ||
                metadata.grid.input_sha256 != entry.input_sha256 ||
                metadata.grid.solver_bed_sha256 != entry.solver_bed_sha256 ||
                replacement->shown_->time_s != 0 || replacement->shown_->stats.max_depth_m != 0)
                throw std::runtime_error("replacement scene is not the selected dry session");
            replacement->render_ = render;
            replacement->scenic_material_ = material;
            // Pending GPU uploads must not mutate the active weather runtime.
            replacement->defer_weather_setup_ = true;
            replacement->view_ = view;
            replacement->catchment_view_ = reading;
            replacement->debug_view_ = debug;
            replacement->bank_view_ = bank;
            replacement->configure_camera();
            return replacement;
        });
    }
    bool retirement_started = false;
    try {
        if (prepared_.valid() &&
            prepared_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto replacement = prepared_.get();
            if (status.phase == "ready" && status.request_id == preparing_request_id_) {
                stage_ = "Preparing Vulkan scene resources";
                uploaded_.emplace(gpu.submit(
                    "terrain water replacement", [replacement = std::move(replacement), slots,
                                                  target](vulkan::GpuOwnerContext& owner) mutable {
                        try {
                            replacement->create_resources(owner.device(), owner.runtime(), slots);
                            replacement->create_render_resources(owner.device(), target);
                            return std::move(replacement);
                        } catch (...) {
                            replacement->shutdown();
                            throw;
                        }
                    }));
            }
        }
        if (uploaded_ && uploaded_->ready()) {
            auto upload = std::move(*uploaded_);
            uploaded_.reset();
            auto replacement = upload.get();
            wait_frames();
            if (status.phase != "ready" || status.request_id != preparing_request_id_) {
                replacement->shutdown();
            } else {
                const auto& previous = control_->case_by_id(current_case_id_);
                const auto& next = control_->case_by_id(status.case_id);
                if (previous.site == next.site && previous.extent == next.extent) {
                    replacement->camera_ = active_->camera_;
                    replacement->orbit_ = active_->orbit_;
                    replacement->target_ = active_->target_;
                }
                retirement_started = true;
                // All active frame submissions have retired; one controller
                // survives the swap, with no parallel weather descriptor writes.
                replacement->weather_ = std::move(active_->weather_);
                replacement->defer_weather_setup_ = false;
                active_->shutdown(); // Drains reader/whitewater jobs before their bed is released.
                if (active_->shutdown_failure_) {
                    replacement->shutdown();
                    std::rethrow_exception(active_->shutdown_failure_);
                }
                active_->external_session_.reset();
                active_->external_snapshot_.reset();
                active_->shown_.reset();
                active_->scenario_ = {};
                std::vector<Fluid25DVelocityGpu>().swap(active_->packed_velocity_);
                active_->whitewater_selection_ = {};
                active_->terrain_surface_.reset();
                installed_ = std::move(replacement);
                active_ = installed_.get();
                active_->managed_demo_ = this;
                current_case_id_ = selected_case_id = next.id;
                active_->managed_held_ = true;
                control_->confirm_installed(true);
                stage_ = "Scene installed; waiting for launcher acknowledgement";
                log("installed");
            }
        }
    } catch (const std::exception& error) {
        if (retirement_started)
            throw; // A failed numerical/upload audit is fatal, never a recoverable old scene.
        if (status.phase == "ready")
            control_->confirm_installed(false, error.what());
        stage_ = std::string("Replacement failed: ") + error.what();
        if (!script_.empty())
            throw;
    }
    if (!control_->busy())
        stage_.clear();
    run_script();
    run_guide();
    static_cast<void>(device); // GPU installation belongs to the shared runtime owner.
}

void ManagedSceneReload::shutdown(vulkan::GpuRuntime& gpu) {
    // Close during any loading stage still joins CPU work and releases unused
    // GPU products while the host device/runtime are alive.
    if (prepared_.valid()) {
        try {
            static_cast<void>(prepared_.get());
        } catch (...) {
        }
    }
    if (uploaded_) {
        try {
            auto unused = uploaded_->get();
            gpu.wait_queue_idle("unused terrain water replacement");
            unused->shutdown();
        } catch (...) {
        }
        uploaded_.reset();
    }
    active_->shutdown();
    if (active_->shutdown_failure_)
        std::rethrow_exception(active_->shutdown_failure_);
    if (!script_.empty() && (action_index_ != script_.size() || action_waiting_))
        throw std::runtime_error("managed regression ended before all actions were acknowledged");
    if (control_)
        log("viewer-closed");
}
} // namespace

int run_fluid_25d_recording(const Fluid25DProjectConfig& config) {
    RecordingApp app(config);
    return app.run();
}
} // namespace cubey::projects::fluid::fluid_25d
