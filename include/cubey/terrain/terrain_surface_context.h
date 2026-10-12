#pragma once
#include <cubey/asset/terrain_water_capture.h>
#include <cubey/terrain/terrain_surface_field.h>

namespace cubey::terrain {
// Render-only exterior ground. No water cells, simulation or source mutation.
struct TerrainSurfaceContextVertex {
    math::Vec4 position_wet_hint{}; // Local XYZ; exterior-only wet-ground weight, never depth.
    math::Vec4 normal{};
    math::Vec4 surface{};
};
struct TerrainSurfaceContextProduct {
    std::vector<TerrainSurfaceContextVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t inner_perimeter_vertices = 0;
    math::Vec3 minimum{}, maximum{};
    float collar_width_m = 128.0F;
};
// Same positive cubic basis as the retained GPU mesh, including clamped extension.
struct TerrainSurfaceContextSample {
    float bed_m = 0, depth_m = 0;
    math::Vec2 bed_gradient{};
};
[[nodiscard]] TerrainSurfaceContextSample
terrain_surface_context_core_sample(const asset::TerrainWaterCapture& capture,
                                    math::Vec2 grid_coordinate);
[[nodiscard]] TerrainSurfaceContextProduct
make_terrain_surface_context(const asset::TerrainWaterCapture& capture,
                             const asset::TerrainRasterHeightSource& source,
                             const TerrainSurfaceField& surface, float collar_width_m = 128.0F);
} // namespace cubey::terrain
