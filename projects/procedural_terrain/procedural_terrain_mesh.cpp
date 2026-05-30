#include "procedural_terrain_mesh.h"

#include <glm/geometric.hpp>

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace cubey::projects::procedural_terrain {
namespace {

constexpr float kWaterSurfaceVisualOffsetM = 0.32F;

[[nodiscard]] std::uint32_t offset_of(std::size_t offset) {
    return static_cast<std::uint32_t>(offset);
}

} // namespace

cubey::render::MeshConfig TerrainMeshData::mesh_config() const {
    return cubey::render::indexed_mesh_config(
        std::span<const TerrainVertex>(vertices.data(), vertices.size()),
        std::span<const std::uint32_t>(indices.data(), indices.size()));
}

cubey::render::VertexInputLayout terrain_vertex_input_layout() {
    return {
        .vertex_bindings = {cubey::render::vertex_input_binding(
            0, static_cast<std::uint32_t>(sizeof(TerrainVertex)), VK_VERTEX_INPUT_RATE_VERTEX)},
        .attributes =
            {
                cubey::render::vertex_input_attribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                                      offset_of(offsetof(TerrainVertex, position))),
                cubey::render::vertex_input_attribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                                      offset_of(offsetof(TerrainVertex, normal))),
                cubey::render::vertex_input_attribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                                      offset_of(offsetof(TerrainVertex, material))),
                cubey::render::vertex_input_attribute(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                                      offset_of(offsetof(TerrainVertex, fields))),
            },
    };
}

TerrainMeshData make_terrain_mesh(const TerrainFieldData& fields) {
    if (fields.desc.width < 2U || fields.desc.height < 2U) {
        throw std::runtime_error("terrain mesh requires at least a 2x2 field");
    }
    if (fields.sample_count() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("terrain mesh exceeds uint32 vertex range");
    }

    TerrainMeshData mesh;
    mesh.vertices.reserve(fields.sample_count());
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float z =
            (static_cast<float>(y) - (static_cast<float>(fields.desc.height - 1U) * 0.5F)) *
            fields.desc.cell_size_m;
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const float world_x =
                (static_cast<float>(x) - (static_cast<float>(fields.desc.width - 1U) * 0.5F)) *
                fields.desc.cell_size_m;
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
            const std::size_t sample = fields.index(x, y);
            const TerrainMaterialMask mask = fields.material_masks[sample];
            mesh.vertices.push_back({
                .position = {world_x, fields.height_m[sample], z},
                .normal = glm::normalize(cubey::math::Vec3{-dhdx, 1.0F, -dhdz}),
                .material = {mask.sand, mask.rock, mask.vegetation, mask.sediment},
                .fields =
                    {
                        fields.height_m[sample],
                        fields.water_depth_m[sample],
                        fields.shore_sdf_m[sample],
                        fields.slope[sample],
                    },
            });
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(fields.desc.width - 1U) *
                         static_cast<std::size_t>(fields.desc.height - 1U) * 6U);
    for (std::uint32_t y = 0; y < fields.desc.height - 1U; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width - 1U; ++x) {
            const std::uint32_t a = static_cast<std::uint32_t>(fields.index(x, y));
            const std::uint32_t b = static_cast<std::uint32_t>(fields.index(x + 1U, y));
            const std::uint32_t c = static_cast<std::uint32_t>(fields.index(x + 1U, y + 1U));
            const std::uint32_t d = static_cast<std::uint32_t>(fields.index(x, y + 1U));
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }
    return mesh;
}

TerrainMeshData make_water_surface_mesh(const TerrainFieldData& fields) {
    if (fields.desc.width < 2U || fields.desc.height < 2U) {
        throw std::runtime_error("water mesh requires at least a 2x2 field");
    }

    const std::uint32_t mesh_width = fields.desc.width + 2U;
    const std::uint32_t mesh_height = fields.desc.height + 2U;
    if (static_cast<std::uint64_t>(mesh_width) * static_cast<std::uint64_t>(mesh_height) >
        std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("water mesh exceeds uint32 vertex range");
    }

    const float terrain_width_m = static_cast<float>(fields.desc.width - 1U) *
                                  fields.desc.cell_size_m;
    const float terrain_height_m = static_cast<float>(fields.desc.height - 1U) *
                                   fields.desc.cell_size_m;
    const float min_x = -terrain_width_m * 0.5F;
    const float min_z = -terrain_height_m * 0.5F;
    const float padding_m = std::max(terrain_width_m, terrain_height_m) * 6.0F;
    const float deep_water_m = std::max(fields.max_water_depth_m, 1.0F);
    const float far_shore_sdf_m = -std::max(fields.max_abs_shore_sdf_m + padding_m, padding_m);

    TerrainMeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(mesh_width) * mesh_height);
    for (std::uint32_t y = 0; y < mesh_height; ++y) {
        for (std::uint32_t x = 0; x < mesh_width; ++x) {
            const bool border =
                x == 0U || y == 0U || x == mesh_width - 1U || y == mesh_height - 1U;
            const float world_x = x == 0U
                                      ? min_x - padding_m
                                  : x == mesh_width - 1U
                                      ? min_x + terrain_width_m + padding_m
                                      : min_x + (static_cast<float>(x - 1U) *
                                                 fields.desc.cell_size_m);
            const float world_z = y == 0U
                                      ? min_z - padding_m
                                  : y == mesh_height - 1U
                                      ? min_z + terrain_height_m + padding_m
                                      : min_z + (static_cast<float>(y - 1U) *
                                                 fields.desc.cell_size_m);

            float water_depth = deep_water_m;
            float shore_sdf = far_shore_sdf_m;
            if (!border) {
                const std::size_t sample = fields.index(x - 1U, y - 1U);
                water_depth = fields.water_depth_m[sample];
                shore_sdf = fields.shore_sdf_m[sample];
            }

            mesh.vertices.push_back({
                .position = {world_x, fields.desc.sea_level_m + kWaterSurfaceVisualOffsetM,
                             world_z},
                .normal = {0.0F, 1.0F, 0.0F},
                .material = {0.0F, 0.0F, 0.0F, 1.0F},
                .fields =
                    {
                        fields.desc.sea_level_m,
                        water_depth,
                        shore_sdf,
                        -1.0F,
                    },
            });
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(mesh_width - 1U) *
                         static_cast<std::size_t>(mesh_height - 1U) * 6U);
    for (std::uint32_t y = 0; y < mesh_height - 1U; ++y) {
        for (std::uint32_t x = 0; x < mesh_width - 1U; ++x) {
            const std::uint32_t a = (y * mesh_width) + x;
            const std::uint32_t b = (y * mesh_width) + x + 1U;
            const std::uint32_t c = ((y + 1U) * mesh_width) + x + 1U;
            const std::uint32_t d = ((y + 1U) * mesh_width) + x;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }
    return mesh;
}

std::uint32_t terrain_triangle_count(const TerrainMeshData& mesh) {
    return static_cast<std::uint32_t>(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::procedural_terrain
