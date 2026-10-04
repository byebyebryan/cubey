#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_gpu_resources.h"
#include "fluid_25d_scenarios.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr float kFluid25DRainStudyMaterialDepthM = 0.01F;
inline constexpr float kFluid25DRainStudyActiveSpeedMPerS = 0.02F;
inline constexpr float kFluid25DRainStudyEdgeBandM = 120.0F;
inline constexpr std::uint32_t kFluid25DRainStudyRegionSpanCells = 21U;

struct Fluid25DRainStudyForcingState {
    bool enabled = true;
    std::optional<bool> queued_enabled{};
    double cumulative_depth_m = 0.0;
    double scheduled_volume_m3 = 0.0;
    std::uint64_t completed_steps = 0U;
    float last_applied_rate_m_per_s = 0.0F;
    bool has_applied_step = false;
};

// Rain is scheduled against public fixed steps. Queued UI changes are applied
// by prepare_fixed_step(), so pausing never consumes a pending On/Off change.
class Fluid25DRainStudyControl {
  public:
    void queue_enabled(bool enabled) noexcept {
        state_.queued_enabled = enabled;
    }

    [[nodiscard]] const std::optional<bool>& queued_enabled() const noexcept {
        return state_.queued_enabled;
    }

    [[nodiscard]] bool enabled() const noexcept {
        return state_.enabled;
    }

    [[nodiscard]] const Fluid25DRainStudyForcingState& state() const noexcept {
        return state_;
    }

    [[nodiscard]] float prepare_fixed_step(const Fluid25DConfig& config) {
        validate_rain_study_config(config);
        if (state_.completed_steps == std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("fluid 2.5D rain-study fixed-step clock overflowed");
        }

        const bool enabled = state_.queued_enabled.value_or(state_.enabled);
        const float applied_rate = enabled ? config.rainfall_depth_rate_m_per_s : 0.0F;
        const double rate = static_cast<double>(applied_rate);
        const double dt = static_cast<double>(config.fixed_delta_seconds);
        const double depth_increment = rate * dt;
        const double area = static_cast<double>(config.cell_size_m) * config.cell_size_m;
        const double volume_increment =
            depth_increment * area * static_cast<double>(fluid_25d_cell_count(config));
        const double next_depth = state_.cumulative_depth_m + depth_increment;
        const double next_volume = state_.scheduled_volume_m3 + volume_increment;
        if (!std::isfinite(next_depth) || !std::isfinite(next_volume)) {
            throw std::runtime_error("fluid 2.5D rain-study schedule overflowed");
        }

        state_.enabled = enabled;
        state_.queued_enabled.reset();
        state_.last_applied_rate_m_per_s = applied_rate;
        state_.has_applied_step = true;
        state_.cumulative_depth_m = next_depth;
        state_.scheduled_volume_m3 = next_volume;
        ++state_.completed_steps;
        return enabled ? 1.0F : 0.0F;
    }

    [[nodiscard]] float applied_rate_m_per_s(const Fluid25DConfig& config) const {
        validate_rain_study_config(config);
        if (state_.has_applied_step) {
            return state_.last_applied_rate_m_per_s;
        }
        return state_.enabled ? config.rainfall_depth_rate_m_per_s : 0.0F;
    }

    [[nodiscard]] double applied_rate_mm_per_hour(const Fluid25DConfig& config) const {
        return static_cast<double>(applied_rate_m_per_s(config)) * 3'600'000.0;
    }

    [[nodiscard]] double total_input_m3_per_s(const Fluid25DConfig& config) const {
        const double rate = static_cast<double>(applied_rate_m_per_s(config));
        const double area = static_cast<double>(config.cell_size_m) * config.cell_size_m;
        const double total = rate * area * static_cast<double>(fluid_25d_cell_count(config));
        if (!std::isfinite(total)) {
            throw std::runtime_error("fluid 2.5D rain-study total input rate is not representable");
        }
        return total;
    }

    void reset() noexcept {
        state_ = {};
        state_.enabled = true;
    }

  private:
    static void validate_rain_study_config(const Fluid25DConfig& config) {
        validate_fluid_25d_config(config);
        if (config.scenario != Fluid25DScenario::HillsideRainStudy ||
            config.solver != Fluid25DSolver::FiniteVolume ||
            !std::isfinite(config.rainfall_depth_rate_m_per_s) ||
            !(config.rainfall_depth_rate_m_per_s > 0.0F) ||
            !std::isfinite(config.fixed_delta_seconds) || config.fixed_delta_seconds <= 0.0F ||
            !std::isfinite(config.cell_size_m) || config.cell_size_m <= 0.0F) {
            throw std::runtime_error("fluid 2.5D rain-study control requires valid rain config");
        }
    }

    Fluid25DRainStudyForcingState state_{};
};

struct Fluid25DRainStudyRegionObservation {
    std::string_view name{};
    bool valid = false;
    double water_volume_m3 = 0.0;
    double direct_rain_volume_m3 = 0.0;
    double net_lateral_storage_m3 = 0.0;
    double maximum_depth_m = 0.0;
    std::uint64_t material_wet_cells = 0U;
    std::uint64_t active_cells = 0U;
};

struct Fluid25DRainStudyObservation {
    double physical_time_s = 0.0;
    double rate_mm_per_hour = 0.0;
    double total_input_m3_per_s = 0.0;
    double cumulative_depth_m = 0.0;
    double scheduled_volume_m3 = 0.0;
    bool enabled = false;
    std::uint64_t material_wet_cells = 0U;
    std::uint64_t material_active_cells = 0U;
    double converged_water_volume_m3 = 0.0;
    std::uint64_t converged_moving_cells = 0U;
    std::uint64_t largest_corridor_cells = 0U;
    double largest_corridor_span_m = 0.0;
    double maximum_depth_m = 0.0;
    std::uint32_t maximum_depth_cell_x = 0U;
    std::uint32_t maximum_depth_cell_z = 0U;
    std::array<Fluid25DRainStudyRegionObservation, 3U> regions{};
};

[[nodiscard]] inline Fluid25DRainStudyObservation compute_fluid_25d_rain_study_observation(
    const Fluid25DConfig& config, std::span<const float> depth_m,
    std::span<const Fluid25DVelocityGpu> velocity,
    const Fluid25DRainStudyControl& forcing, double physical_time_s) {
    validate_fluid_25d_config(config);
    if (config.scenario != Fluid25DScenario::HillsideRainStudy ||
        config.solver != Fluid25DSolver::FiniteVolume || !std::isfinite(physical_time_s) ||
        physical_time_s < 0.0) {
        throw std::runtime_error("fluid 2.5D rain-study observation has invalid configuration");
    }
    const std::size_t cell_count = fluid_25d_cell_count(config);
    if (depth_m.size() != cell_count || velocity.size() != cell_count) {
        throw std::runtime_error("fluid 2.5D rain-study observation fields do not match the grid");
    }

    const auto& state = forcing.state();
    const double area = static_cast<double>(config.cell_size_m) * config.cell_size_m;
    if (!std::isfinite(state.cumulative_depth_m) || state.cumulative_depth_m < 0.0 ||
        !std::isfinite(state.scheduled_volume_m3) || state.scheduled_volume_m3 < 0.0) {
        throw std::runtime_error("fluid 2.5D rain-study schedule observations are invalid");
    }
    Fluid25DRainStudyObservation result{
        .physical_time_s = physical_time_s,
        .rate_mm_per_hour = forcing.applied_rate_mm_per_hour(config),
        .total_input_m3_per_s = forcing.total_input_m3_per_s(config),
        .cumulative_depth_m = state.cumulative_depth_m,
        .scheduled_volume_m3 = state.scheduled_volume_m3,
        .enabled = state.enabled,
    };

    const double converged_threshold =
        state.cumulative_depth_m + static_cast<double>(kFluid25DRainStudyMaterialDepthM);
    std::vector<std::uint8_t> corridor_candidate(cell_count, 0U);
    for (std::uint32_t z = 0U; z < config.grid_height; ++z) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const std::size_t index = fluid_25d_scenario_index(config.grid_width, config.grid_height,
                                                               x, z);
            const float h = depth_m[index];
            const auto& velocity_cell = velocity[index].velocity_wet;
            if (!std::isfinite(h) || h < 0.0F || !std::isfinite(velocity_cell[0]) ||
                !std::isfinite(velocity_cell[1])) {
                throw std::runtime_error("fluid 2.5D rain-study observation field is invalid");
            }
            const double speed = std::hypot(static_cast<double>(velocity_cell[0]),
                                            static_cast<double>(velocity_cell[1]));
            if (static_cast<double>(h) >= kFluid25DRainStudyMaterialDepthM) {
                ++result.material_wet_cells;
                if (speed >= kFluid25DRainStudyActiveSpeedMPerS) {
                    ++result.material_active_cells;
                }
            }
            result.converged_water_volume_m3 +=
                std::max(static_cast<double>(h) - state.cumulative_depth_m, 0.0) * area;
            if (h > result.maximum_depth_m) {
                result.maximum_depth_m = h;
                result.maximum_depth_cell_x = x;
                result.maximum_depth_cell_z = z;
            }
            if (static_cast<double>(h) >= converged_threshold &&
                speed >= kFluid25DRainStudyActiveSpeedMPerS) {
                ++result.converged_moving_cells;
                const std::uint32_t edge_band_cells = static_cast<std::uint32_t>(
                    std::ceil(static_cast<double>(kFluid25DRainStudyEdgeBandM) /
                              config.cell_size_m));
                if (x >= edge_band_cells && z >= edge_band_cells &&
                    x + edge_band_cells < config.grid_width &&
                    z + edge_band_cells < config.grid_height) {
                    corridor_candidate[index] = 1U;
                }
            }
        }
    }

    std::vector<std::uint8_t> visited(cell_count, 0U);
    constexpr std::array<std::array<int, 2>, 4U> neighbors{{
        {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
    }};
    for (std::uint32_t z = 0U; z < config.grid_height; ++z) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const std::size_t start = fluid_25d_scenario_index(config.grid_width, config.grid_height,
                                                               x, z);
            if (corridor_candidate[start] == 0U || visited[start] != 0U) {
                continue;
            }
            std::queue<std::size_t> pending;
            pending.push(start);
            visited[start] = 1U;
            std::uint32_t minimum_x = x;
            std::uint32_t maximum_x = x;
            std::uint32_t minimum_z = z;
            std::uint32_t maximum_z = z;
            std::uint64_t component_cells = 0U;
            while (!pending.empty()) {
                const std::size_t index = pending.front();
                pending.pop();
                const std::uint32_t cell_x = static_cast<std::uint32_t>(index % config.grid_width);
                const std::uint32_t cell_z = static_cast<std::uint32_t>(index / config.grid_width);
                ++component_cells;
                minimum_x = std::min(minimum_x, cell_x);
                maximum_x = std::max(maximum_x, cell_x);
                minimum_z = std::min(minimum_z, cell_z);
                maximum_z = std::max(maximum_z, cell_z);
                for (const auto& offset : neighbors) {
                    const int next_x = static_cast<int>(cell_x) + offset[0];
                    const int next_z = static_cast<int>(cell_z) + offset[1];
                    if (next_x < 0 || next_z < 0 || next_x >= static_cast<int>(config.grid_width) ||
                        next_z >= static_cast<int>(config.grid_height)) {
                        continue;
                    }
                    const std::size_t next = fluid_25d_scenario_index(
                        config.grid_width, config.grid_height, static_cast<std::uint32_t>(next_x),
                        static_cast<std::uint32_t>(next_z));
                    if (corridor_candidate[next] != 0U && visited[next] == 0U) {
                        visited[next] = 1U;
                        pending.push(next);
                    }
                }
            }
            const std::uint64_t cell_span = std::max(maximum_x - minimum_x, maximum_z - minimum_z);
            const double span_m = static_cast<double>(cell_span) * config.cell_size_m;
            if (span_m > result.largest_corridor_span_m ||
                (span_m == result.largest_corridor_span_m &&
                 component_cells > result.largest_corridor_cells)) {
                result.largest_corridor_span_m = span_m;
                result.largest_corridor_cells = component_cells;
            }
        }
    }

    struct RegionOrigin {
        std::string_view name;
        std::uint32_t x;
        std::uint32_t z;
    };
    constexpr std::array<RegionOrigin, 3U> origins{{
        {"upper", 203U, 203U}, {"transit", 128U, 224U}, {"collection", 48U, 241U},
    }};
    constexpr std::uint64_t region_cells =
        static_cast<std::uint64_t>(kFluid25DRainStudyRegionSpanCells) *
        kFluid25DRainStudyRegionSpanCells;
    for (std::size_t region_index = 0U; region_index < origins.size(); ++region_index) {
        const RegionOrigin& origin = origins[region_index];
        Fluid25DRainStudyRegionObservation& region = result.regions[region_index];
        region.name = origin.name;
        region.valid = origin.x + kFluid25DRainStudyRegionSpanCells <= config.grid_width &&
                       origin.z + kFluid25DRainStudyRegionSpanCells <= config.grid_height;
        if (!region.valid) {
            continue;
        }
        for (std::uint32_t z = origin.z;
             z < origin.z + kFluid25DRainStudyRegionSpanCells; ++z) {
            for (std::uint32_t x = origin.x;
                 x < origin.x + kFluid25DRainStudyRegionSpanCells; ++x) {
                const std::size_t index = fluid_25d_scenario_index(
                    config.grid_width, config.grid_height, x, z);
                const double h = depth_m[index];
                const auto& velocity_cell = velocity[index].velocity_wet;
                const double speed = std::hypot(static_cast<double>(velocity_cell[0]),
                                                static_cast<double>(velocity_cell[1]));
                region.water_volume_m3 += h * area;
                region.maximum_depth_m = std::max(region.maximum_depth_m, h);
                if (h >= kFluid25DRainStudyMaterialDepthM) {
                    ++region.material_wet_cells;
                    if (speed >= kFluid25DRainStudyActiveSpeedMPerS) {
                        ++region.active_cells;
                    }
                }
            }
        }
        region.direct_rain_volume_m3 = state.cumulative_depth_m * area * region_cells;
        region.net_lateral_storage_m3 =
            region.water_volume_m3 - region.direct_rain_volume_m3;
    }
    return result;
}

} // namespace cubey::projects::fluid::fluid_25d
