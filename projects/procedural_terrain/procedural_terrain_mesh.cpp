#include "procedural_terrain_mesh.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cubey::projects::procedural_terrain {
namespace {

constexpr float kWaterSurfaceVisualOffsetM = 0.04F;
constexpr std::uint32_t kShoreClipSubdivisions = 4U;

[[nodiscard]] std::uint32_t offset_of(std::size_t offset) {
    return static_cast<std::uint32_t>(offset);
}

[[nodiscard]] TerrainVertex interpolated_vertex(const TerrainVertex& a, const TerrainVertex& b,
                                                float t, float sea_level_m) {
    TerrainVertex vertex{
        .position = a.position + ((b.position - a.position) * t),
        .normal = {0.0F, 1.0F, 0.0F},
        .material = a.material + ((b.material - a.material) * t),
        .fields = a.fields + ((b.fields - a.fields) * t),
        .generator = a.generator + ((b.generator - a.generator) * t),
        .contributions_a = a.contributions_a + ((b.contributions_a - a.contributions_a) * t),
        .contributions_b = a.contributions_b + ((b.contributions_b - a.contributions_b) * t),
    };
    vertex.position.y = sea_level_m;
    vertex.material = {0.62F, 0.0F, 0.0F, 0.38F};
    vertex.fields.x = sea_level_m;
    vertex.fields.y = 0.0F;
    vertex.fields.z = 0.0F;
    return vertex;
}

[[nodiscard]] float land_clip_value(const TerrainVertex& vertex) {
    return vertex.fields.z;
}

[[nodiscard]] bool is_land_side(const TerrainVertex& vertex) {
    return land_clip_value(vertex) >= 0.0F;
}

[[nodiscard]] TerrainVertex visual_land_vertex(TerrainVertex vertex, float sea_level_m) {
    if (vertex.fields.z < 0.0F || vertex.fields.x < sea_level_m) {
        vertex.position.y = sea_level_m;
        vertex.normal = {0.0F, 1.0F, 0.0F};
        vertex.material = {0.62F, 0.0F, 0.0F, 0.38F};
        vertex.fields.x = sea_level_m;
        vertex.fields.y = 0.0F;
        vertex.fields.z = 0.0F;
    }
    return vertex;
}

[[nodiscard]] TerrainVertex bilinear_vertex(const TerrainVertex& v00, const TerrainVertex& v10,
                                            const TerrainVertex& v01, const TerrainVertex& v11,
                                            float tx, float ty) {
    const auto mix_vec = [tx, ty](auto a00, auto a10, auto a01, auto a11) {
        const auto bottom = a00 + ((a10 - a00) * tx);
        const auto top = a01 + ((a11 - a01) * tx);
        return bottom + ((top - bottom) * ty);
    };

    TerrainVertex vertex{
        .position = mix_vec(v00.position, v10.position, v01.position, v11.position),
        .normal = mix_vec(v00.normal, v10.normal, v01.normal, v11.normal),
        .material = mix_vec(v00.material, v10.material, v01.material, v11.material),
        .fields = mix_vec(v00.fields, v10.fields, v01.fields, v11.fields),
        .generator = mix_vec(v00.generator, v10.generator, v01.generator, v11.generator),
        .contributions_a = mix_vec(v00.contributions_a, v10.contributions_a, v01.contributions_a,
                                   v11.contributions_a),
        .contributions_b = mix_vec(v00.contributions_b, v10.contributions_b, v01.contributions_b,
                                   v11.contributions_b),
    };
    vertex.normal = glm::normalize(vertex.normal);
    return vertex;
}

[[nodiscard]] std::vector<TerrainVertex>
clip_triangle_to_land(const std::array<TerrainVertex, 3>& triangle, float sea_level_m) {
    std::vector<TerrainVertex> output;
    output.reserve(4U);

    for (std::size_t index = 0; index < triangle.size(); ++index) {
        const TerrainVertex& current = triangle[index];
        const TerrainVertex& next = triangle[(index + 1U) % triangle.size()];
        const bool current_inside = is_land_side(current);
        const bool next_inside = is_land_side(next);

        if (current_inside && next_inside) {
            output.push_back(visual_land_vertex(next, sea_level_m));
        } else if (current_inside && !next_inside) {
            const float current_value = land_clip_value(current);
            const float next_value = land_clip_value(next);
            const float denom = current_value - next_value;
            const float t = denom == 0.0F ? 0.0F : current_value / denom;
            output.push_back(interpolated_vertex(current, next, t, sea_level_m));
        } else if (!current_inside && next_inside) {
            const float current_value = land_clip_value(current);
            const float next_value = land_clip_value(next);
            const float denom = next_value - current_value;
            const float t = denom == 0.0F ? 0.0F : -current_value / denom;
            output.push_back(interpolated_vertex(current, next, t, sea_level_m));
            output.push_back(visual_land_vertex(next, sea_level_m));
        }
    }

    return output;
}

void append_triangle_fan(TerrainMeshData& mesh, const std::vector<TerrainVertex>& polygon) {
    if (polygon.size() < 3U) {
        return;
    }
    if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max() - polygon.size()) {
        throw std::runtime_error("clipped land mesh exceeds uint32 vertex range");
    }

    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), polygon.begin(), polygon.end());
    for (std::uint32_t index = 1; index + 1U < polygon.size(); ++index) {
        mesh.indices.insert(mesh.indices.end(), {base, base + index, base + index + 1U});
    }
}

void append_clipped_triangle(TerrainMeshData& mesh, const TerrainVertex& a, const TerrainVertex& b,
                             const TerrainVertex& c, float sea_level_m) {
    append_triangle_fan(mesh, clip_triangle_to_land({a, b, c}, sea_level_m));
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
                cubey::render::vertex_input_attribute(
                    4, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainVertex, generator))),
                cubey::render::vertex_input_attribute(
                    5, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainVertex, contributions_a))),
                cubey::render::vertex_input_attribute(
                    6, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                    offset_of(offsetof(TerrainVertex, contributions_b))),
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
            const TerrainHeightContributions contributions = fields.height_contributions[sample];
            const float land_debug =
                std::clamp(0.5F + (fields.land_potential[sample] * 1.8F), 0.0F, 1.0F);
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
                .generator =
                    {
                        land_debug,
                        fields.inland[sample],
                        std::clamp(fields.ridge_strength[sample], 0.0F, 1.0F),
                        std::clamp(fields.valley_strength[sample], 0.0F, 1.0F),
                    },
                .contributions_a =
                    {
                        contributions.coast_lift_m,
                        contributions.inland_lift_m,
                        contributions.broad_noise_m,
                        contributions.detail_noise_m,
                    },
                .contributions_b =
                    {
                        contributions.foothills_m,
                        contributions.ridge_m,
                        contributions.broken_ridge_m - contributions.valley_cut_m,
                        contributions.relax_delta_m,
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

TerrainMeshData make_clipped_land_mesh(const TerrainFieldData& fields,
                                       const TerrainMeshData& terrain_mesh) {
    if (terrain_mesh.vertices.size() != fields.sample_count()) {
        throw std::runtime_error("terrain mesh vertex count must match terrain field samples");
    }

    TerrainMeshData mesh;
    mesh.vertices.reserve(terrain_mesh.vertices.size() * 2U);
    mesh.indices.reserve(terrain_mesh.indices.size());

    const auto vertex_at = [&fields, &terrain_mesh](std::uint32_t x,
                                                    std::uint32_t y) -> const TerrainVertex& {
        return terrain_mesh.vertices[fields.index(x, y)];
    };

    for (std::uint32_t y = 0; y < fields.desc.height - 1U; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width - 1U; ++x) {
            const TerrainVertex& v00 = vertex_at(x, y);
            const TerrainVertex& v10 = vertex_at(x + 1U, y);
            const TerrainVertex& v01 = vertex_at(x, y + 1U);
            const TerrainVertex& v11 = vertex_at(x + 1U, y + 1U);
            const std::array<const TerrainVertex*, 4> corners{&v00, &v10, &v01, &v11};
            bool has_land = false;
            bool has_water = false;
            for (const TerrainVertex* vertex : corners) {
                has_land = has_land || is_land_side(*vertex);
                has_water = has_water || !is_land_side(*vertex);
            }
            if (!has_land) {
                continue;
            }

            if (!has_water) {
                append_clipped_triangle(mesh, v00, v10, v11, fields.desc.sea_level_m);
                append_clipped_triangle(mesh, v00, v11, v01, fields.desc.sea_level_m);
                continue;
            }

            for (std::uint32_t sy = 0; sy < kShoreClipSubdivisions; ++sy) {
                const float ty0 =
                    static_cast<float>(sy) / static_cast<float>(kShoreClipSubdivisions);
                const float ty1 =
                    static_cast<float>(sy + 1U) / static_cast<float>(kShoreClipSubdivisions);
                for (std::uint32_t sx = 0; sx < kShoreClipSubdivisions; ++sx) {
                    const float tx0 =
                        static_cast<float>(sx) / static_cast<float>(kShoreClipSubdivisions);
                    const float tx1 =
                        static_cast<float>(sx + 1U) / static_cast<float>(kShoreClipSubdivisions);
                    const TerrainVertex s00 = bilinear_vertex(v00, v10, v01, v11, tx0, ty0);
                    const TerrainVertex s10 = bilinear_vertex(v00, v10, v01, v11, tx1, ty0);
                    const TerrainVertex s01 = bilinear_vertex(v00, v10, v01, v11, tx0, ty1);
                    const TerrainVertex s11 = bilinear_vertex(v00, v10, v01, v11, tx1, ty1);
                    append_clipped_triangle(mesh, s00, s10, s11, fields.desc.sea_level_m);
                    append_clipped_triangle(mesh, s00, s11, s01, fields.desc.sea_level_m);
                }
            }
        }
    }
    if (mesh.indices.empty()) {
        throw std::runtime_error("clipped land mesh produced no triangles");
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

    const float terrain_width_m =
        static_cast<float>(fields.desc.width - 1U) * fields.desc.cell_size_m;
    const float terrain_height_m =
        static_cast<float>(fields.desc.height - 1U) * fields.desc.cell_size_m;
    const float min_x = -terrain_width_m * 0.5F;
    const float min_z = -terrain_height_m * 0.5F;
    const float padding_m = std::max(terrain_width_m, terrain_height_m) * 6.0F;
    const float deep_water_m = std::max(fields.max_water_depth_m, 1.0F);
    const float far_shore_sdf_m = -std::max(fields.max_abs_shore_sdf_m + padding_m, padding_m);

    TerrainMeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(mesh_width) * mesh_height);
    for (std::uint32_t y = 0; y < mesh_height; ++y) {
        for (std::uint32_t x = 0; x < mesh_width; ++x) {
            const bool border = x == 0U || y == 0U || x == mesh_width - 1U || y == mesh_height - 1U;
            const float world_x =
                x == 0U ? min_x - padding_m
                : x == mesh_width - 1U
                    ? min_x + terrain_width_m + padding_m
                    : min_x + (static_cast<float>(x - 1U) * fields.desc.cell_size_m);
            const float world_z =
                y == 0U ? min_z - padding_m
                : y == mesh_height - 1U
                    ? min_z + terrain_height_m + padding_m
                    : min_z + (static_cast<float>(y - 1U) * fields.desc.cell_size_m);

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
                .generator = {0.0F, 0.0F, 0.0F, 0.0F},
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
