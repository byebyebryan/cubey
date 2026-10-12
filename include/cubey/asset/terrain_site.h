#pragma once
#include <array>
#include <cubey/asset/terrain_height_source.h>
#include <filesystem>
#include <string>

namespace cubey::asset {
// Shared prepared terrain coverage, deliberately independent of hydraulic state.
struct TerrainSite {
    std::string id, world_id;
    math::Vec2 center_xz_m{};
    std::array<float, 4> level_spans_m{};
    TerrainHeightSourceBounds world_bounds{}, backdrop_bounds{};
    std::filesystem::path heightfield, surface_fields;
};
// Validates admission, producer/coordinate binding and safe package references.
// Raster payload hashes are subsequently checked by TerrainRasterHeightSource.
[[nodiscard]] TerrainSite load_terrain_site(const std::filesystem::path& manifest);
} // namespace cubey::asset
