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

[[nodiscard]] float local_detail_outer_blend(const PlanetConfig& config, float x, float z) {
    const float outer = config.local_detail_outer_half_extent_m;
    const float radial = std::max(std::abs(x), std::abs(z));
    const float fade_start = outer * 0.86F;
    return 1.0F - smoothstep(fade_start, outer, radial);
}

void append_vertex(PlanetLocalDetailMeshData& mesh, const PlanetConfig& config,
                   const cubey::render::ClipmapGrid2DPatch& patch, float u, float v) {
    const float x = std::lerp(patch.bounds.min_x, patch.bounds.max_x, u);
    const float z = std::lerp(patch.bounds.min_z, patch.bounds.max_z, v);
    const std::uint32_t index = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(PlanetLocalDetailVertex{
        .local_xz_m = {x, z},
        .patch_uv = {u, v},
        .level = static_cast<float>(patch.level),
        .blend = local_detail_outer_blend(config, x, z),
    });
    mesh.indices.push_back(index);
}

void append_cell(PlanetLocalDetailMeshData& mesh, const PlanetConfig& config,
                 const cubey::render::ClipmapGrid2DPatch& patch, std::uint32_t x,
                 std::uint32_t z) {
    const float u0 = static_cast<float>(x) / static_cast<float>(patch.cells_x);
    const float u1 = static_cast<float>(x + 1U) / static_cast<float>(patch.cells_x);
    const float v0 = static_cast<float>(z) / static_cast<float>(patch.cells_z);
    const float v1 = static_cast<float>(z + 1U) / static_cast<float>(patch.cells_z);
    append_vertex(mesh, config, patch, u0, v0);
    append_vertex(mesh, config, patch, u1, v0);
    append_vertex(mesh, config, patch, u1, v1);
    append_vertex(mesh, config, patch, u0, v0);
    append_vertex(mesh, config, patch, u1, v1);
    append_vertex(mesh, config, patch, u0, v1);
}

} // namespace

PlanetLocalDetailDiagnostics planet_local_detail_diagnostics(
    const PlanetConfig& config, const PlanetLocalDetailPlan& plan) {
    validate_planet_config(config);
    const cubey::render::ClipmapGrid2DDiagnostics clipmap = plan.clipmap_diagnostics;
    return {
        .enabled = config.local_detail_enabled,
        .lod_levels = clipmap.lod_levels,
        .patch_count = clipmap.patch_count,
        .vertex_count = clipmap.total_vertices,
        .triangle_count = clipmap.total_triangles,
        .near_cell_size = clipmap.near_cell_size,
        .outer_half_extent = clipmap.outer_half_extent,
        .max_detail_delta_m = config.local_detail_enabled
                                  ? config.local_detail_height_strength_m
                                  : 0.0F,
        .detail_scale_m = config.local_detail_scale_m,
    };
}

PlanetLocalDetailBuildResult make_planet_local_detail_mesh(const PlanetConfig& config,
                                                           const PlanetFrame& frame) {
    const PlanetLocalDetailPlan plan = plan_planet_local_detail(config, frame);
    PlanetLocalDetailMeshData mesh{};
    mesh.vertices.reserve(plan.clipmap_diagnostics.total_vertices);
    mesh.indices.reserve(plan.clipmap_diagnostics.total_vertices);

    for (const cubey::render::ClipmapGrid2DPatch& patch : plan.patches) {
        for (std::uint32_t z = 0; z < patch.cells_z; ++z) {
            for (std::uint32_t x = 0; x < patch.cells_x; ++x) {
                append_cell(mesh, config, patch, x, z);
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
