#pragma once

#include "terrain_patch.h"

#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainMeshData {
    std::vector<cubey::render::VertexPositionColorNormal> vertices{};
    std::vector<std::uint32_t> indices{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
};

[[nodiscard]] TerrainMeshData make_terrain_mesh(const TerrainPatchProduct& product,
                                                std::string_view debug_view,
                                                float vertical_scale = 1.0F);
[[nodiscard]] std::uint32_t terrain_mesh_triangle_count(const TerrainMeshData& mesh);

} // namespace cubey::projects::terrain
