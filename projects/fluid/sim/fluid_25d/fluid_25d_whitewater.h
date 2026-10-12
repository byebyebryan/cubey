#pragma once
#include "fluid_25d_solver_state.h"
#include <cubey/render/terrain_surface_patch_whitewater.h>
namespace cubey::projects::fluid::fluid_25d {
using Fluid25DWhitewaterSeed = render::TerrainSurfacePatchWhitewaterSeed;
using Fluid25DWhitewaterSelection = render::TerrainSurfacePatchWhitewaterSelection;
[[nodiscard]] inline Fluid25DWhitewaterSelection
fluid_25d_whitewater_seeds(std::uint32_t width, std::uint32_t height, float cell_m,
                           std::span<const float> bed, std::span<const float> depth,
                           std::span<const Fluid25DVelocity> velocity, std::uint32_t budget) {
    return render::terrain_surface_patch_whitewater_seeds(width, height, cell_m, bed, depth,
                                                          velocity, budget);
}
} // namespace cubey::projects::fluid::fluid_25d
