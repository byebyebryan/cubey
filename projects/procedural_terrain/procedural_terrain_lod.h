#pragma once

#include "procedural_terrain_fields.h"

#include <cubey/render/clipmap_grid_2d.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::procedural_terrain {

inline constexpr std::uint32_t kTerrainClipmapDefaultLodLevels = 4U;
inline constexpr std::uint32_t kTerrainClipmapMaxLodLevels = 6U;
inline constexpr std::uint32_t kTerrainClipmapDefaultCellsPerAxis = 64U;
inline constexpr std::uint32_t kTerrainClipmapMaxPatches =
    cubey::render::clipmap_grid_2d_patch_count(kTerrainClipmapMaxLodLevels);

using TerrainClipmapPatch = cubey::render::ClipmapGrid2DPatch;
using TerrainClipmapPatchList = cubey::render::ClipmapGrid2DPatchList<kTerrainClipmapMaxPatches>;

struct TerrainClipmapPlan {
    cubey::render::ClipmapGrid2DConfig grid{};
    TerrainClipmapPatchList patches{};
    cubey::render::ClipmapGrid2DDiagnostics diagnostics{};
    float field_half_extent_x_m = 0.0F;
    float field_half_extent_z_m = 0.0F;
};

[[nodiscard]] inline float terrain_field_half_extent_x_m(const TerrainGridDesc& desc) {
    if (desc.width < 2U || desc.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain clipmap requires a valid terrain grid");
    }
    return static_cast<float>(desc.width - 1U) * desc.cell_size_m * 0.5F;
}

[[nodiscard]] inline float terrain_field_half_extent_z_m(const TerrainGridDesc& desc) {
    if (desc.height < 2U || desc.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain clipmap requires a valid terrain grid");
    }
    return static_cast<float>(desc.height - 1U) * desc.cell_size_m * 0.5F;
}

[[nodiscard]] inline cubey::render::ClipmapGrid2DConfig
terrain_clipmap_grid_config(const TerrainGridDesc& desc,
                            std::uint32_t lod_levels = kTerrainClipmapDefaultLodLevels,
                            std::uint32_t cells_per_axis = kTerrainClipmapDefaultCellsPerAxis) {
    if (lod_levels == 0U || lod_levels > kTerrainClipmapMaxLodLevels) {
        throw std::runtime_error("terrain clipmap LOD levels out of supported range");
    }
    const float half_x = terrain_field_half_extent_x_m(desc);
    const float half_z = terrain_field_half_extent_z_m(desc);
    return {
        .lod_levels = lod_levels,
        .cells_per_axis = cells_per_axis,
        .outer_half_extent = std::max(half_x, half_z),
        .transition_cells = 8.0F,
        .max_transition_ratio = 0.25F,
    };
}

[[nodiscard]] inline TerrainClipmapPatchList
terrain_clipmap_patches(const TerrainGridDesc& desc,
                        std::uint32_t lod_levels = kTerrainClipmapDefaultLodLevels,
                        std::uint32_t cells_per_axis = kTerrainClipmapDefaultCellsPerAxis) {
    return cubey::render::clipmap_grid_2d_patches<kTerrainClipmapMaxPatches>(
        terrain_clipmap_grid_config(desc, lod_levels, cells_per_axis));
}

[[nodiscard]] inline bool terrain_clipmap_patch_overlaps_field(const TerrainGridDesc& desc,
                                                               const TerrainClipmapPatch& patch) {
    const float half_x = terrain_field_half_extent_x_m(desc);
    const float half_z = terrain_field_half_extent_z_m(desc);
    return patch.bounds.max_x >= -half_x && patch.bounds.min_x <= half_x &&
           patch.bounds.max_z >= -half_z && patch.bounds.min_z <= half_z;
}

[[nodiscard]] inline TerrainClipmapPlan
terrain_clipmap_plan(const TerrainGridDesc& desc,
                     std::uint32_t lod_levels = kTerrainClipmapDefaultLodLevels,
                     std::uint32_t cells_per_axis = kTerrainClipmapDefaultCellsPerAxis) {
    const cubey::render::ClipmapGrid2DConfig grid =
        terrain_clipmap_grid_config(desc, lod_levels, cells_per_axis);
    const TerrainClipmapPatchList patches =
        cubey::render::clipmap_grid_2d_patches<kTerrainClipmapMaxPatches>(grid);
    return {
        .grid = grid,
        .patches = patches,
        .diagnostics = cubey::render::clipmap_grid_2d_diagnostics(grid, patches),
        .field_half_extent_x_m = terrain_field_half_extent_x_m(desc),
        .field_half_extent_z_m = terrain_field_half_extent_z_m(desc),
    };
}

} // namespace cubey::projects::procedural_terrain
