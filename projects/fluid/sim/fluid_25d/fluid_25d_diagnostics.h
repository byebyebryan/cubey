#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_gpu_resources.h"

#include <cubey/core/profiling.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/common_config.h>
#include <cubey/render/frame_data.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
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
inline constexpr std::size_t kFluid25DBoundaryOutflowBinCount = 16U;
inline constexpr std::size_t kFluid25DSustainedHeadwatersStationCount = 7U;

struct Fluid25DSustainedHeadwatersStationDiagnostics {
    std::string_view name{};
    std::uint32_t x_cell = 0U;
    std::uint32_t y_cell = 0U;
    double depth_m = 0.0;
    double velocity_x_m_per_s = 0.0;
    double velocity_y_m_per_s = 0.0;
};

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
    // Diagnostic attribution of the existing per-cell boundary ledger. Corner
    // cells may have two open faces, so their combined volume is kept separate
    // rather than assigned to either side. Non-edge volume should remain zero.
    std::array<double, kFluid25DBoundaryOutflowBinCount> north_outflow_bins_m3{};
    std::array<double, kFluid25DBoundaryOutflowBinCount> south_outflow_bins_m3{};
    std::array<double, kFluid25DBoundaryOutflowBinCount> west_outflow_bins_m3{};
    std::array<double, kFluid25DBoundaryOutflowBinCount> east_outflow_bins_m3{};
    double corner_outflow_volume_m3 = 0.0;
    double non_edge_outflow_volume_m3 = 0.0;
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

struct Fluid25DSourceOutletCrossSectionStation {
    std::string_view name{};
    std::uint32_t x_cell = 0U;
    bool endpoint_pool_affected = false;
};

inline constexpr std::size_t kFluid25DSourceOutletCrossSectionStationCount = 5U;

// Metrics cover the bank-to-bank cell span, inclusive of the selected bank
// crest cells. A wet cell has h > minimum_wet_depth_m. Wetted width is wet-cell
// count times dx; water area is sum(h*dx); wet and section mean depths divide
// that area by wetted width and bank-to-bank width respectively. Surface is
// water-area-weighted terrain+h over wet cells, with *_valid=0 and a numeric
// zero when dry. Each bank crest is the maximum immutable terrain sample in
// the authored shoulder band 1.0 <= |(y-center)/half_width| <= 2.5; if that
// band has no grid sample on a side, the highest sample on that side is used
// and the fallback flag is set. Lower crest is the lesser side elevation.
// Bankfull capacity is sum(max(lower_crest-terrain, 0)*dx) only between the
// two selected crest cells. Bankfull fraction is in-bank section area divided
// by that capacity, unclamped; its validity flag is zero for zero capacity.
// Discharge is the approximate depth-velocity estimate sum(h*u_x*dx) over wet
// in-bank cells, not a solver face flux. Mean x velocity is Q/A. Froude uses
// abs(Q/A)/sqrt(g*A/wetted_width). Wet cells beyond either selected crest are
// counted separately as overbank cells and are excluded from in-bank metrics.
struct Fluid25DSourceOutletCrossSectionDiagnostics {
    std::string_view station_name{};
    std::uint32_t station_x_cell = 0U;
    std::uint32_t left_bank_crest_y_cell = 0U;
    std::uint32_t right_bank_crest_y_cell = 0U;
    std::uint64_t wetted_cell_count = 0U;
    std::uint64_t overbank_wet_cell_count = 0U;
    std::uint32_t left_bank_crest_fallback = 0U;
    std::uint32_t right_bank_crest_fallback = 0U;
    bool endpoint_pool_affected = false;
    double station_x_m = 0.0;
    double left_bank_crest_elevation_m = 0.0;
    double right_bank_crest_elevation_m = 0.0;
    double lower_bank_crest_elevation_m = 0.0;
    double centerline_bed_elevation_m = 0.0;
    double bank_to_bank_width_m = 0.0;
    double wetted_cell_width_m = 0.0;
    double wetted_width_fraction = 0.0;
    double representative_free_surface_elevation_m = 0.0;
    double representative_free_surface_valid = 0.0;
    double section_water_area_m2 = 0.0;
    double mean_wet_depth_m = 0.0;
    double mean_section_depth_m = 0.0;
    double bankfull_capacity_area_m2 = 0.0;
    double bankfull_fraction = 0.0;
    double bankfull_capacity_valid = 0.0;
    double depth_velocity_discharge_estimate_m3_per_s = 0.0;
    double mean_x_velocity_m_per_s = 0.0;
    double froude_estimate = 0.0;
};

struct Fluid25DSourceOutletSpatialZoneDiagnostics {
    std::uint64_t wet_cell_count = 0U;
    std::uint64_t material_dyed_wet_cell_count = 0U;
    double water_volume_m3 = 0.0;
    double tracer_amount_m3 = 0.0;
    double maximum_wet_depth_m = 0.0;
};

struct Fluid25DSourceOutletSpatialDiagnostics {
    Fluid25DSourceOutletSpatialZoneDiagnostics before_source{};
    Fluid25DSourceOutletSpatialZoneDiagnostics route_off_bank{};
    Fluid25DSourceOutletSpatialZoneDiagnostics route_in_bank{};
    Fluid25DSourceOutletSpatialZoneDiagnostics after_outlet{};
    // One representative free surface per source-to-outlet column, using the
    // same water-area weighting as the five named cross-section stations.
    // This captures gaps between those stations without changing solver state.
    std::uint32_t route_section_count = 0U;
    std::uint32_t route_wet_section_count = 0U;
    std::uint32_t route_freeboard_above_half_m_section_count = 0U;
    double route_min_freeboard_m = 0.0;
    double route_mean_freeboard_m = 0.0;
    double route_max_freeboard_m = 0.0;
    double route_min_centerline_depth_m = 0.0;
    double route_mean_centerline_depth_m = 0.0;
    double route_max_centerline_depth_m = 0.0;
    bool material_dyed_after_outlet_valid = false;
    std::uint32_t material_dyed_max_x_after_outlet_cell_x = 0U;
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

// Fixed probes are a reading aid for the authored dry-start Y control. They
// sample the completed solver state; no probe feeds back into the simulation.
[[nodiscard]] std::array<Fluid25DSustainedHeadwatersStationDiagnostics,
                         kFluid25DSustainedHeadwatersStationCount>
compute_fluid_25d_sustained_headwaters_stations(const Fluid25DConfig& config,
                                                std::span<const float> depth_m,
                                                std::span<const Fluid25DVelocityGpu> velocity);

[[nodiscard]] Fluid25DTracerProfileDiagnostics
compute_fluid_25d_tracer_profile_diagnostics(
    const Fluid25DConfig& config, std::span<const float> depth_m,
    std::span<const float> tracer_q_m, std::span<const float> sink_depth_rate_m_per_s,
    std::span<const Fluid25DTracerLedgerGpu> cumulative_tracer_ledger,
    double initial_tracer_amount_m3 = 0.0);

[[nodiscard]] std::array<Fluid25DSourceOutletCrossSectionStation,
                         kFluid25DSourceOutletCrossSectionStationCount>
fluid_25d_source_outlet_cross_section_stations(std::uint32_t grid_width, std::uint32_t grid_height);

[[nodiscard]] Fluid25DSourceOutletCrossSectionDiagnostics
compute_fluid_25d_source_outlet_cross_section_diagnostics(
    const Fluid25DConfig& config, const Fluid25DSourceOutletCrossSectionStation& station,
    std::span<const float> terrain_height_m, std::span<const float> depth_m,
    std::span<const Fluid25DVelocityGpu> velocity);

// Spatial totals partition every cell exactly once. Before/after zones include
// all rows at x<source_x and x>sink_x. Within the inclusive source-to-sink
// route, cells between the inclusive minimum/maximum selected crest rows are
// in-bank; all other rows are route off-bank. Wet means h>minimum_wet_depth_m.
// Water volume is sum(h*cell_area), and tracer amount is sum(clamp(q,0,h)*area)
// over every zone cell, including dry-cell tracer residue. Tracer q tolerates
// the existing 1e-7 m rounding residue before clamping. A material dyed wet
// cell is wet with q/h >= kFluid25DTracerMaterialConcentration. Maximum wet
// depth ignores dry cells. After-outlet is a closed-domain tail zone, not a
// boundary-loss classification. The recorder uses category
// `fluid_25d.river_spatial` and stable names `<zone>.<metric>` plus
// `material_dyed_max_x_after_outlet_*`.
[[nodiscard]] Fluid25DSourceOutletSpatialDiagnostics
compute_fluid_25d_source_outlet_spatial_diagnostics(const Fluid25DConfig& config,
                                                    std::span<const float> terrain_height_m,
                                                    std::span<const float> depth_m,
                                                    std::span<const float> tracer_q_m);

void record_fluid_25d_profile_diagnostics(cubey::profiling::ProfileRecorder& recorder,
                                          std::uint64_t frame_index,
                                          const Fluid25DProfileDiagnostics& diagnostics);
void record_fluid_25d_sustained_headwaters_stations(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const std::array<Fluid25DSustainedHeadwatersStationDiagnostics,
                     kFluid25DSustainedHeadwatersStationCount>& stations);
void record_fluid_25d_boundary_outflow_diagnostics(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const Fluid25DProfileDiagnostics& diagnostics);
void record_fluid_25d_tracer_profile_diagnostics(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const Fluid25DTracerProfileDiagnostics& diagnostics);
void record_fluid_25d_source_outlet_cross_section_diagnostics(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const Fluid25DSourceOutletCrossSectionDiagnostics& diagnostics);
void record_fluid_25d_source_outlet_spatial_diagnostics(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const Fluid25DSourceOutletSpatialDiagnostics& diagnostics);

} // namespace cubey::projects::fluid::fluid_25d
