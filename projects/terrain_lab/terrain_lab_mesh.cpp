#include "terrain_lab_mesh.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace cubey::projects::terrain_lab {
namespace {

[[nodiscard]] std::uint32_t offset_of(std::size_t offset) {
    return static_cast<std::uint32_t>(offset);
}

[[nodiscard]] cubey::math::Vec3 terrain_lab_normal_at(const TerrainLabFieldData& fields,
                                                      std::uint32_t x, std::uint32_t y) {
    const std::uint32_t left = x == 0U ? x : x - 1U;
    const std::uint32_t right = std::min(x + 1U, fields.desc.width - 1U);
    const std::uint32_t down = y == 0U ? y : y - 1U;
    const std::uint32_t up = std::min(y + 1U, fields.desc.height - 1U);
    const float dx = static_cast<float>(right - left) * fields.desc.cell_size_m;
    const float dz = static_cast<float>(up - down) * fields.desc.cell_size_m;
    const float dhdx =
        (fields.height_m[fields.index(right, y)] - fields.height_m[fields.index(left, y)]) /
        std::max(dx, fields.desc.cell_size_m);
    const float dhdz =
        (fields.height_m[fields.index(x, up)] - fields.height_m[fields.index(x, down)]) /
        std::max(dz, fields.desc.cell_size_m);
    return glm::normalize(cubey::math::Vec3{-dhdx, 1.0F, -dhdz});
}

} // namespace

cubey::render::MeshConfig TerrainLabMeshData::mesh_config() const {
    return cubey::render::indexed_mesh_config(
        std::span<const TerrainLabVertex>(vertices.data(), vertices.size()),
        std::span<const std::uint32_t>(indices.data(), indices.size()));
}

cubey::render::VertexInputLayout terrain_lab_vertex_input_layout() {
    return {
        .vertex_bindings = {cubey::render::vertex_input_binding(
            0, static_cast<std::uint32_t>(sizeof(TerrainLabVertex)),
            VK_VERTEX_INPUT_RATE_VERTEX)},
        .attributes =
            {
                cubey::render::vertex_input_attribute(
                    0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, position))),
                cubey::render::vertex_input_attribute(
                    1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, normal))),
                cubey::render::vertex_input_attribute(
                    2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, terrain))),
                cubey::render::vertex_input_attribute(
                    3, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, contributions))),
                cubey::render::vertex_input_attribute(
                    4, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, hydrology))),
                cubey::render::vertex_input_attribute(
                    5, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, material_a))),
                cubey::render::vertex_input_attribute(
                    6, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, material_b))),
                cubey::render::vertex_input_attribute(
                    7, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, vegetation))),
                cubey::render::vertex_input_attribute(
                    8, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, influences))),
            },
    };
}

TerrainLabMeshData make_terrain_lab_mesh(const TerrainLabFieldData& fields) {
    validate_terrain_lab_fields(fields);
    if (fields.desc.width < 2U || fields.desc.height < 2U) {
        throw std::runtime_error("terrain lab mesh requires at least a 2x2 field");
    }
    if (fields.sample_count() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("terrain lab mesh exceeds uint32 vertex range");
    }

    const float height_range = std::max(fields.max_height_m - fields.min_height_m, 0.001F);
    TerrainLabMeshData mesh;
    mesh.vertices.reserve(fields.sample_count());
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float world_z = terrain_lab_grid_sample_z_m(fields.desc, y);
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const float world_x = terrain_lab_grid_sample_x_m(fields.desc, x);
            const std::size_t sample = fields.index(x, y);
            const TerrainLabMaterialMask mask = fields.material_masks[sample];
            const float height_t =
                (fields.height_m[sample] - fields.min_height_m) / height_range;
            mesh.vertices.push_back({
                .position = {world_x, fields.height_m[sample], world_z},
                .normal = terrain_lab_normal_at(fields, x, y),
                .terrain =
                    {
                        fields.height_m[sample],
                        fields.slope[sample],
                        fields.curvature[sample],
                        height_t,
                    },
                .contributions =
                    {
                        fields.structure_height_m[sample],
                        fields.process_delta_m[sample],
                        fields.detail_height_m[sample],
                        fields.structure_height_m[sample] + fields.process_delta_m[sample],
                    },
                .hydrology =
                    {
                        static_cast<float>(fields.flow_direction[sample]),
                        fields.flow_accumulation[sample],
                        fields.stream_power[sample],
                        fields.wetness[sample],
                    },
                .material_a = {mask.rock, mask.soil, mask.scree, mask.meadow},
                .material_b = {mask.forest, mask.snow, fields.deposition[sample], 0.0F},
                .vegetation =
                    {
                        fields.grass_density[sample],
                        fields.shrub_density[sample],
                        fields.tree_density[sample],
                        fields.canopy_height_m[sample],
                    },
                .influences =
                    {
                        fields.ridge_influence[sample],
                        fields.valley_influence[sample],
                        fields.basin_influence[sample],
                        0.0F,
                    },
            });
        }
    }

    const std::size_t quad_count =
        static_cast<std::size_t>(fields.desc.width - 1U) *
        static_cast<std::size_t>(fields.desc.height - 1U);
    if (quad_count > std::numeric_limits<std::uint32_t>::max() / 6U) {
        throw std::runtime_error("terrain lab mesh exceeds uint32 index range");
    }
    mesh.indices.reserve(quad_count * 6U);
    for (std::uint32_t y = 0; y + 1U < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x + 1U < fields.desc.width; ++x) {
            const std::uint32_t i00 = static_cast<std::uint32_t>(fields.index(x, y));
            const std::uint32_t i10 = static_cast<std::uint32_t>(fields.index(x + 1U, y));
            const std::uint32_t i01 = static_cast<std::uint32_t>(fields.index(x, y + 1U));
            const std::uint32_t i11 = static_cast<std::uint32_t>(fields.index(x + 1U, y + 1U));
            mesh.indices.insert(mesh.indices.end(), {i00, i10, i01, i10, i11, i01});
        }
    }
    return mesh;
}

std::uint32_t terrain_lab_triangle_count(const TerrainLabMeshData& mesh) {
    return static_cast<std::uint32_t>(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::terrain_lab
