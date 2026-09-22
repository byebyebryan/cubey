#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_gpu_resources.h"

#include <cubey/core/profiling.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/common_config.h>
#include <cubey/render/frame_data.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <cstdint>
#include <span>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

[[nodiscard]] std::uint64_t profile_frame_index(const ProjectFrame& frame);
[[nodiscard]] std::uint64_t collected_profile_frame_index(const ProjectFrame& frame,
                                                          cubey::render::FrameSlot frame_slot);

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings);

// The threshold is intentionally a diagnostic classification, not a solver
// parameter. Wet cells at or below this speed are reported as slow/pooled so
// auditions can distinguish broad retained water from active routing without
// changing numerical evolution.
inline constexpr float kFluid25DSlowPooledSpeedThresholdMPerS = 0.02F;
// Presentation/evidence classification only: conservative q is retained all
// the way to dry cells, but numerical Rusanov tails below 1% concentration do
// not constitute a material dyed front.
inline constexpr float kFluid25DTracerMaterialConcentration = 0.01F;

struct Fluid25DProfileDiagnostics {
    std::uint64_t wet_cell_count = 0U;
    double wet_cell_ratio = 0.0;
    double total_water_volume_m3 = 0.0;
    double maximum_depth_m = 0.0;
    double wet_mean_depth_m = 0.0;
    std::uint64_t active_flow_cell_count = 0U;
    double active_flow_cell_ratio = 0.0;
    double maximum_speed_m_per_s = 0.0;
    double active_mean_speed_m_per_s = 0.0;
    double slow_pooled_wet_fraction = 0.0;
    double cumulative_source_volume_m3 = 0.0;
    double cumulative_sink_volume_m3 = 0.0;
    double cumulative_boundary_outflow_volume_m3 = 0.0;
    double conservation_residual_m3 = 0.0;
};

struct Fluid25DTracerProfileDiagnostics {
    double total_tracer_amount_m3 = 0.0;
    double maximum_concentration = 0.0;
    double mean_concentration = 0.0;
    std::uint64_t dyed_wet_cell_count = 0U;
    double dyed_wet_cell_ratio = 0.0;
    double amount_weighted_centroid_cell_x = 0.0;
    double amount_weighted_centroid_cell_y = 0.0;
    double downstream_extent_cell_x = 0.0;
    double tracer_in_explicit_sink_region_m3 = 0.0;
    double cumulative_source_amount_m3 = 0.0;
    double cumulative_sink_amount_m3 = 0.0;
    double cumulative_boundary_outflow_amount_m3 = 0.0;
    double conservation_residual_m3 = 0.0;
};

[[nodiscard]] bool
should_record_fluid_25d_profile_diagnostics(cubey::profiling::ProfileRecorder* recorder,
                                            const cubey::host::CommonRunConfig& common_config,
                                            std::uint64_t frame_index);

[[nodiscard]] double fluid_25d_water_volume_m3(const Fluid25DConfig& config,
                                               std::span<const float> depth_m);

[[nodiscard]] Fluid25DProfileDiagnostics
compute_fluid_25d_profile_diagnostics(const Fluid25DConfig& config, std::span<const float> depth_m,
                                      std::span<const Fluid25DVelocityGpu> velocity,
                                      std::span<const Fluid25DLedgerGpu> cumulative_ledger,
                                      double initial_water_volume_m3);

[[nodiscard]] Fluid25DTracerProfileDiagnostics
compute_fluid_25d_tracer_profile_diagnostics(
    const Fluid25DConfig& config, std::span<const float> depth_m,
    std::span<const float> tracer_q_m, std::span<const float> sink_depth_rate_m_per_s,
    std::span<const Fluid25DTracerLedgerGpu> cumulative_tracer_ledger,
    double initial_tracer_amount_m3 = 0.0);

void record_fluid_25d_profile_diagnostics(cubey::profiling::ProfileRecorder& recorder,
                                          std::uint64_t frame_index,
                                          const Fluid25DProfileDiagnostics& diagnostics);
void record_fluid_25d_tracer_profile_diagnostics(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const Fluid25DTracerProfileDiagnostics& diagnostics);

} // namespace cubey::projects::fluid::fluid_25d
