#pragma once

#include "planet_config.h"
#include "planet_frame.h"

#include <cubey/render/clipmap_grid_2d.h>
#include <cubey/render/local_tangent_frame.h>

#include <cstdint>

namespace cubey::projects::planet {

inline constexpr float kPlanetLocalDetailTransitionCells = 16.0F;
inline constexpr float kPlanetLocalDetailMaxTransitionRatio = 0.35F;
inline constexpr std::uint32_t kPlanetMaxLocalDetailPatches =
    cubey::render::clipmap_grid_2d_patch_count(kPlanetMaxLocalDetailLodLevels);

using PlanetLocalDetailPatch = cubey::render::ClipmapGrid2DPatch;
using PlanetLocalDetailPatchList =
    cubey::render::ClipmapGrid2DPatchList<kPlanetMaxLocalDetailPatches>;

struct PlanetLocalDetailPlan {
    cubey::render::LocalTangentFrame local_frame{};
    cubey::render::ClipmapGrid2DConfig grid{};
    PlanetLocalDetailPatchList patches{};
    cubey::render::ClipmapGrid2DDiagnostics diagnostics{};
};

[[nodiscard]] inline cubey::render::ClipmapGrid2DConfig
planet_local_detail_clipmap_config(const PlanetConfig& config) {
    validate_planet_config(config);
    return {
        .lod_levels = config.local_detail_lod_levels,
        .cells_per_axis = config.local_detail_cells_per_axis,
        .outer_half_extent = config.local_detail_outer_half_extent_m,
        .transition_cells = kPlanetLocalDetailTransitionCells,
        .max_transition_ratio = kPlanetLocalDetailMaxTransitionRatio,
    };
}

[[nodiscard]] inline PlanetLocalDetailPlan plan_planet_local_detail(const PlanetConfig& config,
                                                                    const PlanetFrame& frame) {
    const cubey::render::ClipmapGrid2DConfig grid =
        planet_local_detail_clipmap_config(config);
    cubey::render::validate_local_tangent_frame(frame.local_frame);
    PlanetLocalDetailPatchList patches =
        cubey::render::clipmap_grid_2d_patches<kPlanetMaxLocalDetailPatches>(grid);
    return {
        .local_frame = frame.local_frame,
        .grid = grid,
        .patches = patches,
        .diagnostics = cubey::render::clipmap_grid_2d_diagnostics(grid, patches),
    };
}

} // namespace cubey::projects::planet
