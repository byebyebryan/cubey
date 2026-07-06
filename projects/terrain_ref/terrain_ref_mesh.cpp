#include "terrain_ref_mesh.h"

#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>

namespace cubey::projects::terrain_ref {

cubey::render::MeshConfig TerrainRefMeshData::mesh_config() const {
    return cubey::render::indexed_mesh_config(
        std::span<const cubey::render::VertexPositionColorNormal>(vertices.data(), vertices.size()),
        std::span<const std::uint32_t>(indices.data(), indices.size()));
}

cubey::render::ClipmapGrid2DConfig terrain_ref_clipmap_config(const TerrainRefConfig& config) {
    const float region_extent = terrain_ref_extent_m(config);
    return cubey::render::ClipmapGrid2DConfig{
        .lod_levels = 5U,
        .cells_per_axis = 96U,
        .outer_half_extent = std::max(region_extent * 1.5F, 8192.0F),
        .transition_cells = 16.0F,
        .max_transition_ratio = 0.35F,
    };
}

TerrainRefMeshData make_terrain_ref_mesh(const TerrainRefConfig& config) {
    const cubey::render::ClipmapGrid2DConfig clipmap_config = terrain_ref_clipmap_config(config);
    const auto patches = cubey::render::clipmap_grid_2d_patches<32U>(clipmap_config);
    const cubey::render::ClipmapGrid2DDiagnostics diagnostics =
        cubey::render::clipmap_grid_2d_diagnostics(clipmap_config, patches);

    TerrainRefMeshData mesh;
    mesh.vertices.reserve(diagnostics.total_vertices);
    mesh.indices.reserve(diagnostics.total_vertices);

    auto append_vertex = [&mesh, lod_levels = clipmap_config.lod_levels](float x, float z,
                                                                         std::uint32_t level) {
        const float level_t = lod_levels <= 1U
                                  ? 0.0F
                                  : static_cast<float>(level) / static_cast<float>(lod_levels - 1U);
        mesh.vertices.push_back({
            .position = {x, 0.0F, z},
            .color = {level_t, 0.0F, 0.0F},
            .normal = {0.0F, 1.0F, 0.0F},
        });
        const std::size_t index = mesh.vertices.size() - 1U;
        if (index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("terrain_ref mesh exceeds uint32 index range");
        }
        mesh.indices.push_back(static_cast<std::uint32_t>(index));
    };

    for (const cubey::render::ClipmapGrid2DPatch& patch : patches) {
        const float span_x = patch.bounds.max_x - patch.bounds.min_x;
        const float span_z = patch.bounds.max_z - patch.bounds.min_z;
        for (std::uint32_t z = 0; z < patch.cells_z; ++z) {
            const float z0 = patch.bounds.min_z +
                             span_z * (static_cast<float>(z) / static_cast<float>(patch.cells_z));
            const float z1 = patch.bounds.min_z + span_z * (static_cast<float>(z + 1U) /
                                                            static_cast<float>(patch.cells_z));
            for (std::uint32_t x = 0; x < patch.cells_x; ++x) {
                const float x0 = patch.bounds.min_x + span_x * (static_cast<float>(x) /
                                                                static_cast<float>(patch.cells_x));
                const float x1 = patch.bounds.min_x + span_x * (static_cast<float>(x + 1U) /
                                                                static_cast<float>(patch.cells_x));
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

std::uint32_t terrain_ref_triangle_count(const TerrainRefMeshData& mesh) {
    return cubey::render::mesh_index_count(mesh.indices.size() / 3U);
}

} // namespace cubey::projects::terrain_ref
