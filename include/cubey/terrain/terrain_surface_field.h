#pragma once

#include <cubey/asset/terrain_raster_climate_source.h>
#include <cubey/terrain/terrain_surface_model.h>
#include <vector>

namespace cubey::terrain {

inline constexpr std::string_view kTerrainSurfaceFieldFormula = "terrain-correlated-surface-v1";

// Immutable render-only field in ORIGINAL source XZ/metres. Never normalized to
// a consumer crop or derived from its draw mesh. Climate is long-term potential,
// not rain, saturation, water depth, ecology or reconstructed geology.
// RGBA = exposed rock, sheltered-ground potential, moisture potential, snow.
// Source bounds alone truncate the 90/360/1440 m square-box neighbourhoods.
// A full-source product supplies the halo for all downstream crops.
struct TerrainSurfaceField {
    asset::TerrainHeightSourceBounds bounds{};
    std::uint32_t width = 0, height = 0;
    std::vector<math::Vec4> weights{};

    [[nodiscard]] math::Vec4 sample(math::Vec2 source_xz) const;
};

// Deterministic staged bake, capped at 1024^2 samples (16 MiB retained CPU data).
// Resolution belongs to the source product, never a camera/consumer crop.
[[nodiscard]] TerrainSurfaceField make_terrain_surface_field(
    const asset::TerrainHeightSource& source, const asset::TerrainHeightSourceBounds& source_bounds,
    const asset::TerrainRasterClimateSource* climate = nullptr, std::uint32_t extent = 1024);

} // namespace cubey::terrain
