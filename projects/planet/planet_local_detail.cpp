#include "planet_local_detail.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace cubey::projects::planet {
namespace {

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] float local_detail_outer_blend(float outer_half_extent, float x, float z) {
    const float outer = outer_half_extent;
    const float radial = std::max(std::abs(x), std::abs(z));
    const float fade_start = outer * 0.86F;
    return 1.0F - smoothstep(fade_start, outer, radial);
}

[[nodiscard]] PlanetLocalDetailView sanitize_view(PlanetLocalDetailView view) {
    if (!std::isfinite(view.camera_altitude_m) || view.camera_altitude_m < 1.0F) {
        view.camera_altitude_m = 1.0F;
    }
    if (!std::isfinite(view.vertical_fov_radians) || view.vertical_fov_radians <= 0.001F) {
        view.vertical_fov_radians = 1.04719758F;
    }
    if (!std::isfinite(view.viewport_height_px) || view.viewport_height_px < 1.0F) {
        view.viewport_height_px = 1.0F;
    }
    view.vertical_fov_radians = std::clamp(view.vertical_fov_radians, 0.01F, 3.05F);
    return view;
}

void append_center_patch(PlanetLocalDetailPatchList& patches,
                         const cubey::render::ClipmapGrid2DConfig& grid, std::uint32_t level) {
    const float outer = cubey::render::clipmap_grid_2d_level_half_extent(grid, level);
    const float cell_size = cubey::render::clipmap_grid_2d_level_cell_size(grid, level);
    cubey::render::clipmap_grid_2d_add_patch(patches, level,
                                             cubey::render::ClipmapGrid2DBounds{
                                                 .min_x = -outer,
                                                 .max_x = outer,
                                                 .min_z = -outer,
                                                 .max_z = outer,
                                             },
                                             cell_size);
}

void append_ring_patches(PlanetLocalDetailPatchList& patches,
                         const cubey::render::ClipmapGrid2DConfig& grid, std::uint32_t level) {
    const float outer = cubey::render::clipmap_grid_2d_level_half_extent(grid, level);
    const float inner = cubey::render::clipmap_grid_2d_level_half_extent(grid, level - 1U);
    const float cell_size = cubey::render::clipmap_grid_2d_level_cell_size(grid, level);
    const float overlap = cubey::render::clipmap_grid_2d_transition_width(
        cell_size, inner, grid.transition_cells, grid.max_transition_ratio);
    cubey::render::clipmap_grid_2d_add_patch(patches, level,
                                             cubey::render::ClipmapGrid2DBounds{
                                                 .min_x = -outer,
                                                 .max_x = outer,
                                                 .min_z = inner - overlap,
                                                 .max_z = outer,
                                             },
                                             cell_size);
    cubey::render::clipmap_grid_2d_add_patch(patches, level,
                                             cubey::render::ClipmapGrid2DBounds{
                                                 .min_x = -outer,
                                                 .max_x = outer,
                                                 .min_z = -outer,
                                                 .max_z = -inner + overlap,
                                             },
                                             cell_size);
    cubey::render::clipmap_grid_2d_add_patch(patches, level,
                                             cubey::render::ClipmapGrid2DBounds{
                                                 .min_x = -outer,
                                                 .max_x = -inner + overlap,
                                                 .min_z = -inner,
                                                 .max_z = inner,
                                             },
                                             cell_size);
    cubey::render::clipmap_grid_2d_add_patch(patches, level,
                                             cubey::render::ClipmapGrid2DBounds{
                                                 .min_x = inner - overlap,
                                                 .max_x = outer,
                                                 .min_z = -inner,
                                                 .max_z = inner,
                                             },
                                             cell_size);
}

[[nodiscard]] PlanetLocalDetailPatchList
active_local_detail_patches(const cubey::render::ClipmapGrid2DConfig& grid,
                            const PlanetLocalDetailActiveRange& active_range) {
    PlanetLocalDetailPatchList patches{};
    if (!active_range.active || active_range.level_count == 0U) {
        return patches;
    }
    const std::uint32_t first = active_range.first_level;
    const std::uint32_t last = first + active_range.level_count - 1U;
    for (std::uint32_t offset = 0U; offset < active_range.level_count; ++offset) {
        const std::uint32_t level = last - offset;
        if (level == first) {
            append_center_patch(patches, grid, level);
        } else {
            append_ring_patches(patches, grid, level);
        }
    }
    return patches;
}

void append_vertex(PlanetLocalDetailMeshData& mesh, float active_outer_half_extent,
                   const cubey::render::ClipmapGrid2DPatch& patch, float u, float v) {
    const float x = std::lerp(patch.bounds.min_x, patch.bounds.max_x, u);
    const float z = std::lerp(patch.bounds.min_z, patch.bounds.max_z, v);
    const std::uint32_t index = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(PlanetLocalDetailVertex{
        .local_xz_m = {x, z},
        .patch_uv = {u, v},
        .level = static_cast<float>(patch.level),
        .blend = local_detail_outer_blend(active_outer_half_extent, x, z),
    });
    mesh.indices.push_back(index);
}

void append_cell(PlanetLocalDetailMeshData& mesh, float active_outer_half_extent,
                 const cubey::render::ClipmapGrid2DPatch& patch, std::uint32_t x, std::uint32_t z) {
    const float u0 = static_cast<float>(x) / static_cast<float>(patch.cells_x);
    const float u1 = static_cast<float>(x + 1U) / static_cast<float>(patch.cells_x);
    const float v0 = static_cast<float>(z) / static_cast<float>(patch.cells_z);
    const float v1 = static_cast<float>(z + 1U) / static_cast<float>(patch.cells_z);
    append_vertex(mesh, active_outer_half_extent, patch, u0, v0);
    append_vertex(mesh, active_outer_half_extent, patch, u1, v0);
    append_vertex(mesh, active_outer_half_extent, patch, u1, v1);
    append_vertex(mesh, active_outer_half_extent, patch, u0, v0);
    append_vertex(mesh, active_outer_half_extent, patch, u1, v1);
    append_vertex(mesh, active_outer_half_extent, patch, u0, v1);
}

} // namespace

PlanetLocalDetailView default_planet_local_detail_view(const PlanetFrame& frame) {
    return sanitize_view(PlanetLocalDetailView{
        .camera_altitude_m = std::max(frame.camera_altitude_m, 1.0F),
        .vertical_fov_radians = 1.04719758F,
        .viewport_height_px = 720.0F,
    });
}

PlanetLocalDetailActiveRange
planet_local_detail_active_range(const PlanetConfig& config,
                                 const cubey::render::ClipmapGrid2DConfig& grid,
                                 PlanetLocalDetailView view) {
    validate_planet_config(config);
    cubey::render::validate_clipmap_grid_2d_config(grid);
    view = sanitize_view(view);
    const float meters_per_pixel =
        (2.0F * view.camera_altitude_m * std::tan(view.vertical_fov_radians * 0.5F)) /
        view.viewport_height_px;
    const float safe_meters_per_pixel = std::max(meters_per_pixel, 0.0001F);
    const std::uint32_t coarsest_level = grid.lod_levels - 1U;
    const float coarsest_cell =
        cubey::render::clipmap_grid_2d_level_cell_size(grid, coarsest_level);

    if (!config.local_detail_enabled) {
        return {
            .active = false,
            .first_level = 0U,
            .level_count = 0U,
            .last_level = 0U,
            .meters_per_pixel = safe_meters_per_pixel,
            .finest_active_cell_size = 0.0F,
            .coarsest_active_cell_size = coarsest_cell,
            .projected_finest_cell_px = 0.0F,
            .active_outer_half_extent = 0.0F,
        };
    }

    for (std::uint32_t level = 0U; level < grid.lod_levels; ++level) {
        const float cell_size = cubey::render::clipmap_grid_2d_level_cell_size(grid, level);
        const float projected_cell_px = cell_size / safe_meters_per_pixel;
        if (projected_cell_px >= kPlanetLocalDetailMinProjectedCellPx) {
            if (level >= coarsest_level) {
                break;
            }
            const std::uint32_t level_count =
                std::min(kPlanetLocalDetailMaxActiveLevels, coarsest_level - level);
            const std::uint32_t active_last_level = level + level_count - 1U;
            return {
                .active = true,
                .first_level = level,
                .level_count = level_count,
                .last_level = active_last_level,
                .meters_per_pixel = safe_meters_per_pixel,
                .finest_active_cell_size = cell_size,
                .coarsest_active_cell_size =
                    cubey::render::clipmap_grid_2d_level_cell_size(grid, active_last_level),
                .projected_finest_cell_px = projected_cell_px,
                .active_outer_half_extent =
                    cubey::render::clipmap_grid_2d_level_half_extent(grid, active_last_level),
            };
        }
    }

    return {
        .active = false,
        .first_level = grid.lod_levels,
        .level_count = 0U,
        .last_level = grid.lod_levels,
        .meters_per_pixel = safe_meters_per_pixel,
        .finest_active_cell_size = 0.0F,
        .coarsest_active_cell_size = coarsest_cell,
        .projected_finest_cell_px = coarsest_cell / safe_meters_per_pixel,
        .active_outer_half_extent = 0.0F,
    };
}

PlanetLocalDetailPlan plan_planet_local_detail(const PlanetConfig& config,
                                               const PlanetFrame& frame) {
    return plan_planet_local_detail(config, frame, default_planet_local_detail_view(frame));
}

PlanetLocalDetailPlan plan_planet_local_detail(const PlanetConfig& config, const PlanetFrame& frame,
                                               PlanetLocalDetailView view) {
    const cubey::render::ClipmapGrid2DConfig grid = planet_local_detail_clipmap_config(config);
    cubey::render::validate_local_tangent_frame(frame.local_frame);
    view = sanitize_view(view);
    const PlanetLocalDetailActiveRange active_range =
        planet_local_detail_active_range(config, grid, view);
    PlanetLocalDetailPatchList patches = active_local_detail_patches(grid, active_range);
    return {
        .local_frame = frame.local_frame,
        .grid = grid,
        .view = view,
        .active_range = active_range,
        .patches = patches,
        .clipmap_diagnostics = cubey::render::clipmap_grid_2d_diagnostics(grid, patches),
    };
}

PlanetLocalDetailDiagnostics planet_local_detail_diagnostics(const PlanetConfig& config,
                                                             const PlanetLocalDetailPlan& plan) {
    validate_planet_config(config);
    const cubey::render::ClipmapGrid2DDiagnostics clipmap = plan.clipmap_diagnostics;
    return {
        .enabled = config.local_detail_enabled,
        .active = plan.active_range.active,
        .lod_levels = clipmap.lod_levels,
        .active_first_level = plan.active_range.first_level,
        .active_level_count = plan.active_range.level_count,
        .active_last_level = plan.active_range.last_level,
        .patch_count = clipmap.patch_count,
        .vertex_count = clipmap.total_vertices,
        .triangle_count = clipmap.total_triangles,
        .near_cell_size = clipmap.near_cell_size,
        .outer_half_extent = clipmap.outer_half_extent,
        .active_outer_half_extent = plan.active_range.active_outer_half_extent,
        .meters_per_pixel = plan.active_range.meters_per_pixel,
        .finest_active_cell_size = plan.active_range.finest_active_cell_size,
        .coarsest_active_cell_size = plan.active_range.coarsest_active_cell_size,
        .projected_finest_cell_px = plan.active_range.projected_finest_cell_px,
        .max_detail_delta_m =
            config.local_detail_enabled ? config.local_detail_height_strength_m : 0.0F,
        .detail_scale_m = config.local_detail_scale_m,
    };
}

PlanetLocalDetailBuildResult make_planet_local_detail_mesh(const PlanetConfig& config,
                                                           const PlanetFrame& frame) {
    return make_planet_local_detail_mesh(config, frame, default_planet_local_detail_view(frame));
}

PlanetLocalDetailBuildResult make_planet_local_detail_mesh(const PlanetConfig& config,
                                                           const PlanetFrame& frame,
                                                           PlanetLocalDetailView view) {
    const PlanetLocalDetailPlan plan = plan_planet_local_detail(config, frame, view);
    PlanetLocalDetailMeshData mesh{};
    mesh.vertices.reserve(plan.clipmap_diagnostics.total_vertices);
    mesh.indices.reserve(plan.clipmap_diagnostics.total_vertices);

    for (const cubey::render::ClipmapGrid2DPatch& patch : plan.patches) {
        for (std::uint32_t z = 0; z < patch.cells_z; ++z) {
            for (std::uint32_t x = 0; x < patch.cells_x; ++x) {
                append_cell(mesh, plan.active_range.active_outer_half_extent, patch, x, z);
            }
        }
    }

    if (mesh.vertices.size() != mesh.indices.size()) {
        throw std::runtime_error("planet local detail mesh expects one index per vertex");
    }
    if (mesh.vertices.size() != plan.clipmap_diagnostics.total_vertices) {
        throw std::runtime_error("planet local detail mesh vertex count mismatch");
    }

    return {
        .mesh = std::move(mesh),
        .diagnostics = planet_local_detail_diagnostics(config, plan),
    };
}

} // namespace cubey::projects::planet
