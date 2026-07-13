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

struct TerrainQualityClipmapMeshData {
    std::vector<cubey::render::VertexPositionColorNormal> vertices{};
    std::vector<std::uint32_t> indices{};
    cubey::render::ClipmapGrid2DDiagnostics diagnostics{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
    [[nodiscard]] std::uint32_t patch_count() const;
};

[[nodiscard]] cubey::render::ClipmapGrid2DConfig
terrain_clipmap_config(const TerrainRuntimeConfig& config);
[[nodiscard]] TerrainClipmapMeshData make_terrain_clipmap_mesh(const TerrainRuntimeConfig& config);
[[nodiscard]] TerrainQualityClipmapMeshData
make_terrain_quality_clipmap_mesh(const TerrainRuntimeConfig& config);
[[nodiscard]] std::uint32_t terrain_clipmap_triangle_count(const TerrainClipmapMeshData& mesh);

} // namespace cubey::projects::terrain
