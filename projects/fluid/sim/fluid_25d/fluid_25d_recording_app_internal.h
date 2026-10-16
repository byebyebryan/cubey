#pragma once

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
namespace recording_detail {

struct WhitewaterBuild {
    Fluid25DWhitewaterSelection selection;
    double source_s = 0.0, cpu_ms = 0.0, published_unix_s = 0.0;
    std::uint64_t epoch = 0U;
};

const char* lifecycle_name(Fluid25DLifecycle lifecycle);

const char* backend_profile_name(Fluid25DBackendProfile profile);

const char* acknowledgement_name(Fluid25DAcknowledgementState state);

const char* external_health_name(fluid_25d_project_config_detail::ExternalSessionHealth health);

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
    bool guided_start_selected() const;
    bool guide_preferred() const {
        return guide_preferred_;
    }
    void set_guide_preferred(bool preferred);
    void start_or_resume();
    void manual_control(const char* reason);
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
    bool guide_preferred_ = true;
    std::chrono::steady_clock::time_point guide_action_started_{};
    std::string guide_note_;
    std::optional<std::uint64_t> action_ack_id_, guide_ack_id_;
};

class RecordingApp {
    friend class ManagedSceneReload;

  public:
    explicit RecordingApp(const Fluid25DProjectConfig& config);

    int run();

  private:
    void activate_comparison_cache();

    void poll_comparison_cache();

    [[nodiscard]] bool matching_mask_ready() const;

    [[nodiscard]] std::span<const float> active_coverage() const;

    void set_bank_view(Fluid25DBankView view);

    void cycle_bank_view();

    void seek_playback(double time);

    void toggle_playback();

    void constrain_comparison_clock(bool was_playing);

    [[nodiscard]] double external_publication_age_s() const;

    [[nodiscard]] double external_seconds_since_valid_publication() const;

    [[nodiscard]] fluid_25d_project_config_detail::ExternalSessionHealth external_health() const;

    [[nodiscard]] bool external_controls_allowed() const;

    void fail_external_session(const std::string& message);

    void accept_frame(std::shared_ptr<const Fluid25DRecordedFrame> frame);

    void prepare_whitewater_sources();

    void reset_presentation_history();

    void reset_visual_history();

    void restart_playback();

    void synchronize_playback_boundary();

    void poll_frame_load();

    void poll_stream();

    void send_external_command(Fluid25DCommandKind kind, std::optional<double> value = {},
                               bool guided = false);

    void poll_external_session();

    std::optional<Fluid25DExternalSnapshot> timed_external_read();

    void update_view(double wall_delta_s);

    void create_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots);

    void ensure_scenic_resources(vulkan::Device& device, render::ColorTargetView target);

    void create_render_resources(vulkan::Device& device, render::ColorTargetView target);

    void select_camera(const char* name, bool guided = false);

    void configure_camera();

    Fluid25DRenderCamera camera(VkExtent2D extent, std::uint64_t frame_index = 0U) const;

    void record(vulkan::Device& device, VkCommandBuffer commands, render::ColorTargetView target,
                render::FrameSlot slot, Fluid25DRenderTargetMode target_mode,
                profiling::ProfileRecorder* profile, std::uint64_t frame_index);

    void collect_native_timings(std::uint32_t slot, profiling::ProfileRecorder* profile);

    template <typename Value>
    void check_buffer(const vulkan::Buffer& buffer, std::span<const Value> expected,
                      const char* name) {
        const auto bytes = runtime_.gpu().readback_buffer(buffer.handle(), buffer.size(), name);
        if (bytes.size() != expected.size_bytes() ||
            std::memcmp(bytes.data(), expected.data(), bytes.size()) != 0)
            throw std::runtime_error(std::string("recording GPU immutability mismatch: ") + name);
    }

    void shutdown();

    bool scenic_material_active() const;

    std::optional<double> applied_rain_mm_per_hour() const;

    Fluid25DRainVisualFrame rain_visual_frame() const;

    void log_rain_capture(std::uint32_t frame) const;

    void draw_native_color_legend() const;

    void select_native_style(std::uint32_t style);

    void report_scenic_material() const;

    void draw_native_presentation_ui();

    void draw_external_rain_ui(bool busy);

    void draw_terrain_selection_ui(ManagedSceneReload& demo);

    void draw_external_ui(ManagedSceneReload* demo = nullptr);

    void draw_ui(ManagedSceneReload* demo = nullptr);

    int run_windowed();

    void update_windowed(host::WindowedAppContext& context, const FrameTiming& timing,
                         ManagedSceneReload* reload);

    int run_headless();

    void record_headless_frame(host::HeadlessPngContext& context,
                               const host::HeadlessCaptureFrame& frame, VkCommandBuffer commands,
                               const host::HeadlessRenderTarget& target);

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

} // namespace recording_detail
} // namespace cubey::projects::fluid::fluid_25d
