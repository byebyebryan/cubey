#include "terrain_clipmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] float vertex_morph(const cubey::render::ClipmapGrid2DConfig& config,
                                 std::uint32_t level, float x, float z) {
    if (level + 1U >= config.lod_levels) {
        return 0.0F;
    }
    const float outer = cubey::render::clipmap_grid_2d_level_half_extent(config, level);
    const float coarse_cell = cubey::render::clipmap_grid_2d_level_cell_size(config, level + 1U);
    const float transition = cubey::render::clipmap_grid_2d_transition_width(
        coarse_cell, outer, config.transition_cells, config.max_transition_ratio);
    const float distance_to_outer = outer - std::max(std::abs(x), std::abs(z));
    const float cell_size = cubey::render::clipmap_grid_2d_level_cell_size(config, level);
    if (distance_to_outer <= cell_size) {
        return 1.0F;
    }
    return 1.0F - smoothstep(0.0F, transition, distance_to_outer);
}

} // namespace

cubey::render::MeshConfig TerrainClipmapMeshData::mesh_config() const {
    return cubey::render::indexed_mesh_config(
        std::span<const cubey::render::VertexPositionColorNormal>(vertices.data(), vertices.size()),
        std::span<const std::uint32_t>(indices.data(), indices.size()));
}

cubey::render::ClipmapGrid2DConfig terrain_clipmap_config(const TerrainRuntimeConfig& config) {
    validate_terrain_runtime_config(config);
    const float near_half_extent =
        config.near_cell_size_m * static_cast<float>(config.cells_per_axis) * 0.5F;
    const float outer_half_extent =
        near_half_extent * static_cast<float>(1U << (config.lod_levels - 1U));
    // Eleven coarse cells keeps every ring span on its advertised cell grid.
    return {
        .lod_levels = config.lod_levels,
        .cells_per_axis = config.cells_per_axis,
        .outer_half_extent = outer_half_extent,
        .transition_cells = 16.0F,
        .max_transition_ratio = 11.0F / 32.0F,
    };
}

TerrainClipmapMeshData make_terrain_clipmap_mesh(const TerrainRuntimeConfig& config) {
    const cubey::render::ClipmapGrid2DConfig clipmap_config = terrain_clipmap_config(config);
    const auto patches = cubey::render::clipmap_grid_2d_patches<64U>(clipmap_config);
    const cubey::render::ClipmapGrid2DDiagnostics diagnostics =
        cubey::render::clipmap_grid_2d_diagnostics(clipmap_config, patches);

    TerrainClipmapMeshData mesh;
    mesh.diagnostics = diagnostics;
    const std::uint32_t skirt_vertex_count =
        (clipmap_config.lod_levels - 1U) * clipmap_config.cells_per_axis * 4U * 6U;
    mesh.vertices.reserve(diagnostics.total_vertices + skirt_vertex_count);
    mesh.indices.reserve(diagnostics.total_vertices + skirt_vertex_count);

    auto append_vertex = [&mesh, &clipmap_config](float x, float z, std::uint32_t level,
                                                  float skirt_depth_m) {
        const float level_t =
            clipmap_config.lod_levels <= 1U
                ? 0.0F
                : static_cast<float>(level) / static_cast<float>(clipmap_config.lod_levels - 1U);
        const float cell_size =
            cubey::render::clipmap_grid_2d_level_cell_size(clipmap_config, level);
        const float child_half_extent =
            level == 0U
                ? 0.0F
                : cubey::render::clipmap_grid_2d_level_half_extent(clipmap_config, level - 1U);
        const float morph = vertex_morph(clipmap_config, level, x, z);
        mesh.vertices.push_back({
            .position = {x, 0.0F, z},
            .color = {cell_size, morph, level_t},
            .normal = {child_half_extent, skirt_depth_m,
                       cubey::render::clipmap_grid_2d_near_cell_size(clipmap_config)},
        });
        const std::size_t index = mesh.vertices.size() - 1U;
        if (index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("terrain clipmap exceeds uint32 index range");
        }
        mesh.indices.push_back(static_cast<std::uint32_t>(index));
    };

    for (std::size_t patch_index = patches.count; patch_index > 0U; --patch_index) {
        const cubey::render::ClipmapGrid2DPatch& patch = patches.patches[patch_index - 1U];
        const float span_x = patch.bounds.max_x - patch.bounds.min_x;
        const float span_z = patch.bounds.max_z - patch.bounds.min_z;
        for (std::uint32_t z = 0; z < patch.cells_z; ++z) {
            const float z0 = patch.bounds.min_z +
                             span_z * static_cast<float>(z) / static_cast<float>(patch.cells_z);
            const float z1 = patch.bounds.min_z + span_z * static_cast<float>(z + 1U) /
                                                      static_cast<float>(patch.cells_z);
            for (std::uint32_t x = 0; x < patch.cells_x; ++x) {
                const float x0 = patch.bounds.min_x +
                                 span_x * static_cast<float>(x) / static_cast<float>(patch.cells_x);
                const float x1 = patch.bounds.min_x + span_x * static_cast<float>(x + 1U) /
                                                          static_cast<float>(patch.cells_x);
                append_vertex(x0, z0, patch.level, 0.0F);
                append_vertex(x1, z0, patch.level, 0.0F);
                append_vertex(x0, z1, patch.level, 0.0F);
                append_vertex(x1, z0, patch.level, 0.0F);
                append_vertex(x1, z1, patch.level, 0.0F);
                append_vertex(x0, z1, patch.level, 0.0F);
            }
        }
    }

    const auto append_skirt_segment = [&append_vertex](cubey::math::Vec2 start,
                                                       cubey::math::Vec2 end, std::uint32_t level,
                                                       float depth_m) {
        append_vertex(start.x, start.y, level, 0.0F);
        append_vertex(start.x, start.y, level, depth_m);
        append_vertex(end.x, end.y, level, 0.0F);
        append_vertex(end.x, end.y, level, 0.0F);
        append_vertex(start.x, start.y, level, depth_m);
        append_vertex(end.x, end.y, level, depth_m);
    };
    for (std::uint32_t level = 0; level + 1U < clipmap_config.lod_levels; ++level) {
        const float outer = cubey::render::clipmap_grid_2d_level_half_extent(clipmap_config, level);
        const float skirt_depth_m =
            cubey::render::clipmap_grid_2d_level_cell_size(clipmap_config, level + 1U);
        for (std::uint32_t cell = 0; cell < clipmap_config.cells_per_axis; ++cell) {
            const float t0 =
                static_cast<float>(cell) / static_cast<float>(clipmap_config.cells_per_axis);
            const float t1 =
                static_cast<float>(cell + 1U) / static_cast<float>(clipmap_config.cells_per_axis);
            const float a = std::lerp(-outer, outer, t0);
            const float b = std::lerp(-outer, outer, t1);
            append_skirt_segment({a, -outer}, {b, -outer}, level, skirt_depth_m);
            append_skirt_segment({a, outer}, {b, outer}, level, skirt_depth_m);
            append_skirt_segment({-outer, a}, {-outer, b}, level, skirt_depth_m);
            append_skirt_segment({outer, a}, {outer, b}, level, skirt_depth_m);
        }
    }
    if (mesh.vertices.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("terrain clipmap diagnostics exceed uint32 range");
    }
    mesh.diagnostics.total_vertices = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.diagnostics.total_triangles = terrain_clipmap_triangle_count(mesh);
    return mesh;
}

std::uint32_t terrain_clipmap_triangle_count(const TerrainClipmapMeshData& mesh) {
    return cubey::render::mesh_index_count(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::terrain
