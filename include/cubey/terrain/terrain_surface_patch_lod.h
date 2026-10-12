#pragma once
#include <cubey/asset/terrain_water_capture.h>
#include <vector>

namespace cubey::terrain {
// Fixed scene-centered paired topology, built once. Coordinates address the
// immutable capture; both surfaces evaluate the same B-spline at these points.
struct TerrainSurfacePatchLod {
    std::vector<math::Vec2> coordinates;
    std::vector<std::uint32_t> indices;
    math::Vec2 fine_minimum_world{}, fine_maximum_world{};
};
[[nodiscard]] TerrainSurfacePatchLod
make_terrain_surface_patch_lod(const asset::TerrainWaterCaptureGrid& grid, math::Vec2 focus_world,
                               float fine_span_m = 16368.0F);
} // namespace cubey::terrain
