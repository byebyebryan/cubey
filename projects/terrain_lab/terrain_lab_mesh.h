#pragma once

#include "terrain_lab_fields.h"

#include <cubey/core/math.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::terrain_lab {

struct TerrainLabVertex {
    cubey::math::Vec3 position{};
    cubey::math::Vec3 normal{0.0F, 1.0F, 0.0F};
    cubey::math::Vec4 terrain{};
    cubey::math::Vec4 contributions{};
    cubey::math::Vec4 hydrology{};
    cubey::math::Vec4 material_a{};
    cubey::math::Vec4 material_b{};
    cubey::math::Vec4 vegetation{};
    cubey::math::Vec4 influences{};
    cubey::math::Vec4 feature_tags{};
    cubey::math::Vec4 drivers{};
};

struct TerrainLabMeshData {
    std::vector<TerrainLabVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    std::size_t terrain_vertex_count = 0;
    std::size_t terrain_index_count = 0;
    std::size_t proxy_vertex_count = 0;
    std::size_t proxy_index_count = 0;

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
};

[[nodiscard]] cubey::render::VertexInputLayout terrain_lab_vertex_input_layout();
[[nodiscard]] TerrainLabMeshData make_terrain_lab_mesh(const TerrainLabFieldData& fields);
[[nodiscard]] std::uint32_t terrain_lab_triangle_count(const TerrainLabMeshData& mesh);

} // namespace cubey::projects::terrain_lab
