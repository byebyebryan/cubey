#include "terrain_lab_mesh.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::projects::terrain_lab {
namespace {

[[nodiscard]] std::uint32_t offset_of(std::size_t offset) {
    return static_cast<std::uint32_t>(offset);
}

[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::uint64_t seed) {
    std::uint64_t value = seed;
    value ^= static_cast<std::uint32_t>(x) + 0x9e37'79b9U + (value << 6U) + (value >> 2U);
    value ^= static_cast<std::uint32_t>(y) + 0x85eb'ca6bU + (value << 6U) + (value >> 2U);
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] float random01(std::uint64_t seed, std::uint32_t index, std::uint32_t channel) {
    constexpr float kScale = 1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(hash_u32(static_cast<std::int32_t>(index),
                                       static_cast<std::int32_t>(channel), seed)) *
           kScale;
}

[[nodiscard]] float lerp(float a, float b, float t) {
    return a + ((b - a) * t);
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

[[nodiscard]] TerrainLabVertex terrain_lab_proxy_vertex(cubey::math::Vec3 position,
                                                        cubey::math::Vec3 normal,
                                                        cubey::math::Vec4 material_a,
                                                        cubey::math::Vec4 material_b,
                                                        cubey::math::Vec4 vegetation) {
    return {
        .position = position,
        .normal = glm::normalize(normal),
        .terrain = {position.y, 0.0F, 0.0F, 0.5F},
        .contributions = {0.0F, 0.0F, 0.0F, position.y},
        .hydrology = {8.0F, 0.0F, 0.0F, 0.0F},
        .material_a = material_a,
        .material_b = material_b,
        .vegetation = vegetation,
        .influences = {0.0F, 0.0F, 0.0F, 0.0F},
        .feature_tags = {0.0F, 0.0F, -1.0F, -1.0F},
        .drivers = {0.0F, 0.0F, 0.0F, 0.0F},
        .river = {0.0F, 0.0F, 0.0F, 0.0F},
    };
}

[[nodiscard]] std::uint32_t append_vertex(TerrainLabMeshData& mesh, TerrainLabVertex vertex) {
    if (mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("terrain lab mesh exceeds uint32 vertex range");
    }
    mesh.vertices.push_back(vertex);
    return static_cast<std::uint32_t>(mesh.vertices.size() - 1U);
}

void append_triangle(TerrainLabMeshData& mesh, TerrainLabVertex a, TerrainLabVertex b,
                     TerrainLabVertex c) {
    const std::uint32_t ia = append_vertex(mesh, a);
    const std::uint32_t ib = append_vertex(mesh, b);
    const std::uint32_t ic = append_vertex(mesh, c);
    mesh.indices.insert(mesh.indices.end(), {ia, ib, ic});
}

void append_shrub_proxy(TerrainLabMeshData& mesh, cubey::math::Vec3 center, float radius,
                        float height) {
    const cubey::math::Vec3 top = center + cubey::math::Vec3{0.0F, height, 0.0F};
    const cubey::math::Vec4 material_a{0.0F, 0.05F, 0.0F, 0.82F};
    const cubey::math::Vec4 material_b{0.13F, 0.0F, 0.0F, 0.0F};
    const cubey::math::Vec4 vegetation{0.22F, 1.0F, 0.0F, height};
    const cubey::math::Vec3 normal_a{0.0F, 0.72F, 0.28F};
    const cubey::math::Vec3 normal_b{0.28F, 0.72F, 0.0F};
    append_triangle(mesh,
                    terrain_lab_proxy_vertex(center + cubey::math::Vec3{-radius, 0.0F, 0.0F},
                                             normal_a, material_a, material_b, vegetation),
                    terrain_lab_proxy_vertex(center + cubey::math::Vec3{radius, 0.0F, 0.0F},
                                             normal_a, material_a, material_b, vegetation),
                    terrain_lab_proxy_vertex(top, normal_a, material_a, material_b, vegetation));
    append_triangle(mesh,
                    terrain_lab_proxy_vertex(center + cubey::math::Vec3{0.0F, 0.0F, -radius},
                                             normal_b, material_a, material_b, vegetation),
                    terrain_lab_proxy_vertex(center + cubey::math::Vec3{0.0F, 0.0F, radius},
                                             normal_b, material_a, material_b, vegetation),
                    terrain_lab_proxy_vertex(top, normal_b, material_a, material_b, vegetation));
}

void append_rock_proxy(TerrainLabMeshData& mesh, cubey::math::Vec3 center, float radius,
                       float height) {
    const cubey::math::Vec3 apex = center + cubey::math::Vec3{0.0F, height, 0.0F};
    const cubey::math::Vec4 material_a{0.48F, 0.04F, 0.48F, 0.0F};
    const cubey::math::Vec4 material_b{0.0F, 0.0F, 0.0F, 0.0F};
    const cubey::math::Vec4 vegetation{0.0F, 0.0F, 0.0F, 0.0F};
    const cubey::math::Vec3 north = center + cubey::math::Vec3{0.0F, 0.0F, -radius};
    const cubey::math::Vec3 east = center + cubey::math::Vec3{radius, 0.0F, 0.0F};
    const cubey::math::Vec3 south = center + cubey::math::Vec3{0.0F, 0.0F, radius};
    const cubey::math::Vec3 west = center + cubey::math::Vec3{-radius, 0.0F, 0.0F};
    append_triangle(
        mesh,
        terrain_lab_proxy_vertex(north, {-0.3F, 0.82F, -0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(east, {0.3F, 0.82F, -0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(apex, {0.0F, 1.0F, 0.0F}, material_a, material_b, vegetation));
    append_triangle(
        mesh,
        terrain_lab_proxy_vertex(east, {0.3F, 0.82F, 0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(south, {0.3F, 0.82F, 0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(apex, {0.0F, 1.0F, 0.0F}, material_a, material_b, vegetation));
    append_triangle(
        mesh,
        terrain_lab_proxy_vertex(south, {-0.3F, 0.82F, 0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(west, {-0.3F, 0.82F, 0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(apex, {0.0F, 1.0F, 0.0F}, material_a, material_b, vegetation));
    append_triangle(
        mesh,
        terrain_lab_proxy_vertex(west, {-0.3F, 0.82F, -0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(north, {-0.3F, 0.82F, -0.3F}, material_a, material_b, vegetation),
        terrain_lab_proxy_vertex(apex, {0.0F, 1.0F, 0.0F}, material_a, material_b, vegetation));
}

void append_proxy_dressing(TerrainLabMeshData& mesh, const TerrainLabFieldData& fields) {
    const std::size_t vertex_begin = mesh.vertices.size();
    const std::size_t index_begin = mesh.indices.size();
    const std::uint32_t step = std::max(4U, std::min(fields.desc.width, fields.desc.height) / 42U);
    const std::uint32_t start = std::max(1U, step / 2U);
    for (std::uint32_t y = start; y + start < fields.desc.height; y += step) {
        for (std::uint32_t x = start; x + start < fields.desc.width; x += step) {
            const std::size_t sample = fields.index(x, y);
            const TerrainLabMaterialMask mask = fields.material_masks[sample];
            const auto sample_u32 = static_cast<std::uint32_t>(sample);
            const cubey::math::Vec3 center{
                terrain_lab_grid_sample_x_m(fields.desc, x),
                fields.height_m[sample] + 0.8F,
                terrain_lab_grid_sample_z_m(fields.desc, y),
            };
            const float shrub_score = fields.shrub_density[sample] * (1.0F - fields.slope[sample]) *
                                      (1.0F - fields.channel_influence[sample] * 0.45F);
            if (shrub_score > 0.010F && random01(fields.desc.seed + 9091U, sample_u32, 0U) <
                                            std::min(shrub_score * 3.8F, 0.65F)) {
                const float shape = random01(fields.desc.seed + 9091U, sample_u32, 1U);
                append_shrub_proxy(mesh, center,
                                   fields.desc.cell_size_m * lerp(0.28F, 0.60F, shape),
                                   fields.desc.cell_size_m * lerp(0.55F, 1.15F, shape));
            }

            const float rock_score =
                (mask.scree * 0.72F + mask.rock * 0.18F + fields.ridge_influence[sample] * 0.24F) *
                (1.0F - fields.channel_influence[sample] * 0.58F);
            if (rock_score > 0.28F && random01(fields.desc.seed + 9091U, sample_u32, 2U) <
                                          std::min(rock_score * 0.52F, 0.50F)) {
                const float shape = random01(fields.desc.seed + 9091U, sample_u32, 3U);
                append_rock_proxy(mesh, center, fields.desc.cell_size_m * lerp(0.20F, 0.44F, shape),
                                  fields.desc.cell_size_m * lerp(0.16F, 0.42F, shape));
            }
        }
    }
    mesh.proxy_vertex_count = mesh.vertices.size() - vertex_begin;
    mesh.proxy_index_count = mesh.indices.size() - index_begin;
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
            0, static_cast<std::uint32_t>(sizeof(TerrainLabVertex)), VK_VERTEX_INPUT_RATE_VERTEX)},
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
                cubey::render::vertex_input_attribute(
                    9, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, feature_tags))),
                cubey::render::vertex_input_attribute(
                    10, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, drivers))),
                cubey::render::vertex_input_attribute(
                    11, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainLabVertex, river))),
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
    const float drainage_region_denominator = fields.drainage_region_count <= 1U
                                                  ? 1.0F
                                                  : static_cast<float>(
                                                        fields.drainage_region_count - 1U);
    const float channel_distance_range = std::max(fields.max_channel_distance_m, 0.001F);
    const float stream_order_denominator =
        fields.max_stream_order <= 1U ? 1.0F : static_cast<float>(fields.max_stream_order);
    TerrainLabMeshData mesh;
    mesh.vertices.reserve(fields.sample_count());
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float world_z = terrain_lab_grid_sample_z_m(fields.desc, y);
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const float world_x = terrain_lab_grid_sample_x_m(fields.desc, x);
            const std::size_t sample = fields.index(x, y);
            const TerrainLabMaterialMask mask = fields.material_masks[sample];
            const float height_t = (fields.height_m[sample] - fields.min_height_m) / height_range;
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
                .material_b = {mask.forest, mask.snow, fields.deposition[sample], mask.sand},
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
                        fields.basin_influence[sample],
                        fields.divide_influence[sample],
                        fields.channel_influence[sample],
                    },
                .feature_tags =
                    {
                        static_cast<float>(fields.drainage_region_id[sample]) /
                            drainage_region_denominator,
                        fields.channel_distance_m[sample] / channel_distance_range,
                        static_cast<float>(fields.drainage_region_id[sample]),
                        static_cast<float>(fields.drainage_region_count),
                    },
                .drivers =
                    {
                        fields.driver_base_potential[sample],
                        fields.driver_relief_potential[sample],
                        fields.driver_process_potential[sample],
                        fields.driver_selection_mask[sample],
                    },
                .river =
                    {
                        fields.river_discharge[sample],
                        static_cast<float>(fields.stream_order[sample]) / stream_order_denominator,
                        fields.river_width_m[sample],
                        fields.water_presence[sample],
                    },
            });
        }
    }
    mesh.terrain_vertex_count = mesh.vertices.size();

    const std::size_t quad_count = static_cast<std::size_t>(fields.desc.width - 1U) *
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
    mesh.terrain_index_count = mesh.indices.size();
    append_proxy_dressing(mesh, fields);
    return mesh;
}

std::uint32_t terrain_lab_triangle_count(const TerrainLabMeshData& mesh) {
    return static_cast<std::uint32_t>(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::terrain_lab
