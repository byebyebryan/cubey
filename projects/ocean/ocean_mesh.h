#pragma once

#include "ocean_config.h"

#include <cubey/render/clipmap_grid_2d.h>

#include <cstddef>
#include <cstdint>

namespace cubey::projects::ocean {

inline constexpr std::uint32_t kOceanMaxMeshPatches =
    cubey::render::clipmap_grid_2d_patch_count(kOceanMaxMeshLodLevels);
inline constexpr float kOceanMeshTransitionCells = 16.0F;
inline constexpr float kOceanMeshMaxTransitionRatio = 0.35F;

using OceanMeshPatchBounds = cubey::render::ClipmapGrid2DBounds;
using OceanMeshPatch = cubey::render::ClipmapGrid2DPatch;
using OceanMeshPatchList = cubey::render::ClipmapGrid2DPatchList<kOceanMaxMeshPatches>;

[[nodiscard]] inline cubey::render::ClipmapGrid2DConfig
ocean_mesh_clipmap_config(const OceanConfig& config) {
    validate_ocean_config(config);
    return {
        .lod_levels = config.mesh_lod_levels,
        .cells_per_axis = config.mesh_cells,
        .outer_half_extent = config.mesh_extent,
        .transition_cells = kOceanMeshTransitionCells,
        .max_transition_ratio = kOceanMeshMaxTransitionRatio,
    };
}

[[nodiscard]] inline float ocean_mesh_near_half_extent(const OceanConfig& config) {
    return cubey::render::clipmap_grid_2d_near_half_extent(ocean_mesh_clipmap_config(config));
}

[[nodiscard]] inline float ocean_mesh_level_half_extent(const OceanConfig& config,
                                                        std::uint32_t level) {
    return cubey::render::clipmap_grid_2d_level_half_extent(ocean_mesh_clipmap_config(config),
                                                            level);
}

[[nodiscard]] inline float ocean_mesh_near_cell_size(const OceanConfig& config) {
    return cubey::render::clipmap_grid_2d_near_cell_size(ocean_mesh_clipmap_config(config));
}

[[nodiscard]] inline float ocean_mesh_level_cell_size(const OceanConfig& config,
                                                      std::uint32_t level) {
    return cubey::render::clipmap_grid_2d_level_cell_size(ocean_mesh_clipmap_config(config), level);
}

[[nodiscard]] inline float ocean_mesh_transition_width(float coarse_cell_size,
                                                       float boundary_extent) {
    return cubey::render::clipmap_grid_2d_transition_width(
        coarse_cell_size, boundary_extent, kOceanMeshTransitionCells, kOceanMeshMaxTransitionRatio);
}

[[nodiscard]] inline std::uint32_t ocean_mesh_cells_for_span(float span, float target_cell_size) {
    return cubey::render::clipmap_grid_2d_cells_for_span(span, target_cell_size);
}

[[nodiscard]] inline std::uint32_t ocean_mesh_patch_vertex_count(const OceanMeshPatch& patch) {
    return cubey::render::clipmap_grid_2d_patch_vertex_count(patch);
}

[[nodiscard]] inline std::uint32_t ocean_mesh_patch_triangle_count(const OceanMeshPatch& patch) {
    return cubey::render::clipmap_grid_2d_patch_triangle_count(patch);
}

inline void ocean_mesh_add_patch(OceanMeshPatchList& list, std::uint32_t level,
                                 OceanMeshPatchBounds bounds, float target_cell_size) {
    cubey::render::clipmap_grid_2d_add_patch(list, level, bounds, target_cell_size);
}

[[nodiscard]] inline OceanMeshPatchList ocean_mesh_clipmap_patches(const OceanConfig& config) {
    return cubey::render::clipmap_grid_2d_patches<kOceanMaxMeshPatches>(
        ocean_mesh_clipmap_config(config));
}

[[nodiscard]] inline std::uint32_t ocean_mesh_patch_count(const OceanConfig& config) {
    validate_ocean_config(config);
    return cubey::render::clipmap_grid_2d_patch_count(config.mesh_lod_levels);
}

[[nodiscard]] inline std::uint32_t ocean_mesh_total_triangle_count(const OceanConfig& config) {
    const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(config);
    return cubey::render::clipmap_grid_2d_total_triangle_count(patches);
}

[[nodiscard]] inline std::uint32_t ocean_mesh_total_vertex_count(const OceanConfig& config) {
    const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(config);
    return cubey::render::clipmap_grid_2d_total_vertex_count(patches);
}

} // namespace cubey::projects::ocean
