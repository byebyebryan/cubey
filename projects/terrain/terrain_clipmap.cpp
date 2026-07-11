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
    return {
        .lod_levels = config.lod_levels,
        .cells_per_axis = config.cells_per_axis,
        .outer_half_extent = outer_half_extent,
        .transition_cells = 16.0F,
        .max_transition_ratio = 0.35F,
    };
}

TerrainClipmapMeshData make_terrain_clipmap_mesh(const TerrainRuntimeConfig& config) {
    const cubey::render::ClipmapGrid2DConfig clipmap_config = terrain_clipmap_config(config);
    const auto patches = cubey::render::clipmap_grid_2d_patches<64U>(clipmap_config);
    const cubey::render::ClipmapGrid2DDiagnostics diagnostics =
        cubey::render::clipmap_grid_2d_diagnostics(clipmap_config, patches);

    TerrainClipmapMeshData mesh;
    mesh.diagnostics = diagnostics;
    mesh.vertices.reserve(diagnostics.total_vertices);
    mesh.indices.reserve(diagnostics.total_vertices);

    auto append_vertex = [&mesh, &clipmap_config](float x, float z, std::uint32_t level) {
        const float level_t =
            clipmap_config.lod_levels <= 1U
                ? 0.0F
                : static_cast<float>(level) / static_cast<float>(clipmap_config.lod_levels - 1U);
        const float cell_size =
            cubey::render::clipmap_grid_2d_level_cell_size(clipmap_config, level);
        const float morph = vertex_morph(clipmap_config, level, x, z);
        mesh.vertices.push_back({
            .position = {x, 0.0F, z},
            .color = {cell_size, morph, level_t},
            .normal = {0.0F, 1.0F, 0.0F},
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
                append_vertex(x0, z0, patch.level);
                append_vertex(x1, z0, patch.level);
                append_vertex(x0, z1, patch.level);
                append_vertex(x1, z0, patch.level);
                append_vertex(x1, z1, patch.level);
                append_vertex(x0, z1, patch.level);
            }
        }
    }
    return mesh;
}

std::uint32_t terrain_clipmap_triangle_count(const TerrainClipmapMeshData& mesh) {
    return cubey::render::mesh_index_count(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::terrain
