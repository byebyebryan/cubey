#pragma once

#include "terrain_ref_config.h"

#include <cubey/render/clipmap_grid_2d.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::terrain_ref {

struct TerrainRefMeshData {
    std::vector<cubey::render::VertexPositionColorNormal> vertices{};
    std::vector<std::uint32_t> indices{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
};

[[nodiscard]] cubey::render::ClipmapGrid2DConfig
terrain_ref_clipmap_config(const TerrainRefConfig& config);
[[nodiscard]] TerrainRefMeshData make_terrain_ref_mesh(const TerrainRefConfig& config);
[[nodiscard]] std::uint32_t terrain_ref_triangle_count(const TerrainRefMeshData& mesh);

} // namespace cubey::projects::terrain_ref
