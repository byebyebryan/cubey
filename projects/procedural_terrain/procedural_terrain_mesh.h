#pragma once

#include "procedural_terrain_fields.h"

#include <cubey/core/math.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::procedural_terrain {

struct TerrainVertex {
    cubey::math::Vec3 position{};
    cubey::math::Vec3 normal{0.0F, 1.0F, 0.0F};
    cubey::math::Vec4 material{};
    cubey::math::Vec4 fields{};
};

struct TerrainMeshData {
    std::vector<TerrainVertex> vertices{};
    std::vector<std::uint32_t> indices{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
};

[[nodiscard]] cubey::render::VertexInputLayout terrain_vertex_input_layout();
[[nodiscard]] TerrainMeshData make_terrain_mesh(const TerrainFieldData& fields);
[[nodiscard]] TerrainMeshData make_water_surface_mesh(const TerrainFieldData& fields);
[[nodiscard]] std::uint32_t terrain_triangle_count(const TerrainMeshData& mesh);

} // namespace cubey::projects::procedural_terrain
