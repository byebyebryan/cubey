#pragma once

#include "terrain_config.h"

#include <cubey/render/clipmap_grid_2d.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainClipmapMeshData {
    std::vector<cubey::render::VertexPositionColorNormal> vertices{};
    std::vector<std::uint32_t> indices{};
    cubey::render::ClipmapGrid2DDiagnostics diagnostics{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
};

struct TerrainQualityTileDiagnostics {
    std::uint32_t patches_per_axis = 0U;
    std::uint32_t patch_count = 0U;
    float patch_span_m = 0.0F;
    float half_extent_m = 0.0F;
};

struct TerrainQualityTileMeshData {
    std::vector<cubey::render::VertexPositionColorNormal> vertices{};
    std::vector<std::uint32_t> indices{};
    TerrainQualityTileDiagnostics diagnostics{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
    [[nodiscard]] std::uint32_t patch_count() const;
};

[[nodiscard]] cubey::render::ClipmapGrid2DConfig
terrain_clipmap_config(const TerrainRuntimeConfig& config);
[[nodiscard]] TerrainClipmapMeshData make_terrain_clipmap_mesh(const TerrainRuntimeConfig& config);
[[nodiscard]] TerrainQualityTileMeshData
make_terrain_quality_tile_mesh(const TerrainRuntimeConfig& config);
[[nodiscard]] std::uint32_t terrain_clipmap_triangle_count(const TerrainClipmapMeshData& mesh);

} // namespace cubey::projects::terrain
