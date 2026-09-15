#pragma once

#include "fluid_25d_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::size_t kFluid25DNoCell = std::numeric_limits<std::size_t>::max();

// Scenario fields use one value per cell in row-major order.  Source and sink
// fields are depth rates (m/s), not volume rates; the oracle multiplies by
// cell area before recording the m3 ledger.
struct Fluid25DScenarioData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float cell_size_m = 0.0F;
    std::vector<float> terrain_height_m{};
    std::vector<float> initial_water_depth_m{};
    std::vector<float> source_depth_rate_m_per_s{};
    std::vector<float> sink_depth_rate_m_per_s{};
    std::size_t source_cell = kFluid25DNoCell;
    std::size_t sink_cell = kFluid25DNoCell;
};

[[nodiscard]] inline std::size_t fluid_25d_scenario_cell_count(std::uint32_t width,
                                                               std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("fluid 2.5D scenario dimensions must be positive");
    }
    const std::size_t width_value = static_cast<std::size_t>(width);
    const std::size_t height_value = static_cast<std::size_t>(height);
    if (width_value > std::numeric_limits<std::size_t>::max() / height_value) {
        throw std::runtime_error("fluid 2.5D scenario dimensions are too large");
    }
    return width_value * height_value;
}

[[nodiscard]] inline std::size_t fluid_25d_scenario_index(std::uint32_t width, std::uint32_t height,
                                                          std::uint32_t x, std::uint32_t y) {
    if (x >= width || y >= height) {
        throw std::runtime_error("fluid 2.5D scenario coordinate is out of bounds");
    }
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
           static_cast<std::size_t>(x);
}

[[nodiscard]] inline Fluid25DScenarioData make_fluid_25d_scenario(Fluid25DScenario scenario,
                                                                  std::uint32_t width,
                                                                  std::uint32_t height,
                                                                  float cell_size_m) {
    if (!(cell_size_m > 0.0F) || !std::isfinite(cell_size_m)) {
        throw std::runtime_error("fluid 2.5D scenario cell size must be finite and positive");
    }
    const std::size_t cell_count = fluid_25d_scenario_cell_count(width, height);
    Fluid25DScenarioData data;
    data.width = width;
    data.height = height;
    data.cell_size_m = cell_size_m;
    data.terrain_height_m.resize(cell_count);
    data.initial_water_depth_m.assign(cell_count, 0.0F);
    data.source_depth_rate_m_per_s.assign(cell_count, 0.0F);
    data.sink_depth_rate_m_per_s.assign(cell_count, 0.0F);

    switch (scenario) {
    case Fluid25DScenario::DryBed: {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float x_value = static_cast<float>(x);
                const float y_value = static_cast<float>(y);
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    0.20F + (0.035F * x_value) + (0.017F * y_value) + (0.001F * x_value * y_value);
            }
        }
        break;
    }
    case Fluid25DScenario::LakeAtRest: {
        float maximum_terrain_height_m = -std::numeric_limits<float>::infinity();
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                // Binary-friendly coefficients make eta = terrain + depth
                // exactly constant in the float oracle, so this fixture tests
                // the solver's lake-at-rest behavior rather than cancellation
                // noise from decimal literals.
                const float terrain_height_m = 0.5F + (0.125F * static_cast<float>(x)) +
                                               (0.0625F * static_cast<float>(y)) +
                                               (0.015625F * static_cast<float>((x + y) % 3U));
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    terrain_height_m;
                maximum_terrain_height_m = std::max(maximum_terrain_height_m, terrain_height_m);
            }
        }
        const float surface_height_m = maximum_terrain_height_m + 0.375F;
        for (std::size_t index = 0; index < cell_count; ++index) {
            data.initial_water_depth_m[index] = surface_height_m - data.terrain_height_m[index];
        }
        break;
    }
    case Fluid25DScenario::RiverCatchment: {
        if (width < 6 || height < 3) {
            throw std::runtime_error("fluid 2.5D river scenario requires at least a 6x3 grid");
        }
        const std::uint32_t source_x = 1;
        const std::uint32_t sink_x = width - 2U;
        const std::uint32_t channel_y = height / 2U;
        const float channel_center = 0.5F * static_cast<float>(height - 1U);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float x_value = static_cast<float>(x);
                const float y_offset = static_cast<float>(y) - channel_center;
                // The longitudinal fall creates the source-to-sink direction;
                // the quadratic cross-section keeps water in a shallow channel.
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    3.20F - (0.085F * x_value) + (0.085F * y_offset * y_offset);
            }
        }
        data.source_cell = fluid_25d_scenario_index(width, height, source_x, channel_y);
        data.sink_cell = fluid_25d_scenario_index(width, height, sink_x, channel_y);
        // Seed a thin, deterministic channel so the first CPU/GPU comparison
        // exercises the complete source-to-sink route without waiting for a
        // numerically vanishing wetting front to cross every dry cell.
        for (std::uint32_t x = source_x; x < sink_x; ++x) {
            data.initial_water_depth_m[fluid_25d_scenario_index(width, height, x, channel_y)] =
                0.05F;
        }
        data.source_depth_rate_m_per_s[data.source_cell] = 0.040F;
        data.sink_depth_rate_m_per_s[data.sink_cell] = 0.055F;
        break;
    }
    default:
        throw std::runtime_error("fluid 2.5D scenario value is invalid");
    }

    return data;
}

} // namespace cubey::projects::fluid::fluid_25d
