#include "fluid_25d_diagnostics.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::fluid::fluid_25d {
namespace {

[[nodiscard]] std::string diagnostic_float(float value) {
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return std::move(stream).str();
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

} // namespace cubey::projects::fluid::fluid_25d
