#pragma once

#include "planet_config.h"
#include "planet_frame.h"

#include <cubey/render/mesh.h>
#include <cubey/render/clipmap_grid_2d.h>
#include <cubey/render/local_tangent_frame.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <span>
#include <vector>

namespace cubey::projects::planet {

inline constexpr float kPlanetLocalDetailTransitionCells = 16.0F;
inline constexpr float kPlanetLocalDetailMaxTransitionRatio = 0.35F;
inline constexpr std::uint32_t kPlanetMaxLocalDetailPatches =
    cubey::render::clipmap_grid_2d_patch_count(kPlanetMaxLocalDetailLodLevels);

using PlanetLocalDetailPatch = cubey::render::ClipmapGrid2DPatch;
using PlanetLocalDetailPatchList =
    cubey::render::ClipmapGrid2DPatchList<kPlanetMaxLocalDetailPatches>;

struct PlanetLocalDetailPlan {
    cubey::render::LocalTangentFrame local_frame{};
    cubey::render::ClipmapGrid2DConfig grid{};
    PlanetLocalDetailPatchList patches{};
    cubey::render::ClipmapGrid2DDiagnostics clipmap_diagnostics{};
};

struct PlanetLocalDetailDiagnostics {
    bool enabled = false;
    std::uint32_t lod_levels = 0;
    std::uint32_t patch_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    float near_cell_size = 0.0F;
    float outer_half_extent = 0.0F;
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

[[nodiscard]] inline PlanetLocalDetailPlan plan_planet_local_detail(const PlanetConfig& config,
                                                                    const PlanetFrame& frame) {
    const cubey::render::ClipmapGrid2DConfig grid =
        planet_local_detail_clipmap_config(config);
    cubey::render::validate_local_tangent_frame(frame.local_frame);
    PlanetLocalDetailPatchList patches =
        cubey::render::clipmap_grid_2d_patches<kPlanetMaxLocalDetailPatches>(grid);
    return {
        .local_frame = frame.local_frame,
        .grid = grid,
        .patches = patches,
        .clipmap_diagnostics = cubey::render::clipmap_grid_2d_diagnostics(grid, patches),
    };
}

[[nodiscard]] PlanetLocalDetailDiagnostics
planet_local_detail_diagnostics(const PlanetConfig& config,
                                const PlanetLocalDetailPlan& plan);
[[nodiscard]] PlanetLocalDetailBuildResult
make_planet_local_detail_mesh(const PlanetConfig& config, const PlanetFrame& frame);

} // namespace cubey::projects::planet
