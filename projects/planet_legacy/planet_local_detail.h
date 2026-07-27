#pragma once

#include "planet_config.h"
#include "planet_frame.h"

#include <cubey/render/clipmap_grid_2d.h>
#include <cubey/render/local_tangent_frame.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace cubey::projects::planet {

inline constexpr float kPlanetLocalDetailTransitionCells = 16.0F;
inline constexpr float kPlanetLocalDetailMaxTransitionRatio = 0.35F;
inline constexpr float kPlanetLocalDetailMinProjectedCellPx = 1.0F;
inline constexpr std::uint32_t kPlanetLocalDetailMaxActiveLevels = 3;
inline constexpr std::uint32_t kPlanetMaxLocalDetailPatches =
    cubey::render::clipmap_grid_2d_patch_count(kPlanetMaxLocalDetailLodLevels);

using PlanetLocalDetailPatch = cubey::render::ClipmapGrid2DPatch;
using PlanetLocalDetailPatchList =
    cubey::render::ClipmapGrid2DPatchList<kPlanetMaxLocalDetailPatches>;

struct PlanetLocalDetailView {
    float camera_clearance_m = 250.0F;
    float vertical_fov_radians = 1.04719758F;
    float viewport_height_px = 720.0F;
    bool full_active_range = false;
    std::uint32_t minimum_lod_levels = 0;
    float minimum_outer_half_extent_m = 0.0F;
};

struct PlanetLocalDetailActiveRange {
    bool active = false;
    std::uint32_t first_level = 0;
    std::uint32_t level_count = 0;
    std::uint32_t last_level = 0;
    float meters_per_pixel = 0.0F;
    float finest_active_cell_size = 0.0F;
    float coarsest_active_cell_size = 0.0F;
    float projected_finest_cell_px = 0.0F;
    float active_outer_half_extent = 0.0F;
};

struct PlanetLocalDetailPlan {
    cubey::render::LocalTangentFrame local_frame{};
    cubey::render::ClipmapGrid2DConfig grid{};
    PlanetLocalDetailView view{};
    PlanetLocalDetailActiveRange active_range{};
    PlanetLocalDetailPatchList patches{};
    cubey::render::ClipmapGrid2DDiagnostics clipmap_diagnostics{};
};

struct PlanetLocalDetailDiagnostics {
    bool enabled = false;
    bool active = false;
    std::uint32_t lod_levels = 0;
    std::uint32_t active_first_level = 0;
    std::uint32_t active_level_count = 0;
    std::uint32_t active_last_level = 0;
    std::uint32_t patch_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    float near_cell_size = 0.0F;
    float outer_half_extent = 0.0F;
    float active_outer_half_extent = 0.0F;
    float meters_per_pixel = 0.0F;
    float finest_active_cell_size = 0.0F;
    float coarsest_active_cell_size = 0.0F;
    float projected_finest_cell_px = 0.0F;
    float max_detail_delta_m = 0.0F;
    float detail_scale_m = 0.0F;
};

struct PlanetLocalDetailVertex {
    cubey::render::PrimitiveVec2 local_xz_m{};
    cubey::render::PrimitiveVec2 patch_uv{};
    float level = 0.0F;
    float blend = 0.0F;
};

struct PlanetLocalDetailMeshData {
    std::vector<PlanetLocalDetailVertex> vertices{};
    std::vector<std::uint32_t> indices{};

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const {
        return cubey::render::indexed_mesh_config(
            std::span<const PlanetLocalDetailVertex>{vertices.data(), vertices.size()},
            std::span<const std::uint32_t>{indices.data(), indices.size()});
    }
};

struct PlanetLocalDetailBuildResult {
    PlanetLocalDetailMeshData mesh{};
    PlanetLocalDetailDiagnostics diagnostics{};
};

[[nodiscard]] inline cubey::render::ClipmapGrid2DConfig
planet_local_detail_clipmap_config(const PlanetConfig& config) {
    validate_planet_config(config);
    return {
        .lod_levels = config.local_detail_lod_levels,
        .cells_per_axis = config.local_detail_cells_per_axis,
        .outer_half_extent = config.local_detail_outer_half_extent_m,
        .transition_cells = kPlanetLocalDetailTransitionCells,
        .max_transition_ratio = kPlanetLocalDetailMaxTransitionRatio,
    };
}

[[nodiscard]] inline cubey::render::ClipmapGrid2DConfig
planet_local_detail_clipmap_config(const PlanetConfig& config, PlanetLocalDetailView view) {
    cubey::render::ClipmapGrid2DConfig grid = planet_local_detail_clipmap_config(config);
    if (view.minimum_lod_levels > 0U) {
        grid.lod_levels = std::max(
            grid.lod_levels, std::min(view.minimum_lod_levels, kPlanetMaxLocalDetailLodLevels));
    }
    if (std::isfinite(view.minimum_outer_half_extent_m) &&
        view.minimum_outer_half_extent_m > grid.outer_half_extent) {
        grid.outer_half_extent = view.minimum_outer_half_extent_m;
    }
    return grid;
}

[[nodiscard]] PlanetLocalDetailView default_planet_local_detail_view(const PlanetFrame& frame);
[[nodiscard]] PlanetLocalDetailActiveRange
planet_local_detail_active_range(const PlanetConfig& config,
                                 const cubey::render::ClipmapGrid2DConfig& grid,
                                 PlanetLocalDetailView view);
[[nodiscard]] PlanetLocalDetailPlan plan_planet_local_detail(const PlanetConfig& config,
                                                             const PlanetFrame& frame);
[[nodiscard]] PlanetLocalDetailPlan plan_planet_local_detail(const PlanetConfig& config,
                                                             const PlanetFrame& frame,
                                                             PlanetLocalDetailView view);

[[nodiscard]] PlanetLocalDetailDiagnostics
planet_local_detail_diagnostics(const PlanetConfig& config, const PlanetLocalDetailPlan& plan);
[[nodiscard]] PlanetLocalDetailBuildResult make_planet_local_detail_mesh(const PlanetConfig& config,
                                                                         const PlanetFrame& frame);
[[nodiscard]] PlanetLocalDetailBuildResult
make_planet_local_detail_mesh(const PlanetConfig& config, const PlanetFrame& frame,
                              PlanetLocalDetailView view);

} // namespace cubey::projects::planet
