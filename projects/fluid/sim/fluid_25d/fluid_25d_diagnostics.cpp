#include "fluid_25d_diagnostics.h"
#include "fluid_25d_scenarios.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

[[nodiscard]] std::string diagnostic_float(float value) {
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return std::move(stream).str();
}

void record_cross_section_metric(cubey::profiling::ProfileRecorder& recorder,
                                 std::uint64_t frame_index, std::string_view station_name,
                                 std::string_view metric_name, double value) {
    const std::string name =
        "station." + std::string(station_name) + "." + std::string(metric_name);
    recorder.record_metric(frame_index, "fluid_25d.river_cross_section", name, value);
}

} // namespace

std::uint64_t profile_frame_index(const ProjectFrame& frame) {
    return frame.frame_index == 0U ? 0U : frame.frame_index - 1U;
}

std::uint64_t collected_profile_frame_index(const ProjectFrame& frame,
                                            cubey::render::FrameSlot frame_slot) {
    if (frame.frame_index > frame_slot.count) {
        return frame.frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame);
}

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings) {
    if (recorder == nullptr) {
        return;
    }
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        recorder->record_gpu_span(frame_index, timing.label, timing.milliseconds);
    }
}

bool should_record_fluid_25d_profile_diagnostics(cubey::profiling::ProfileRecorder* recorder,
                                                 const cubey::host::CommonRunConfig& common_config,
                                                 std::uint64_t frame_index) {
    if (recorder == nullptr || !common_config.headless || !common_config.profile_diagnostics ||
        common_config.profile_diagnostic_interval == 0U) {
        return false;
    }
    return recorder->should_record_frame(frame_index) &&
           (frame_index % common_config.profile_diagnostic_interval) == 0U;
}

double fluid_25d_water_volume_m3(const Fluid25DConfig& config, std::span<const float> depth_m) {
    validate_fluid_25d_config(config);
    const std::size_t cells = fluid_25d_cell_count(config);
    if (depth_m.size() != cells) {
        throw std::runtime_error("fluid 2.5D diagnostic depth field has invalid dimensions");
    }
    const double cell_area_m2 =
        static_cast<double>(config.cell_size_m) * static_cast<double>(config.cell_size_m);
    double volume_m3 = 0.0;
    for (const float depth : depth_m) {
        if (!std::isfinite(depth) || depth < 0.0F) {
            throw std::runtime_error("fluid 2.5D diagnostic depth is invalid");
        }
        volume_m3 += static_cast<double>(depth) * cell_area_m2;
    }
    if (!std::isfinite(volume_m3)) {
        throw std::runtime_error("fluid 2.5D diagnostic volume is nonfinite");
    }
    return volume_m3;
}

Fluid25DProfileDiagnostics
compute_fluid_25d_profile_diagnostics(const Fluid25DConfig& config, std::span<const float> depth_m,
                                      std::span<const Fluid25DVelocityGpu> velocity,
                                      std::span<const Fluid25DLedgerGpu> cumulative_ledger,
                                      double initial_water_volume_m3) {
    validate_fluid_25d_config(config);
    const std::size_t cells = fluid_25d_cell_count(config);
    if (velocity.size() != cells || cumulative_ledger.size() != cells) {
        throw std::runtime_error("fluid 2.5D diagnostic fields have invalid dimensions");
    }
    if (!std::isfinite(initial_water_volume_m3) || initial_water_volume_m3 < 0.0) {
        throw std::runtime_error("fluid 2.5D diagnostic initial water volume is invalid");
    }

    Fluid25DProfileDiagnostics result;
    result.total_water_volume_m3 = fluid_25d_water_volume_m3(config, depth_m);
    double wet_depth_sum_m = 0.0;
    double active_speed_sum_m_per_s = 0.0;
    for (std::size_t index = 0U; index < cells; ++index) {
        const float depth = depth_m[index];
        const Fluid25DVelocityGpu state = velocity[index];
        const float speed_m_per_s = std::hypot(state.velocity_wet[0], state.velocity_wet[1]);
        if (!std::isfinite(speed_m_per_s) || !std::isfinite(state.velocity_wet[2]) ||
            !std::isfinite(state.velocity_wet[3])) {
            throw std::runtime_error("fluid 2.5D diagnostic velocity is invalid");
        }
        result.maximum_depth_m = std::max(result.maximum_depth_m, static_cast<double>(depth));
        if (depth <= config.minimum_wet_depth_m) {
            continue;
        }
        ++result.wet_cell_count;
        wet_depth_sum_m += static_cast<double>(depth);
        result.maximum_speed_m_per_s =
            std::max(result.maximum_speed_m_per_s, static_cast<double>(speed_m_per_s));
        if (speed_m_per_s > kFluid25DSlowPooledSpeedThresholdMPerS) {
            ++result.active_flow_cell_count;
            active_speed_sum_m_per_s += static_cast<double>(speed_m_per_s);
        }
    }
    result.wet_cell_ratio = static_cast<double>(result.wet_cell_count) / static_cast<double>(cells);
    result.wet_mean_depth_m = result.wet_cell_count == 0U
                                  ? 0.0
                                  : wet_depth_sum_m / static_cast<double>(result.wet_cell_count);
    result.active_flow_cell_ratio =
        static_cast<double>(result.active_flow_cell_count) / static_cast<double>(cells);
    result.active_mean_speed_m_per_s =
        result.active_flow_cell_count == 0U
            ? 0.0
            : active_speed_sum_m_per_s / static_cast<double>(result.active_flow_cell_count);
    result.slow_pooled_wet_fraction =
        result.wet_cell_count == 0U
            ? 0.0
            : static_cast<double>(result.wet_cell_count - result.active_flow_cell_count) /
                  static_cast<double>(result.wet_cell_count);

    for (std::size_t index = 0; index < cumulative_ledger.size(); ++index) {
        const Fluid25DLedgerGpu& ledger = cumulative_ledger[index];
        for (std::size_t component = 0U; component < ledger.source_sink_boundary_reserved_m3.size();
             ++component) {
            const float value = ledger.source_sink_boundary_reserved_m3[component];
            if (!std::isfinite(value)) {
                throw std::runtime_error(
                    "fluid 2.5D diagnostic ledger is nonfinite at cell=" + std::to_string(index) +
                    " component=" + std::to_string(component) +
                    " value=" + diagnostic_float(value));
            }
        }
        if (ledger.source_sink_boundary_reserved_m3[0] < 0.0F ||
            ledger.source_sink_boundary_reserved_m3[1] < 0.0F ||
            ledger.source_sink_boundary_reserved_m3[2] < 0.0F) {
            const std::size_t component =
                ledger.source_sink_boundary_reserved_m3[0] < 0.0F
                    ? 0U
                    : (ledger.source_sink_boundary_reserved_m3[1] < 0.0F ? 1U : 2U);
            throw std::runtime_error(
                "fluid 2.5D diagnostic ledger is negative at cell=" + std::to_string(index) +
                " component=" + std::to_string(component) +
                " value=" + diagnostic_float(ledger.source_sink_boundary_reserved_m3[component]));
        }
        result.cumulative_source_volume_m3 +=
            static_cast<double>(ledger.source_sink_boundary_reserved_m3[0]);
        result.cumulative_sink_volume_m3 +=
            static_cast<double>(ledger.source_sink_boundary_reserved_m3[1]);
        result.cumulative_boundary_outflow_volume_m3 +=
            static_cast<double>(ledger.source_sink_boundary_reserved_m3[2]);
    }
    result.conservation_residual_m3 = result.total_water_volume_m3 - initial_water_volume_m3 -
                                      result.cumulative_source_volume_m3 +
                                      result.cumulative_sink_volume_m3 +
                                      result.cumulative_boundary_outflow_volume_m3;
    if (!std::isfinite(result.conservation_residual_m3)) {
        throw std::runtime_error("fluid 2.5D diagnostic conservation residual is nonfinite");
    }
    return result;
}

Fluid25DTracerProfileDiagnostics compute_fluid_25d_tracer_profile_diagnostics(
    const Fluid25DConfig& config, std::span<const float> depth_m,
    std::span<const float> tracer_q_m, std::span<const float> sink_depth_rate_m_per_s,
    std::span<const Fluid25DTracerLedgerGpu> cumulative_tracer_ledger,
    double initial_tracer_amount_m3) {
    validate_fluid_25d_config(config);
    const std::size_t cells = fluid_25d_cell_count(config);
    if (depth_m.size() != cells || tracer_q_m.size() != cells ||
        sink_depth_rate_m_per_s.size() != cells || cumulative_tracer_ledger.size() != cells) {
        throw std::runtime_error("fluid 2.5D tracer diagnostic fields have invalid dimensions");
    }
    if (!std::isfinite(initial_tracer_amount_m3) || initial_tracer_amount_m3 < 0.0) {
        throw std::runtime_error("fluid 2.5D diagnostic initial tracer amount is invalid");
    }

    constexpr float kTracerRoundingResidueM = 1.0e-7F;
    const double cell_area_m2 =
        static_cast<double>(config.cell_size_m) * static_cast<double>(config.cell_size_m);
    Fluid25DTracerProfileDiagnostics result;
    double wet_concentration_sum = 0.0;
    double concentration_weight = 0.0;
    double weighted_x = 0.0;
    double weighted_y = 0.0;
    bool has_dye = false;
    for (std::size_t index = 0U; index < cells; ++index) {
        const float depth = depth_m[index];
        const float tracer_q = tracer_q_m[index];
        const float sink_rate = sink_depth_rate_m_per_s[index];
        if (!std::isfinite(depth) || depth < 0.0F || !std::isfinite(tracer_q) ||
            tracer_q < -kTracerRoundingResidueM ||
            tracer_q > depth + kTracerRoundingResidueM || !std::isfinite(sink_rate) ||
            sink_rate < 0.0F) {
            throw std::runtime_error("fluid 2.5D tracer diagnostic state is invalid at cell=" +
                                     std::to_string(index));
        }
        const double clamped_q = std::clamp(static_cast<double>(tracer_q), 0.0,
                                            static_cast<double>(depth));
        const double amount_m3 = clamped_q * cell_area_m2;
        result.total_tracer_amount_m3 += amount_m3;
        if (sink_rate > 0.0F) {
            result.tracer_in_explicit_sink_region_m3 += amount_m3;
        }
        const double x = static_cast<double>(index % config.grid_width);
        const double y = static_cast<double>(index / config.grid_width);
        weighted_x += amount_m3 * x;
        weighted_y += amount_m3 * y;
        if (depth <= config.minimum_wet_depth_m || clamped_q == 0.0) {
            continue;
        }
        const double concentration = clamped_q / static_cast<double>(depth);
        if (!std::isfinite(concentration) || concentration < 0.0 || concentration > 1.0) {
            throw std::runtime_error("fluid 2.5D tracer diagnostic concentration is invalid");
        }
        result.maximum_concentration = std::max(result.maximum_concentration, concentration);
        wet_concentration_sum += concentration;
        concentration_weight += 1.0;
        if (concentration < static_cast<double>(kFluid25DTracerMaterialConcentration)) {
            continue;
        }
        ++result.dyed_wet_cell_count;
        result.downstream_extent_cell_x =
            has_dye ? std::max(result.downstream_extent_cell_x, x) : x;
        has_dye = true;
    }
    result.dyed_wet_cell_ratio = static_cast<double>(result.dyed_wet_cell_count) /
                                  static_cast<double>(cells);
    result.mean_concentration =
        concentration_weight == 0.0 ? 0.0 : wet_concentration_sum / concentration_weight;
    if (result.total_tracer_amount_m3 > 0.0) {
        result.amount_weighted_centroid_cell_x = weighted_x / result.total_tracer_amount_m3;
        result.amount_weighted_centroid_cell_y = weighted_y / result.total_tracer_amount_m3;
    }

    for (std::size_t index = 0U; index < cells; ++index) {
        const Fluid25DTracerLedgerGpu& ledger = cumulative_tracer_ledger[index];
        for (std::size_t component = 0U;
             component < ledger.source_sink_boundary_reserved_m3.size(); ++component) {
            const float value = ledger.source_sink_boundary_reserved_m3[component];
            if (!std::isfinite(value) || value < 0.0F) {
                throw std::runtime_error("fluid 2.5D tracer diagnostic ledger is invalid at cell=" +
                                         std::to_string(index) + " component=" +
                                         std::to_string(component));
            }
        }
        if (ledger.source_sink_boundary_reserved_m3[3] != 0.0F) {
            throw std::runtime_error("fluid 2.5D tracer diagnostic ledger padding is nonzero");
        }
        result.cumulative_source_amount_m3 +=
            static_cast<double>(ledger.source_sink_boundary_reserved_m3[0]);
        result.cumulative_sink_amount_m3 +=
            static_cast<double>(ledger.source_sink_boundary_reserved_m3[1]);
        result.cumulative_boundary_outflow_amount_m3 +=
            static_cast<double>(ledger.source_sink_boundary_reserved_m3[2]);
    }
    result.conservation_residual_m3 = result.total_tracer_amount_m3 - initial_tracer_amount_m3 -
                                      result.cumulative_source_amount_m3 +
                                      result.cumulative_sink_amount_m3 +
                                      result.cumulative_boundary_outflow_amount_m3;
    if (!std::isfinite(result.total_tracer_amount_m3) ||
        !std::isfinite(result.conservation_residual_m3)) {
        throw std::runtime_error("fluid 2.5D tracer diagnostic totals are nonfinite");
    }
    return result;
}

namespace {

void record_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                   std::string_view name, double value) {
    recorder.record_metric(frame_index, "fluid_25d.water", name, value);
}

void record_tracer_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                          std::string_view name, double value) {
    recorder.record_metric(frame_index, "fluid_25d.tracer", name, value);
}

} // namespace

void record_fluid_25d_profile_diagnostics(cubey::profiling::ProfileRecorder& recorder,
                                          std::uint64_t frame_index,
                                          const Fluid25DProfileDiagnostics& diagnostics) {
    record_metric(recorder, frame_index, "wet_cell_count",
                  static_cast<double>(diagnostics.wet_cell_count));
    record_metric(recorder, frame_index, "wet_cell_ratio", diagnostics.wet_cell_ratio);
    record_metric(recorder, frame_index, "total_water_volume_m3",
                  diagnostics.total_water_volume_m3);
    record_metric(recorder, frame_index, "maximum_depth_m", diagnostics.maximum_depth_m);
    record_metric(recorder, frame_index, "wet_mean_depth_m", diagnostics.wet_mean_depth_m);
    record_metric(recorder, frame_index, "active_flow_cell_count",
                  static_cast<double>(diagnostics.active_flow_cell_count));
    record_metric(recorder, frame_index, "active_flow_cell_ratio",
                  diagnostics.active_flow_cell_ratio);
    record_metric(recorder, frame_index, "maximum_speed_m_per_s",
                  diagnostics.maximum_speed_m_per_s);
    record_metric(recorder, frame_index, "active_mean_speed_m_per_s",
                  diagnostics.active_mean_speed_m_per_s);
    record_metric(recorder, frame_index, "slow_pooled_wet_fraction",
                  diagnostics.slow_pooled_wet_fraction);
    record_metric(recorder, frame_index, "cumulative_source_volume_m3",
                  diagnostics.cumulative_source_volume_m3);
    record_metric(recorder, frame_index, "cumulative_sink_volume_m3",
                  diagnostics.cumulative_sink_volume_m3);
    record_metric(recorder, frame_index, "cumulative_boundary_outflow_volume_m3",
                  diagnostics.cumulative_boundary_outflow_volume_m3);
    record_metric(recorder, frame_index, "conservation_residual_m3",
                  diagnostics.conservation_residual_m3);
}

std::array<Fluid25DSourceOutletCrossSectionStation, kFluid25DSourceOutletCrossSectionStationCount>
fluid_25d_source_outlet_cross_section_stations(std::uint32_t grid_width,
                                               std::uint32_t grid_height) {
    const Fluid25DSourceOutletGeometry geometry(grid_width, grid_height);
    const auto station_at_progress = [&geometry](float progress) {
        const float span = static_cast<float>(geometry.sink_x - geometry.source_x);
        const float x = static_cast<float>(geometry.source_x) + (span * progress);
        return static_cast<std::uint32_t>(
            std::clamp(std::lround(x), 0L, static_cast<long>(geometry.width - 1U)));
    };
    const auto make_station = [&geometry](std::string_view name, std::uint32_t x_cell) {
        return Fluid25DSourceOutletCrossSectionStation{
            .name = name,
            .x_cell = x_cell,
            .endpoint_pool_affected = geometry.endpoint_pool_affected(x_cell),
        };
    };
    return {{make_station("near_source", geometry.source_x),
             make_station("upstream_reach", station_at_progress(0.25F)),
             make_station("constriction", station_at_progress(0.58F)),
             make_station("downstream_reach", station_at_progress(0.80F)),
             make_station("near_outlet", geometry.sink_x)}};
}

Fluid25DSourceOutletCrossSectionDiagnostics
compute_fluid_25d_source_outlet_cross_section_diagnostics(
    const Fluid25DConfig& config, const Fluid25DSourceOutletCrossSectionStation& station,
    std::span<const float> terrain_height_m, std::span<const float> depth_m,
    std::span<const Fluid25DVelocityGpu> velocity) {
    validate_fluid_25d_config(config);
    if (config.scenario != Fluid25DScenario::SourceOutletDemo) {
        throw std::runtime_error("fluid 2.5D river cross sections require source-outlet-demo");
    }
    const std::size_t cells = fluid_25d_cell_count(config);
    if (terrain_height_m.size() != cells || depth_m.size() != cells || velocity.size() != cells) {
        throw std::runtime_error("fluid 2.5D river cross-section fields have invalid dimensions");
    }
    if (station.name.empty() || station.x_cell >= config.grid_width) {
        throw std::runtime_error("fluid 2.5D river cross-section station is invalid");
    }

    const Fluid25DSourceOutletGeometry geometry(config.grid_width, config.grid_height);
    const std::size_t width = static_cast<std::size_t>(config.grid_width);
    const Fluid25DSourceOutletBankCrestSample left_crest =
        fluid_25d_source_outlet_bank_crest(geometry, station.x_cell, terrain_height_m, true);
    const Fluid25DSourceOutletBankCrestSample right_crest =
        fluid_25d_source_outlet_bank_crest(geometry, station.x_cell, terrain_height_m, false);
    const std::uint32_t first_bank_y = std::min(left_crest.y_cell, right_crest.y_cell);
    const std::uint32_t last_bank_y = std::max(left_crest.y_cell, right_crest.y_cell);
    const std::uint32_t centerline_y = static_cast<std::uint32_t>(
        std::clamp(std::lround(geometry.channel_center_y(static_cast<float>(station.x_cell))), 0L,
                   static_cast<long>(config.grid_height - 1U)));
    const std::size_t centerline_index =
        static_cast<std::size_t>(centerline_y) * width + station.x_cell;
    const double cell_width_m = static_cast<double>(config.cell_size_m);
    const double bank_to_bank_width_m =
        static_cast<double>(last_bank_y - first_bank_y + 1U) * cell_width_m;
    const double lower_bank_crest_elevation_m = std::min(
        static_cast<double>(left_crest.elevation_m), static_cast<double>(right_crest.elevation_m));

    Fluid25DSourceOutletCrossSectionDiagnostics result;
    result.station_name = station.name;
    result.station_x_cell = station.x_cell;
    result.left_bank_crest_y_cell = left_crest.y_cell;
    result.right_bank_crest_y_cell = right_crest.y_cell;
    result.left_bank_crest_fallback = left_crest.fallback ? 1U : 0U;
    result.right_bank_crest_fallback = right_crest.fallback ? 1U : 0U;
    result.endpoint_pool_affected = station.endpoint_pool_affected;
    result.station_x_m = (static_cast<double>(station.x_cell) + 0.5) * cell_width_m;
    result.left_bank_crest_elevation_m = left_crest.elevation_m;
    result.right_bank_crest_elevation_m = right_crest.elevation_m;
    result.lower_bank_crest_elevation_m = lower_bank_crest_elevation_m;
    result.centerline_bed_elevation_m = terrain_height_m[centerline_index];
    result.bank_to_bank_width_m = bank_to_bank_width_m;

    double surface_area_weighted_sum_m3 = 0.0;
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        const std::size_t index = static_cast<std::size_t>(y) * width + station.x_cell;
        const float bed = terrain_height_m[index];
        const float depth = depth_m[index];
        const Fluid25DVelocityGpu state = velocity[index];
        if (!std::isfinite(bed) || !std::isfinite(depth) || depth < 0.0F ||
            !std::isfinite(state.velocity_wet[0]) || !std::isfinite(state.velocity_wet[1]) ||
            !std::isfinite(state.velocity_wet[2]) || !std::isfinite(state.velocity_wet[3])) {
            throw std::runtime_error("fluid 2.5D river cross-section readback is invalid");
        }
        const bool in_bank_span = y >= first_bank_y && y <= last_bank_y;
        if (!in_bank_span) {
            if (depth > config.minimum_wet_depth_m) {
                ++result.overbank_wet_cell_count;
            }
            continue;
        }
        if (depth > config.minimum_wet_depth_m) {
            ++result.wetted_cell_count;
            const double depth_value_m = static_cast<double>(depth);
            const double area_contribution_m2 = depth_value_m * cell_width_m;
            result.section_water_area_m2 += area_contribution_m2;
            result.wetted_cell_width_m += cell_width_m;
            result.depth_velocity_discharge_estimate_m3_per_s +=
                depth_value_m * static_cast<double>(state.velocity_wet[0]) * cell_width_m;
            surface_area_weighted_sum_m3 +=
                (static_cast<double>(bed) + depth_value_m) * area_contribution_m2;
        }
        result.bankfull_capacity_area_m2 +=
            std::max(0.0, lower_bank_crest_elevation_m - static_cast<double>(bed)) * cell_width_m;
    }

    result.wetted_width_fraction = result.bank_to_bank_width_m > 0.0
                                       ? result.wetted_cell_width_m / result.bank_to_bank_width_m
                                       : 0.0;
    result.mean_section_depth_m = result.bank_to_bank_width_m > 0.0
                                      ? result.section_water_area_m2 / result.bank_to_bank_width_m
                                      : 0.0;
    if (result.wetted_cell_width_m > 0.0) {
        result.mean_wet_depth_m = result.section_water_area_m2 / result.wetted_cell_width_m;
    }
    if (result.section_water_area_m2 > 0.0) {
        result.representative_free_surface_elevation_m =
            surface_area_weighted_sum_m3 / result.section_water_area_m2;
        result.representative_free_surface_valid = 1.0;
        result.mean_x_velocity_m_per_s =
            result.depth_velocity_discharge_estimate_m3_per_s / result.section_water_area_m2;
        result.froude_estimate =
            std::abs(result.mean_x_velocity_m_per_s) /
            std::sqrt(static_cast<double>(config.gravity_m_per_s2) * result.mean_wet_depth_m);
    }
    if (result.bankfull_capacity_area_m2 > 0.0) {
        result.bankfull_capacity_valid = 1.0;
        result.bankfull_fraction = result.section_water_area_m2 / result.bankfull_capacity_area_m2;
    }
    return result;
}

void record_fluid_25d_tracer_profile_diagnostics(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const Fluid25DTracerProfileDiagnostics& diagnostics) {
    record_tracer_metric(recorder, frame_index, "total_tracer_amount_m3",
                         diagnostics.total_tracer_amount_m3);
    record_tracer_metric(recorder, frame_index, "maximum_concentration",
                         diagnostics.maximum_concentration);
    record_tracer_metric(recorder, frame_index, "mean_concentration",
                         diagnostics.mean_concentration);
    record_tracer_metric(recorder, frame_index, "dyed_wet_cell_count",
                         static_cast<double>(diagnostics.dyed_wet_cell_count));
    record_tracer_metric(recorder, frame_index, "dyed_wet_cell_ratio",
                         diagnostics.dyed_wet_cell_ratio);
    record_tracer_metric(recorder, frame_index, "amount_weighted_centroid_cell_x",
                         diagnostics.amount_weighted_centroid_cell_x);
    record_tracer_metric(recorder, frame_index, "amount_weighted_centroid_cell_y",
                         diagnostics.amount_weighted_centroid_cell_y);
    record_tracer_metric(recorder, frame_index, "downstream_extent_cell_x",
                         diagnostics.downstream_extent_cell_x);
    record_tracer_metric(recorder, frame_index, "tracer_in_explicit_sink_region_m3",
                         diagnostics.tracer_in_explicit_sink_region_m3);
    record_tracer_metric(recorder, frame_index, "cumulative_source_amount_m3",
                         diagnostics.cumulative_source_amount_m3);
    record_tracer_metric(recorder, frame_index, "cumulative_sink_amount_m3",
                         diagnostics.cumulative_sink_amount_m3);
    record_tracer_metric(recorder, frame_index, "cumulative_boundary_outflow_amount_m3",
                         diagnostics.cumulative_boundary_outflow_amount_m3);
    record_tracer_metric(recorder, frame_index, "conservation_residual_m3",
                         diagnostics.conservation_residual_m3);
}

void record_fluid_25d_source_outlet_cross_section_diagnostics(
    cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
    const Fluid25DSourceOutletCrossSectionDiagnostics& diagnostics) {
    const std::string_view station = diagnostics.station_name;
    record_cross_section_metric(recorder, frame_index, station, "station_x_cell",
                                static_cast<double>(diagnostics.station_x_cell));
    record_cross_section_metric(recorder, frame_index, station, "station_x_m",
                                diagnostics.station_x_m);
    record_cross_section_metric(recorder, frame_index, station, "endpoint_pool_affected",
                                diagnostics.endpoint_pool_affected ? 1.0 : 0.0);
    record_cross_section_metric(recorder, frame_index, station, "left_bank_crest_y_cell",
                                static_cast<double>(diagnostics.left_bank_crest_y_cell));
    record_cross_section_metric(recorder, frame_index, station, "right_bank_crest_y_cell",
                                static_cast<double>(diagnostics.right_bank_crest_y_cell));
    record_cross_section_metric(recorder, frame_index, station, "left_bank_crest_fallback",
                                static_cast<double>(diagnostics.left_bank_crest_fallback));
    record_cross_section_metric(recorder, frame_index, station, "right_bank_crest_fallback",
                                static_cast<double>(diagnostics.right_bank_crest_fallback));
    record_cross_section_metric(recorder, frame_index, station, "left_bank_crest_elevation_m",
                                diagnostics.left_bank_crest_elevation_m);
    record_cross_section_metric(recorder, frame_index, station, "right_bank_crest_elevation_m",
                                diagnostics.right_bank_crest_elevation_m);
    record_cross_section_metric(recorder, frame_index, station, "lower_bank_crest_elevation_m",
                                diagnostics.lower_bank_crest_elevation_m);
    record_cross_section_metric(recorder, frame_index, station, "centerline_bed_elevation_m",
                                diagnostics.centerline_bed_elevation_m);
    record_cross_section_metric(recorder, frame_index, station, "bank_to_bank_width_m",
                                diagnostics.bank_to_bank_width_m);
    record_cross_section_metric(recorder, frame_index, station, "wetted_cell_count",
                                static_cast<double>(diagnostics.wetted_cell_count));
    record_cross_section_metric(recorder, frame_index, station, "wetted_cell_width_m",
                                diagnostics.wetted_cell_width_m);
    record_cross_section_metric(recorder, frame_index, station, "wetted_width_fraction",
                                diagnostics.wetted_width_fraction);
    record_cross_section_metric(recorder, frame_index, station,
                                "representative_free_surface_elevation_m",
                                diagnostics.representative_free_surface_elevation_m);
    record_cross_section_metric(recorder, frame_index, station, "representative_free_surface_valid",
                                diagnostics.representative_free_surface_valid);
    record_cross_section_metric(recorder, frame_index, station, "section_water_area_m2",
                                diagnostics.section_water_area_m2);
    record_cross_section_metric(recorder, frame_index, station, "mean_wet_depth_m",
                                diagnostics.mean_wet_depth_m);
    record_cross_section_metric(recorder, frame_index, station, "mean_section_depth_m",
                                diagnostics.mean_section_depth_m);
    record_cross_section_metric(recorder, frame_index, station, "bankfull_capacity_area_m2",
                                diagnostics.bankfull_capacity_area_m2);
    record_cross_section_metric(recorder, frame_index, station, "bankfull_fraction",
                                diagnostics.bankfull_fraction);
    record_cross_section_metric(recorder, frame_index, station, "bankfull_capacity_valid",
                                diagnostics.bankfull_capacity_valid);
    record_cross_section_metric(recorder, frame_index, station, "overbank_wet_cell_count",
                                static_cast<double>(diagnostics.overbank_wet_cell_count));
    record_cross_section_metric(recorder, frame_index, station,
                                "depth_velocity_discharge_estimate_m3_per_s",
                                diagnostics.depth_velocity_discharge_estimate_m3_per_s);
    record_cross_section_metric(recorder, frame_index, station, "mean_x_velocity_m_per_s",
                                diagnostics.mean_x_velocity_m_per_s);
    record_cross_section_metric(recorder, frame_index, station, "froude_estimate",
                                diagnostics.froude_estimate);
}

} // namespace cubey::projects::fluid::fluid_25d
