#pragma once

#include "fluid_25d_app.h"

#include "fluid_25d_backend_adapters.h"
#include "fluid_25d_commands.h"
#include "fluid_25d_diagnostics.h"
#include "fluid_25d_finite_volume_oracle.h"
#include "fluid_25d_gpu_resources.h"
#include "fluid_25d_hillside_supply.h"
#include "fluid_25d_local_session.h"
#include "fluid_25d_mass_audit.h"
#include "fluid_25d_motion_markers.h"
#include "fluid_25d_natural_flow_recipe.h"
#include "fluid_25d_oracle.h"
#include "fluid_25d_rain_study.h"
#include "fluid_25d_recording_app.h"
#include "fluid_25d_scenarios.h"
#include "fluid_25d_ui.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/immediate_commands.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace builtin_detail {

inline constexpr float kDepthToleranceM = 0.0005F;
inline constexpr float kFluxToleranceM3PerS = 0.002F;
inline constexpr float kMomentumToleranceM2PerS = 0.002F;
inline constexpr float kVelocityToleranceMPerS = 0.002F;
inline constexpr float kTracerQToleranceM = 0.0005F;
inline constexpr float kTracerConcentrationTolerance = 0.01F;
inline constexpr double kMinimumLedgerToleranceM3 = 0.003;
inline constexpr double kRelativeLedgerTolerance = 3.0e-4;
inline constexpr float kCatchmentCameraBaseYaw = -0.52F;
inline constexpr float kCatchmentCameraBasePitch = -0.92F;
// The terrain-and-water presentation spans kilometres in X/Z while deliberately
// compressing elevation. A conventional 0.1 m near plane leaves too little
// forward-Z precision to distinguish a valid shallow sheet from its bed at the
// far side of the catchment. This stays safely inside the closest permitted
// orbit (30% of the horizontal extent) while scaling with every terrain crop.
inline constexpr float kCatchmentCameraNearExtentFraction = 0.10F;
inline constexpr float kCatchmentCameraMinimumNearPlaneM = 8.0F;

[[nodiscard]] inline std::uint32_t headless_frame_count(const host::CommonRunConfig& config) {
    return config.frames == 0U ? 120U : config.frames;
}

[[nodiscard]] inline FrameTiming fixed_headless_timing(const Fluid25DConfig& config,
                                                       std::uint64_t frame_index) {
    if (frame_index == 0U) {
        throw std::runtime_error("fluid 2.5D fixed headless frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

template <typename Value>
[[nodiscard]] std::vector<Value> readback_values(cubey::ProjectGpuServices& gpu,
                                                 const cubey::vulkan::Buffer& buffer,
                                                 std::size_t value_count, const char* label) {
    const VkDeviceSize expected_size = static_cast<VkDeviceSize>(value_count * sizeof(Value));
    if (buffer.size() != expected_size) {
        throw std::runtime_error(std::string("fluid 2.5D ") + label +
                                 " buffer has an unexpected byte size");
    }
    const std::vector<std::uint8_t> bytes =
        gpu.readback_buffer(buffer.handle(), buffer.size(), label);
    if (bytes.size() != static_cast<std::size_t>(expected_size)) {
        throw std::runtime_error(std::string("fluid 2.5D ") + label +
                                 " readback has an unexpected byte size");
    }
    std::vector<Value> values(value_count);
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

[[nodiscard]] inline bool finite(float value) {
    return std::isfinite(value);
}

[[nodiscard]] inline double cumulative_ledger_tolerance_m3(double expected_volume_m3) {
    if (!std::isfinite(expected_volume_m3)) {
        throw std::runtime_error("fluid 2.5D expected cumulative ledger is nonfinite");
    }
    // GPU distributed per-cell cumulative ledger is float, while CPU oracle
    // accumulation is double. Scale the ledger tolerance at 0.03%; state
    // tolerances remain unchanged, and zero expected boundary flow stays strict.
    return std::max(kMinimumLedgerToleranceM3,
                    std::abs(expected_volume_m3) * kRelativeLedgerTolerance);
}

[[nodiscard]] Fluid25DScenarioData make_startup_scenario(Fluid25DProjectConfig& config);

class Fluid25DApp {
  public:
    explicit Fluid25DApp(Fluid25DProjectConfig config);

    Fluid25DApp(const Fluid25DApp&) = delete;
    Fluid25DApp& operator=(const Fluid25DApp&) = delete;

    int run();

  private:
    void refresh_builtin_boundary(cubey::ProjectGpuServices& gpu, bool force);

    int run_windowed();

    void draw_ui();

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count);

    void record_marker_timings(cubey::profiling::ProfileRecorder* recorder, std::uint32_t slot,
                               bool draw_only = false, bool update_only = false);

    void record_windowed_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& render_frame);

    float source_rate_scale_for_step(const Fluid25DSourceRateSchedule& clock,
                                     Fluid25DHillsideSupply& supply,
                                     Fluid25DRainStudyControl& rain_study);

    void configure_catchment_camera();

    [[nodiscard]] cubey::Transform3D render_camera_transform() const;

    [[nodiscard]] Fluid25DRenderCamera render_camera(VkExtent2D extent) const;

    [[nodiscard]] bool flow_inspection_active() const noexcept;

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const cubey::host::HeadlessCaptureFrame& frame,
                                          cubey::profiling::ProfileRecorder* profile_recorder);

    void record_headless_profile_diagnostics_if_requested(
        cubey::ProjectGpuServices& gpu, cubey::profiling::ProfileRecorder* profile_recorder,
        std::uint64_t frame_index);

    [[nodiscard]] std::uint32_t read_finite_volume_status(cubey::ProjectGpuServices& gpu,
                                                          const char* context) const;

    void validate_gpu_oracle(cubey::ProjectGpuServices& gpu);

    int run_headless();

    Fluid25DProjectConfig config_;
    Fluid25DWindowedPacing windowed_pacing_;
    Fluid25DInspectionAdvance inspection_advance_;
    Fluid25DScenarioData scenario_;
    Fluid25DBackendMetadata backend_metadata_;
    std::optional<Fluid25DLocalSession> local_session_;
    std::chrono::steady_clock::time_point next_backend_status_poll_{};
    bool backend_reset_in_flight_ = false;
    bool backend_applied_paused_ = false;
    float backend_applied_time_scale_ = 1.0F;
    cubey::ProjectRuntimeAdapter runtime_{1};
    Fluid25DGpuResources resources_;
    Fluid25DMotionMarkers motion_markers_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::math::Vec3 catchment_target_{0.0F, 0.0F, 0.0F};
    Fluid25DPresentationView presentation_view_ = Fluid25DPresentationView::Catchment;
    Fluid25DCatchmentView catchment_view_ = Fluid25DCatchmentView::Composite;
    Fluid25DDebugView debug_view_ = Fluid25DDebugView::Terrain;
    std::optional<Fluid25DOracle> oracle_;
    std::optional<Fluid25DFiniteVolumeOracle> finite_volume_oracle_;
    Fluid25DSourceRateSchedule source_schedule_;
    Fluid25DDyeSourceSchedule dye_source_schedule_;
    Fluid25DHillsideSupply hillside_supply_;
    Fluid25DRainStudyControl rain_study_;
    float last_source_m3_per_s_ = 0.0F;
    double scheduled_source_volume_m3_ = 0.0;
    double expected_source_volume_m3_ = 0.0;
    double expected_sink_volume_m3_ = 0.0;
    double expected_boundary_outflow_volume_m3_ = 0.0;
    double expected_tracer_source_amount_m3_ = 0.0;
    double expected_tracer_sink_amount_m3_ = 0.0;
    double expected_tracer_boundary_outflow_amount_m3_ = 0.0;
    double initial_water_volume_m3_ = 0.0;
    bool paused_ = false;
    bool stopped_ = false;
    bool resume_after_advance_ = false;
    bool show_motion_markers_ = false;
    Fluid25DMotionMarkerDisplayClock marker_display_clock_;
    double continuous_wall_seconds_ = 0.0;
    double continuous_physical_seconds_ = 0.0;
    // Headless simulation keeps the uploaded initial numerical state and
    // solver dispatch stream unchanged. Windowed presentation initializes its
    // render-only cue separately on the first frame or an explicit reset.
    bool reset_requested_ = false;
    bool presentation_cue_reset_requested_ = true;
    bool quiver_reset_requested_ = true;
};

} // namespace builtin_detail
} // namespace cubey::projects::fluid::fluid_25d
