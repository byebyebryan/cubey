#pragma once

#include "fluid_25d_scenarios.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::uint32_t kFluid25DForcingCubeVertexCount = 60U;

// Static presentation-only instances. The cube's side is one cell in render
// metres; only its center follows the (possibly exaggerated) terrain height.
// Neighbor data removes shared side overlaps, including on uneven terrain.
struct Fluid25DForcingCubeGpu {
    std::array<float, 4> center_kind{};  // world x, physical bed y, world z, sink=1/source=0
    std::array<float, 4> neighbor_bed{}; // left, right, down, up
    std::array<float, 4> neighbor_present{};
};
static_assert(sizeof(Fluid25DForcingCubeGpu) == 48U);

[[nodiscard]] inline bool fluid_25d_uses_forcing_cubes(Fluid25DScenario scenario) {
    return fluid_25d_is_source_outlet_demo(scenario) ||
           scenario == Fluid25DScenario::SustainedHeadwatersDemo ||
           scenario == Fluid25DScenario::NaturalFlowStudy ||
           scenario == Fluid25DScenario::HillsideFlowStudy;
}

[[nodiscard]] inline std::vector<Fluid25DForcingCubeGpu>
fluid_25d_forcing_cubes(const Fluid25DConfig& config, const Fluid25DScenarioData& scenario) {
    if (!fluid_25d_uses_forcing_cubes(config.scenario))
        return {};
    const auto count = fluid_25d_cell_count(config);
    if (scenario.width != config.grid_width || scenario.height != config.grid_height ||
        scenario.cell_size_m != config.cell_size_m || scenario.terrain_height_m.size() != count ||
        scenario.source_depth_rate_m_per_s.size() != count ||
        scenario.sink_depth_rate_m_per_s.size() != count)
        throw std::runtime_error("forcing cube fields do not match the grid");
    std::vector<Fluid25DForcingCubeGpu> cubes;
    constexpr std::array<std::array<int, 2>, 4> offsets{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
    for (std::uint32_t kind = 0U; kind < 2U; ++kind) {
        const auto& rates =
            kind == 0U ? scenario.source_depth_rate_m_per_s : scenario.sink_depth_rate_m_per_s;
        for (std::size_t i = 0U; i < count; ++i) {
            if (!std::isfinite(rates[i]) || rates[i] < 0.0F ||
                !std::isfinite(scenario.terrain_height_m[i]))
                throw std::runtime_error("invalid forcing cube field");
            if (rates[i] == 0.0F)
                continue;
            const int x = static_cast<int>(i % config.grid_width);
            const int z = static_cast<int>(i / config.grid_width);
            Fluid25DForcingCubeGpu cube{
                .center_kind =
                    {(static_cast<float>(x) - 0.5F * static_cast<float>(config.grid_width - 1U)) *
                         config.cell_size_m,
                     scenario.terrain_height_m[i],
                     (static_cast<float>(z) - 0.5F * static_cast<float>(config.grid_height - 1U)) *
                         config.cell_size_m,
                     static_cast<float>(kind)},
            };
            for (std::size_t face = 0U; face < offsets.size(); ++face) {
                const int nx = x + offsets[face][0], nz = z + offsets[face][1];
                if (nx < 0 || nz < 0 || nx >= static_cast<int>(config.grid_width) ||
                    nz >= static_cast<int>(config.grid_height))
                    continue;
                const auto neighbor =
                    static_cast<std::size_t>(nz) * config.grid_width + static_cast<std::size_t>(nx);
                if (rates[neighbor] > 0.0F) {
                    cube.neighbor_bed[face] = scenario.terrain_height_m[neighbor];
                    cube.neighbor_present[face] = 1.0F;
                }
            }
            cubes.push_back(cube);
        }
    }
    return cubes;
}

// CPU reference for the vertex shader's two exposed side intervals, measured
// relative to the cube center after terrain height scaling. Empty intervals
// become degenerate triangles rather than overlapping transparent faces.
[[nodiscard]] inline std::array<std::array<float, 2>, 2>
fluid_25d_forcing_cube_side_ranges(float side, float neighbor_offset, bool neighbor_present) {
    const float half = side * 0.5F;
    if (!neighbor_present)
        return {{{-half, half}, {half, half}}};
    return {{{-half, std::clamp(neighbor_offset - half, -half, half)},
             {std::clamp(neighbor_offset + half, -half, half), half}}};
}

} // namespace cubey::projects::fluid::fluid_25d
