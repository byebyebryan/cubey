#pragma once

#include "terrain_product.h"
#include "terrain_preview_config.h"

#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainPreviewMeshData {
    std::vector<cubey::render::VertexPositionColorNormal> vertices{};
    std::vector<std::uint32_t> indices{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
};

[[nodiscard]] TerrainPreviewMeshData make_terrain_preview_mesh(
    const TerrainRegionProduct& product, const TerrainPreviewConfig& config);
[[nodiscard]] std::uint32_t terrain_preview_triangle_count(const TerrainPreviewMeshData& mesh);

} // namespace cubey::projects::terrain
