#include "planet_surface.h"

#include "planet_surface_field.h"

#include <cubey/core/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace cubey::projects::planet {
namespace {

using cubey::render::PrimitiveVec2;
using cubey::render::PrimitiveVec3;
using cubey::render::VertexPositionColorNormalUv;

constexpr std::array<PrimitiveVec3, 6> kFaceColors{
    PrimitiveVec3{0.95F, 0.22F, 0.18F}, PrimitiveVec3{0.18F, 0.45F, 0.95F},
    PrimitiveVec3{0.20F, 0.78F, 0.36F}, PrimitiveVec3{0.96F, 0.70F, 0.18F},
    PrimitiveVec3{0.58F, 0.30F, 0.92F}, PrimitiveVec3{0.15F, 0.78F, 0.78F},
};

struct PlanetSurfacePatchIdHash {
    [[nodiscard]] std::size_t operator()(PlanetSurfacePatchId id) const noexcept {
        std::uint64_t hash = id.face * 73856093ULL;
        hash ^= id.level * 19349663ULL;
        hash ^= id.x * 83492791ULL;
        hash ^= id.y * 2654435761ULL;
        return static_cast<std::size_t>(hash);
    }
};

[[nodiscard]] PlanetSurfacePatchId parent_patch_id(PlanetSurfacePatchId id) {
    return {
        .face = id.face,
        .level = id.level - 1U,
        .x = id.x >> 1U,
        .y = id.y >> 1U,
    };
}

class PlanetPatchSelectionLookup {
  public:
    explicit PlanetPatchSelectionLookup(std::span<const PlanetSurfacePatchId> previous_selection) {
        selected_.reserve(previous_selection.size());
        refined_ancestors_.reserve(previous_selection.size());
        for (PlanetSurfacePatchId id : previous_selection) {
            selected_.insert(id);
            while (id.level > 0U) {
                id = parent_patch_id(id);
                refined_ancestors_.insert(id);
            }
        }
    }

    [[nodiscard]] bool was_selected(PlanetSurfacePatchId id) const {
        return selected_.contains(id);
    }

    [[nodiscard]] bool was_refined(PlanetSurfacePatchId id) const {
        return refined_ancestors_.contains(id);
    }

  private:
    std::unordered_set<PlanetSurfacePatchId, PlanetSurfacePatchIdHash> selected_;
    std::unordered_set<PlanetSurfacePatchId, PlanetSurfacePatchIdHash> refined_ancestors_;
};

[[nodiscard]] float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] PrimitiveVec3 to_primitive(cubey::math::Vec3 value) {
    return {value.x, value.y, value.z};
}

[[nodiscard]] PrimitiveVec3 latitude_color(cubey::math::Vec3 normal) {
    const float latitude = normal.y * 0.5F + 0.5F;
    return {
        0.035F + 0.030F * latitude,
        0.100F + 0.070F * latitude,
        0.230F + 0.200F * latitude,
    };
}

[[nodiscard]] PrimitiveVec3 final_color(const PlanetConfig& config,
                                        const PlanetSurfaceSample& sample) {
    if (config.terrain_enabled && config.terrain_height_scale_m > 0.0F) {
        return to_primitive(planet_surface_material_color(
            sample.material, sample.normalized_elevation, sample.normalized_slope,
            sample.moisture, sample.temperature));
    }
    return latitude_color(sample.normal);
}

[[nodiscard]] PrimitiveVec3 patch_color(PlanetSurfacePatchId id) {
    std::uint32_t hash = id.face * 73856093U;
    hash ^= id.level * 19349663U;
    hash ^= id.x * 83492791U;
    hash ^= id.y * 2654435761U;
    const float band = static_cast<float>(hash % 97U) / 96.0F;
    return {
        0.18F + 0.58F * band,
        0.78F - 0.42F * band,
        0.28F + 0.36F * (1.0F - band),
    };
}

[[nodiscard]] PrimitiveVec3 lod_color(std::uint32_t level, std::uint32_t max_level) {
    const float t =
        max_level == 0U ? 0.0F : static_cast<float>(level) / static_cast<float>(max_level);
    return {
        0.12F + 0.82F * t,
        0.55F - 0.28F * t,
        0.95F - 0.76F * t,
    };
}

[[nodiscard]] PrimitiveVec3 screen_error_color(float error_px, float target_px) {
    const float t = std::clamp(error_px / std::max(target_px, 0.0001F), 0.0F, 2.0F) * 0.5F;
    return {
        0.16F + 0.80F * t,
        0.82F - 0.46F * t,
        0.24F,
    };
}

[[nodiscard]] float lod_transition_pressure(float error_px, float target_px) {
    const float ratio = error_px / std::max(target_px, 0.0001F);
    return 1.0F - std::clamp(std::abs(ratio - 1.0F) / 0.25F, 0.0F, 1.0F);
}

[[nodiscard]] PrimitiveVec3 lod_transition_color(float error_px, float target_px) {
    const float pressure = lod_transition_pressure(error_px, target_px);
    return {
        lerp(0.05F, 0.98F, pressure),
        lerp(0.10F, 0.72F, pressure),
        lerp(0.26F, 0.18F, pressure),
    };
}

[[nodiscard]] PrimitiveVec3 cell_edge_color(const PlanetConfig& config,
                                            const PlanetSurfacePatchInstance& patch) {
    const PlanetSurfacePatchBounds bounds = planet_surface_patch_bounds(config, patch.id);
    const float u_mid = (bounds.u0 + bounds.u1) * 0.5F;
    const float v_mid = (bounds.v0 + bounds.v1) * 0.5F;
    const cubey::math::DVec3 edge_a =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u0, v_mid);
    const cubey::math::DVec3 edge_b =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u1, v_mid);
    const cubey::math::DVec3 edge_c =
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, bounds.v0);
    const cubey::math::DVec3 edge_d =
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, bounds.v1);
    const float horizontal_cell_m = static_cast<float>(glm::length(edge_b - edge_a)) /
                                    static_cast<float>(config.patch_resolution);
    const float vertical_cell_m = static_cast<float>(glm::length(edge_d - edge_c)) /
                                  static_cast<float>(config.patch_resolution);
    const float cell_edge_m = std::max(std::max(horizontal_cell_m, vertical_cell_m), 1.0F);
    const float detail =
        std::clamp(std::log2(std::max(config.radius_m, 1.0F) / cell_edge_m) / 16.0F, 0.0F, 1.0F);
    return {
        lerp(0.95F, 0.12F, detail),
        lerp(0.42F, 0.78F, detail),
        lerp(0.14F, 0.95F, detail),
    };
}

[[nodiscard]] PrimitiveVec3 terrain_height_color(const PlanetConfig& config, float height_m) {
    const float t =
        std::clamp(height_m / std::max(config.terrain_height_scale_m, 1.0F), -1.0F, 1.0F) * 0.5F +
        0.5F;
    if (t < 0.5F) {
        const float blend = t * 2.0F;
        return {
            lerp(0.04F, 0.08F, blend),
            lerp(0.12F, 0.42F, blend),
            lerp(0.36F, 0.20F, blend),
        };
    }
    const float blend = (t - 0.5F) * 2.0F;
    return {
        lerp(0.08F, 0.92F, blend),
        lerp(0.42F, 0.88F, blend),
        lerp(0.20F, 0.74F, blend),
    };
}

[[nodiscard]] PrimitiveVec3 terrain_slope_color(float normalized_slope) {
    const float t = std::clamp(normalized_slope, 0.0F, 1.0F);
    return {
        lerp(0.08F, 0.95F, t),
        lerp(0.25F, 0.66F, t),
        lerp(0.42F, 0.14F, t),
    };
}

[[nodiscard]] PrimitiveVec3 bathymetry_color(float normalized_bathymetry) {
    const float t = std::clamp(normalized_bathymetry, 0.0F, 1.0F);
    return {
        lerp(0.04F, 0.01F, t),
        lerp(0.28F, 0.06F, t),
        lerp(0.44F, 0.88F, t),
    };
}

[[nodiscard]] PrimitiveVec3 shoreline_color(float shoreline_mask) {
    const float t = std::clamp(shoreline_mask, 0.0F, 1.0F);
    return {
        lerp(0.03F, 0.95F, t),
        lerp(0.12F, 0.82F, t),
        lerp(0.20F, 0.32F, t),
    };
}

[[nodiscard]] PrimitiveVec3 land_mask_color(float land_mask) {
    const float t = std::clamp(land_mask, 0.0F, 1.0F);
    return {
        lerp(0.02F, 0.20F, t),
        lerp(0.10F, 0.62F, t),
        lerp(0.30F, 0.14F, t),
    };
}

[[nodiscard]] PrimitiveVec3 moisture_color(float moisture) {
    const float t = std::clamp(moisture, 0.0F, 1.0F);
    return {
        lerp(0.56F, 0.04F, t),
        lerp(0.42F, 0.46F, t),
        lerp(0.18F, 0.72F, t),
    };
}

[[nodiscard]] PrimitiveVec3 temperature_color(float temperature) {
    const float t = std::clamp(temperature, 0.0F, 1.0F);
    return {
        lerp(0.08F, 0.95F, t),
        lerp(0.24F, 0.44F, t),
        lerp(0.82F, 0.10F, t),
    };
}

[[nodiscard]] PrimitiveVec3 roughness_color(float roughness) {
    const float t = std::clamp(roughness, 0.0F, 1.0F);
    return {
        lerp(0.08F, 0.90F, t),
        lerp(0.09F, 0.90F, t),
        lerp(0.12F, 0.96F, t),
    };
}

[[nodiscard]] PrimitiveVec3 terrain_material_debug_color(PlanetSurfaceMaterial material) {
    switch (material) {
    case PlanetSurfaceMaterial::DeepWater:
        return {0.02F, 0.08F, 0.46F};
    case PlanetSurfaceMaterial::ShallowWater:
        return {0.05F, 0.30F, 0.66F};
    case PlanetSurfaceMaterial::Beach:
        return {0.86F, 0.70F, 0.34F};
    case PlanetSurfaceMaterial::Lowland:
        return {0.14F, 0.62F, 0.22F};
    case PlanetSurfaceMaterial::Highland:
        return {0.62F, 0.48F, 0.28F};
    case PlanetSurfaceMaterial::Snow:
        return {0.88F, 0.92F, 0.96F};
    }
    return {0.14F, 0.62F, 0.22F};
}

[[nodiscard]] PrimitiveVec3 seam_surface_color(cubey::math::Vec3 normal) {
    const PrimitiveVec3 color = latitude_color(normal);
    return {
        color[0] * 0.28F,
        color[1] * 0.34F,
        color[2] * 0.42F,
    };
}

[[nodiscard]] PrimitiveVec3 skirt_color(const PlanetConfig& config, cubey::math::Vec3 normal) {
    if (config.debug_view == PlanetDebugView::Seams) {
        return {1.0F, 0.82F, 0.22F};
    }
    return latitude_color(normal);
}

[[nodiscard]] PrimitiveVec3 vertex_color(const PlanetConfig& config,
                                         const PlanetSurfacePatchInstance& patch,
                                         const PlanetSurfaceSample& sample) {
    switch (config.debug_view) {
    case PlanetDebugView::Final:
        return final_color(config, sample);
    case PlanetDebugView::FaceId:
        return kFaceColors[patch.id.face];
    case PlanetDebugView::PatchId:
        return patch_color(patch.id);
    case PlanetDebugView::LodLevel:
        return lod_color(patch.id.level, config.max_lod_level);
    case PlanetDebugView::ScreenError:
        return screen_error_color(patch.screen_error_px, config.lod_target_edge_px);
    case PlanetDebugView::LodTransition:
        return lod_transition_color(patch.screen_error_px, config.lod_target_edge_px);
    case PlanetDebugView::Seams:
        return seam_surface_color(sample.normal);
    case PlanetDebugView::CellEdge:
        return cell_edge_color(config, patch);
    case PlanetDebugView::TerrainHeight:
        return terrain_height_color(config, sample.height_m);
    case PlanetDebugView::TerrainSlope:
        return terrain_slope_color(sample.normalized_slope);
    case PlanetDebugView::TerrainMaterial:
        return terrain_material_debug_color(sample.material);
    case PlanetDebugView::Bathymetry:
        return bathymetry_color(sample.normalized_bathymetry);
    case PlanetDebugView::Shoreline:
        return shoreline_color(sample.shoreline_mask);
    case PlanetDebugView::LandMask:
        return land_mask_color(sample.land_mask);
    case PlanetDebugView::Moisture:
        return moisture_color(sample.moisture);
    case PlanetDebugView::Temperature:
        return temperature_color(sample.temperature);
    case PlanetDebugView::Roughness:
        return roughness_color(sample.roughness);
    case PlanetDebugView::Wireframe:
        return lod_color(patch.id.level, config.max_lod_level);
    case PlanetDebugView::CelestialPlanes:
        return latitude_color(sample.sphere_normal);
    case PlanetDebugView::LocalDetailWireframe:
        return {0.26F, 0.42F, 0.58F};
    case PlanetDebugView::LocalDetailBlend:
        return {0.22F, 0.58F, 0.80F};
    case PlanetDebugView::LocalDetailHeight:
        return terrain_height_color(config, sample.height_m);
    }
    return final_color(config, sample);
}

void update_edge_range(PlanetSurfaceDiagnostics& diagnostics, cubey::math::DVec3 a,
                       cubey::math::DVec3 b) {
    const float length = static_cast<float>(glm::length(a - b));
    if (diagnostics.min_edge_length_m == 0.0F) {
        diagnostics.min_edge_length_m = length;
    } else {
        diagnostics.min_edge_length_m = std::min(diagnostics.min_edge_length_m, length);
    }
    diagnostics.max_edge_length_m = std::max(diagnostics.max_edge_length_m, length);
}

void update_screen_error_range(PlanetSurfaceDiagnostics& diagnostics, float value) {
    if (diagnostics.visible_patch_count == 0U || diagnostics.min_screen_error_px == 0.0F) {
        diagnostics.min_screen_error_px = value;
    } else {
        diagnostics.min_screen_error_px = std::min(diagnostics.min_screen_error_px, value);
    }
    diagnostics.max_screen_error_px = std::max(diagnostics.max_screen_error_px, value);
}

void update_lod_transition_diagnostics(const PlanetConfig& config,
                                       PlanetSurfaceDiagnostics& diagnostics, float error_px) {
    const float pressure = lod_transition_pressure(error_px, config.lod_target_edge_px);
    diagnostics.max_transition_pressure = std::max(diagnostics.max_transition_pressure, pressure);
    if (pressure > 0.0F) {
        ++diagnostics.transition_candidate_count;
    }
}

void update_lod_cell_edge_range(PlanetSurfaceDiagnostics& diagnostics, std::uint32_t level,
                                float value) {
    if (level >= diagnostics.min_cell_edge_m_by_lod.size()) {
        return;
    }
    if (diagnostics.min_cell_edge_m_by_lod[level] == 0.0F) {
        diagnostics.min_cell_edge_m_by_lod[level] = value;
    } else {
        diagnostics.min_cell_edge_m_by_lod[level] =
            std::min(diagnostics.min_cell_edge_m_by_lod[level], value);
    }
    diagnostics.max_cell_edge_m_by_lod[level] =
        std::max(diagnostics.max_cell_edge_m_by_lod[level], value);
}

[[nodiscard]] std::array<cubey::math::DVec3, 5>
patch_sample_points(const PlanetConfig& config, const PlanetSurfacePatchInstance& patch) {
    const PlanetSurfacePatchBounds bounds = planet_surface_patch_bounds(config, patch.id);
    const float u_mid = (bounds.u0 + bounds.u1) * 0.5F;
    const float v_mid = (bounds.v0 + bounds.v1) * 0.5F;
    return {
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, v_mid),
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u0, bounds.v0),
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u1, bounds.v0),
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u0, bounds.v1),
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u1, bounds.v1),
    };
}

struct PatchBounds {
    cubey::math::DVec3 center_m{0.0, 0.0, 0.0};
    double radius_m = 0.0;
};

struct PatchGridSpan {
    std::uint32_t x0 = 0;
    std::uint32_t y0 = 0;
    std::uint32_t x1 = 0;
    std::uint32_t y1 = 0;
};

[[nodiscard]] float patch_cell_edge_m(const PlanetConfig& config,
                                      const PlanetSurfacePatchInstance& patch);

[[nodiscard]] std::uint32_t max_lod_grid_divisions(const PlanetConfig& config) {
    return config.patches_per_face << config.max_lod_level;
}

[[nodiscard]] float terrain_displacement_bound_m(const PlanetConfig& config) {
    return config.terrain_enabled ? std::max(config.terrain_height_scale_m, 0.0F) : 0.0F;
}

[[nodiscard]] PatchGridSpan patch_grid_span(const PlanetConfig& config,
                                            PlanetSurfacePatchId id) {
    const std::uint32_t total_divisions = max_lod_grid_divisions(config);
    const std::uint32_t patch_divisions = config.patches_per_face << id.level;
    const std::uint32_t scale = total_divisions / patch_divisions;
    return {
        .x0 = id.x * scale,
        .y0 = id.y * scale,
        .x1 = (id.x + 1U) * scale,
        .y1 = (id.y + 1U) * scale,
    };
}

[[nodiscard]] PatchBounds patch_bounds(const PlanetConfig& config,
                                       const PlanetSurfacePatchInstance& patch) {
    const std::array<cubey::math::DVec3, 5> samples = patch_sample_points(config, patch);
    PatchBounds bounds{
        .center_m = samples[0],
    };
    for (cubey::math::DVec3 sample : samples) {
        bounds.radius_m = std::max(bounds.radius_m, glm::length(sample - bounds.center_m));
    }
    bounds.radius_m += static_cast<double>(terrain_displacement_bound_m(config));
    return bounds;
}

class PlanetPatchSelectionSet {
  public:
    explicit PlanetPatchSelectionSet(std::span<const PlanetSurfacePatchInstance> patches) {
        selected_.reserve(patches.size());
        for (const PlanetSurfacePatchInstance& patch : patches) {
            selected_.insert(patch.id);
        }
    }

    [[nodiscard]] bool contains(PlanetSurfacePatchId id) const {
        return selected_.contains(id);
    }

  private:
    std::unordered_set<PlanetSurfacePatchId, PlanetSurfacePatchIdHash> selected_;
};

[[nodiscard]] std::optional<PlanetSurfacePatchId>
find_selected_patch_covering_cell(const PlanetConfig& config, const PlanetPatchSelectionSet& set,
                                  std::uint32_t face, std::uint32_t cell_x,
                                  std::uint32_t cell_y) {
    const std::uint32_t total_divisions = max_lod_grid_divisions(config);
    for (int level = static_cast<int>(config.max_lod_level); level >= 0; --level) {
        const auto level_u32 = static_cast<std::uint32_t>(level);
        const std::uint32_t divisions = config.patches_per_face << level_u32;
        const PlanetSurfacePatchId id{
            .face = face,
            .level = level_u32,
            .x = std::min((cell_x * divisions) / total_divisions, divisions - 1U),
            .y = std::min((cell_y * divisions) / total_divisions, divisions - 1U),
        };
        if (set.contains(id)) {
            return id;
        }
    }
    return std::nullopt;
}

struct NeighborEdgeProbe {
    bool boundary = false;
    bool found_neighbor = false;
    std::uint32_t max_delta = 0;
    std::uint32_t max_coarser_delta = 0;
    std::uint32_t max_finer_delta = 0;
};

[[nodiscard]] NeighborEdgeProbe analyze_neighbor_edge(const PlanetConfig& config,
                                                      const PlanetPatchSelectionSet& set,
                                                      PlanetSurfacePatchId id,
                                                      std::uint32_t edge_index) {
    const std::uint32_t total_divisions = max_lod_grid_divisions(config);
    const PatchGridSpan span = patch_grid_span(config, id);
    const bool left = edge_index == 0U;
    const bool right = edge_index == 1U;
    const bool bottom = edge_index == 2U;
    const bool top = edge_index == 3U;
    if ((left && span.x0 == 0U) || (right && span.x1 >= total_divisions) ||
        (bottom && span.y0 == 0U) || (top && span.y1 >= total_divisions)) {
        return {.boundary = true};
    }

    NeighborEdgeProbe probe{};
    const std::uint32_t edge_length = (left || right) ? (span.y1 - span.y0) : (span.x1 - span.x0);
    for (std::uint32_t sample = 1U; sample <= 3U; ++sample) {
        const std::uint32_t along =
            ((left || right) ? span.y0 : span.x0) + (edge_length * sample) / 4U;
        const std::uint32_t cell_x =
            left ? span.x0 - 1U : right ? span.x1 : std::min(along, total_divisions - 1U);
        const std::uint32_t cell_y =
            bottom ? span.y0 - 1U : top ? span.y1 : std::min(along, total_divisions - 1U);
        const std::optional<PlanetSurfacePatchId> neighbor =
            find_selected_patch_covering_cell(config, set, id.face, cell_x, cell_y);
        if (!neighbor.has_value()) {
            continue;
        }
        probe.found_neighbor = true;
        const std::uint32_t delta = id.level > neighbor->level ? id.level - neighbor->level
                                                               : neighbor->level - id.level;
        probe.max_delta = std::max(probe.max_delta, delta);
        if (id.level > neighbor->level) {
            probe.max_coarser_delta = std::max(probe.max_coarser_delta, id.level - neighbor->level);
        } else if (neighbor->level > id.level) {
            probe.max_finer_delta = std::max(probe.max_finer_delta, neighbor->level - id.level);
        }
    }
    return probe;
}

[[nodiscard]] std::uint32_t edge_transition_mask(const PlanetConfig& config,
                                                 const PlanetPatchSelectionSet& set,
                                                 PlanetSurfacePatchId id) {
    std::uint32_t mask = 0;
    for (std::uint32_t edge = 0; edge < 4U; ++edge) {
        const NeighborEdgeProbe probe = analyze_neighbor_edge(config, set, id, edge);
        if (probe.max_coarser_delta > 0U) {
            mask |= 1U << edge;
        }
    }
    return mask;
}

void update_neighbor_lod_diagnostics(PlanetSurfaceDiagnostics& diagnostics,
                                     PlanetSurfaceLodNeighborDiagnostics neighbor_diagnostics) {
    diagnostics.lod_neighbor_edge_count = neighbor_diagnostics.edge_count;
    diagnostics.lod_neighbor_boundary_edge_count = neighbor_diagnostics.boundary_edge_count;
    diagnostics.lod_neighbor_mismatch_edge_count = neighbor_diagnostics.mismatch_edge_count;
    diagnostics.max_lod_neighbor_delta = neighbor_diagnostics.max_lod_delta;
}

void reset_selected_patch_diagnostics(PlanetSurfaceDiagnostics& diagnostics) {
    diagnostics.visible_patch_count = 0;
    diagnostics.patch_count = 0;
    diagnostics.base_patch_count = 0;
    diagnostics.refined_patch_count = 0;
    diagnostics.transition_candidate_count = 0;
    diagnostics.min_lod_level = 0;
    diagnostics.max_lod_level = 0;
    diagnostics.patches_by_lod.fill(0U);
    diagnostics.min_screen_error_px = 0.0F;
    diagnostics.max_screen_error_px = 0.0F;
    diagnostics.max_transition_pressure = 0.0F;
    diagnostics.min_cell_edge_m_by_lod.fill(0.0F);
    diagnostics.max_cell_edge_m_by_lod.fill(0.0F);
}

void record_selected_patch_diagnostics(const PlanetConfig& config, PlanetSurfaceDiagnostics& diagnostics,
                                       const PlanetSurfacePatchInstance& patch) {
    diagnostics.min_lod_level = diagnostics.visible_patch_count == 0U
                                    ? patch.id.level
                                    : std::min(diagnostics.min_lod_level, patch.id.level);
    diagnostics.max_lod_level = std::max(diagnostics.max_lod_level, patch.id.level);
    if (patch.id.level < diagnostics.patches_by_lod.size()) {
        ++diagnostics.patches_by_lod[patch.id.level];
    }
    update_screen_error_range(diagnostics, patch.screen_error_px);
    update_lod_transition_diagnostics(config, diagnostics, patch.screen_error_px);
    update_lod_cell_edge_range(diagnostics, patch.id.level, patch_cell_edge_m(config, patch));
    if (patch.id.level == 0U) {
        ++diagnostics.base_patch_count;
    } else {
        ++diagnostics.refined_patch_count;
    }
    ++diagnostics.visible_patch_count;
    diagnostics.patch_count = diagnostics.visible_patch_count;
}

void refresh_selected_patch_diagnostics(const PlanetConfig& config, PlanetSurfacePatchPlan& plan) {
    reset_selected_patch_diagnostics(plan.diagnostics);
    for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
        record_selected_patch_diagnostics(config, plan.diagnostics, patch);
    }
}

[[nodiscard]] bool patch_passes_horizon_cull(const PlanetConfig& config, PlanetSurfaceView view,
                                             const PlanetSurfacePatchInstance& patch) {
    if (!view.culling_enabled) {
        return true;
    }
    const double camera_distance_m = glm::length(view.camera_world_position_m);
    if (camera_distance_m <= static_cast<double>(config.radius_m) * 1.001) {
        return true;
    }

    const double radius_m = static_cast<double>(config.radius_m);
    const double horizon_dot_m2 = radius_m * radius_m;
    const double conservative_margin_m2 = horizon_dot_m2 * 0.08;
    const PatchBounds bounds = patch_bounds(config, patch);
    const double max_patch_dot_m2 = glm::dot(bounds.center_m, view.camera_world_position_m) +
                                    bounds.radius_m * camera_distance_m;
    return max_patch_dot_m2 >= horizon_dot_m2 - conservative_margin_m2;
}

[[nodiscard]] cubey::math::Vec3 normalized_camera_forward(PlanetSurfaceView view) {
    if (glm::length(view.camera_forward_world) <= 0.0001F) {
        return {0.0F, 0.0F, -1.0F};
    }
    return glm::normalize(view.camera_forward_world);
}

[[nodiscard]] bool patch_passes_view_cull(const PlanetConfig& config, PlanetSurfaceView view,
                                          const PlanetSurfacePatchInstance& patch) {
    if (!view.culling_enabled) {
        return true;
    }
    const cubey::math::Vec3 forward = normalized_camera_forward(view);
    const float aspect = std::max(view.aspect_ratio, 0.001F);
    const float tan_half_vertical = std::tan(view.vertical_fov_radians * 0.5F);
    const float diagonal_half_angle =
        std::atan(tan_half_vertical * std::sqrt(1.0F + aspect * aspect));
    const float conservative_margin_radians = 0.22F;
    const PatchBounds bounds = patch_bounds(config, patch);
    const cubey::math::DVec3 to_center_d = bounds.center_m - view.camera_world_position_m;
    const double distance_m = glm::length(to_center_d);
    if (distance_m <= std::max(bounds.radius_m, 0.0001)) {
        return true;
    }
    const float angular_radius = static_cast<float>(
        std::asin(std::clamp(bounds.radius_m / std::max(distance_m, 0.0001), 0.0, 1.0)));
    const float cos_limit = std::cos(
        std::min(diagonal_half_angle + conservative_margin_radians + angular_radius, 3.0F));
    const cubey::math::Vec3 to_center{
        static_cast<float>(to_center_d.x / distance_m),
        static_cast<float>(to_center_d.y / distance_m),
        static_cast<float>(to_center_d.z / distance_m),
    };
    return glm::dot(forward, to_center) >= cos_limit;
}

[[nodiscard]] float patch_screen_error_px(const PlanetConfig& config, PlanetSurfaceView view,
                                          const PlanetSurfacePatchInstance& patch) {
    const PlanetSurfacePatchBounds bounds = planet_surface_patch_bounds(config, patch.id);
    const float u_mid = (bounds.u0 + bounds.u1) * 0.5F;
    const float v_mid = (bounds.v0 + bounds.v1) * 0.5F;
    const cubey::math::DVec3 center =
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, v_mid);
    const cubey::math::DVec3 edge_a =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u0, v_mid);
    const cubey::math::DVec3 edge_b =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u1, v_mid);
    const float patch_edge_m = static_cast<float>(glm::length(edge_b - edge_a));
    const float cell_edge_m = patch_edge_m / static_cast<float>(config.patch_resolution);
    const float displacement_bound_m = terrain_displacement_bound_m(config);
    const float distance_m =
        std::max(static_cast<float>(glm::length(center - view.camera_world_position_m)) -
                     displacement_bound_m,
                 1.0F);
    const float pixel_scale =
        view.viewport_height_px / (2.0F * std::tan(view.vertical_fov_radians * 0.5F));
    return (cell_edge_m / distance_m) * pixel_scale;
}

[[nodiscard]] float patch_cell_edge_m(const PlanetConfig& config,
                                      const PlanetSurfacePatchInstance& patch) {
    const PlanetSurfacePatchBounds bounds = planet_surface_patch_bounds(config, patch.id);
    const float u_mid = (bounds.u0 + bounds.u1) * 0.5F;
    const float v_mid = (bounds.v0 + bounds.v1) * 0.5F;
    const cubey::math::DVec3 edge_a =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u0, v_mid);
    const cubey::math::DVec3 edge_b =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u1, v_mid);
    const cubey::math::DVec3 edge_c =
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, bounds.v0);
    const cubey::math::DVec3 edge_d =
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, bounds.v1);
    const float horizontal_cell_m = static_cast<float>(glm::length(edge_b - edge_a)) /
                                    static_cast<float>(config.patch_resolution);
    const float vertical_cell_m = static_cast<float>(glm::length(edge_d - edge_c)) /
                                  static_cast<float>(config.patch_resolution);
    return std::max(horizontal_cell_m, vertical_cell_m);
}

void record_refinement_cull(PlanetSurfacePatchPlan& plan, bool horizon_culled) {
    if (horizon_culled) {
        ++plan.diagnostics.culled_horizon_count;
    } else {
        ++plan.diagnostics.culled_view_count;
    }
}

[[nodiscard]] float lod_refinement_threshold_px(const PlanetConfig& config,
                                                const PlanetPatchSelectionLookup& lookup,
                                                PlanetSurfacePatchId id) {
    if (lookup.was_refined(id)) {
        return config.lod_target_edge_px * (1.0F - config.lod_hysteresis);
    }
    if (lookup.was_selected(id)) {
        return config.lod_target_edge_px * (1.0F + config.lod_hysteresis);
    }
    return config.lod_target_edge_px;
}

[[nodiscard]] std::uint64_t root_patch_reserve(const PlanetConfig& config) {
    return static_cast<std::uint64_t>(config.patches_per_face) *
           static_cast<std::uint64_t>(config.patches_per_face) * 6ULL;
}

[[nodiscard]] bool can_refine_with_live_budget(const PlanetConfig& config,
                                               const PlanetSurfacePatchPlan& plan) {
    const std::uint64_t fallback_depth_reserve =
        4ULL * (static_cast<std::uint64_t>(config.max_lod_level) + 1ULL);
    return static_cast<std::uint64_t>(plan.selected_patches.size()) + root_patch_reserve(config) +
               fallback_depth_reserve + 4ULL <
           kPlanetMaxLivePatchInstances;
}

[[nodiscard]] bool record_visible_patch(const PlanetConfig& config, PlanetSurfacePatchPlan& plan,
                                        const PlanetSurfacePatchInstance& patch) {
    record_selected_patch_diagnostics(config, plan.diagnostics, patch);
    plan.selected_patches.push_back(patch);
    return plan.selected_patches.size() <= kPlanetMaxLivePatchInstances;
}

[[nodiscard]] bool append_coverage_patches(const PlanetConfig& config, PlanetSurfaceView view,
                                           const PlanetPatchSelectionLookup& lookup,
                                           PlanetSurfacePatchInstance patch,
                                           PlanetSurfacePatchPlan& plan) {
    patch.screen_error_px = patch_screen_error_px(config, view, patch);
    ++plan.diagnostics.planned_patch_count;

    const bool raw_wants_refinement =
        patch.id.level < config.max_lod_level && patch.screen_error_px > config.lod_target_edge_px;
    const float refinement_threshold_px = lod_refinement_threshold_px(config, lookup, patch.id);
    const bool wants_refinement =
        patch.id.level < config.max_lod_level && patch.screen_error_px > refinement_threshold_px;
    if (raw_wants_refinement && !wants_refinement) {
        ++plan.diagnostics.hysteresis_delayed_split_count;
    } else if (!raw_wants_refinement && wants_refinement) {
        ++plan.diagnostics.hysteresis_delayed_merge_count;
    }

    if (wants_refinement && !patch_passes_horizon_cull(config, view, patch)) {
        record_refinement_cull(plan, true);
        ++plan.diagnostics.refinement_fallback_patch_count;
        return record_visible_patch(config, plan, patch);
    }
    if (wants_refinement && !patch_passes_view_cull(config, view, patch)) {
        record_refinement_cull(plan, false);
        ++plan.diagnostics.refinement_fallback_patch_count;
        return record_visible_patch(config, plan, patch);
    }
    if (wants_refinement && !can_refine_with_live_budget(config, plan)) {
        ++plan.diagnostics.budget_fallback_patch_count;
        return record_visible_patch(config, plan, patch);
    }

    if (wants_refinement) {
        const std::size_t selected_patch_snapshot = plan.selected_patches.size();
        const PlanetSurfaceDiagnostics diagnostics_snapshot = plan.diagnostics;
        ++plan.diagnostics.subdivided_patch_count;
        const bool children_fit =
            append_coverage_patches(
                config, view, lookup,
                PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 0U)},
                plan) &&
            append_coverage_patches(
                config, view, lookup,
                PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 1U)},
                plan) &&
            append_coverage_patches(
                config, view, lookup,
                PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 2U)},
                plan) &&
            append_coverage_patches(
                config, view, lookup,
                PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 3U)},
                plan);
        if (!children_fit) {
            plan.selected_patches.resize(selected_patch_snapshot);
            plan.diagnostics = diagnostics_snapshot;
            ++plan.diagnostics.budget_fallback_patch_count;
            return record_visible_patch(config, plan, patch);
        }
        return true;
    }
    return record_visible_patch(config, plan, patch);
}

[[nodiscard]] PlanetSurfacePatchPlan
make_surface_patch_plan(const PlanetConfig& config, PlanetSurfaceView view,
                        PlanetSurfacePatchSelectionHints hints) {
    PlanetSurfacePatchPlan plan{};
    const PlanetPatchSelectionLookup lookup{hints.previous_selected_patches};
    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t py = 0; py < config.patches_per_face; ++py) {
            for (std::uint32_t px = 0; px < config.patches_per_face; ++px) {
                if (!append_coverage_patches(config, view, lookup,
                                             PlanetSurfacePatchInstance{
                                                 .id =
                                                     {
                                                         .face = face,
                                                         .level = 0,
                                                         .x = px,
                                                         .y = py,
                                                     },
                                             },
                                             plan)) {
                    throw std::runtime_error(
                        "planet live LOD selection exceeded the root patch budget");
                }
            }
        }
    }
    return plan;
}

[[nodiscard]] PlanetSurfacePatchInstance patch_instance_for_id(const PlanetConfig& config,
                                                               PlanetSurfaceView view,
                                                               PlanetSurfacePatchId id) {
    PlanetSurfacePatchInstance patch{.id = id};
    patch.screen_error_px = patch_screen_error_px(config, view, patch);
    return patch;
}

void replace_patch_with_children(const PlanetConfig& config, PlanetSurfaceView view,
                                 PlanetSurfacePatchPlan& plan, std::size_t patch_index) {
    const PlanetSurfacePatchId parent = plan.selected_patches.at(patch_index).id;
    std::array<PlanetSurfacePatchInstance, 4> children{};
    for (std::uint32_t child = 0; child < children.size(); ++child) {
        children[child] =
            patch_instance_for_id(config, view, planet_surface_child_patch_id(parent, child));
    }
    plan.selected_patches[patch_index] = children[0];
    plan.selected_patches.insert(plan.selected_patches.begin() + static_cast<std::ptrdiff_t>(patch_index) +
                                     1,
                                 children.begin() + 1, children.end());
    ++plan.diagnostics.lod_neighbor_repaired_split_count;
}

enum class NeighborRepairStep : std::uint8_t {
    Done,
    Split,
    NeedsBudget,
};

[[nodiscard]] NeighborRepairStep repair_neighbor_lod_once(const PlanetConfig& config,
                                                          PlanetSurfaceView view,
                                                          PlanetSurfacePatchPlan& plan) {
    const PlanetPatchSelectionSet set{plan.selected_patches};
    for (std::size_t index = 0; index < plan.selected_patches.size(); ++index) {
        const PlanetSurfacePatchId id = plan.selected_patches[index].id;
        if (id.level >= config.max_lod_level) {
            continue;
        }
        for (std::uint32_t edge = 0; edge < 4U; ++edge) {
            const NeighborEdgeProbe probe = analyze_neighbor_edge(config, set, id, edge);
            if (probe.max_finer_delta <= 1U) {
                continue;
            }
            if (plan.selected_patches.size() + 3U > kPlanetMaxLivePatchInstances) {
                return NeighborRepairStep::NeedsBudget;
            }
            replace_patch_with_children(config, view, plan, index);
            return NeighborRepairStep::Split;
        }
    }
    return NeighborRepairStep::Done;
}

[[nodiscard]] bool coarsen_selected_patches_once(const PlanetConfig& config, PlanetSurfaceView view,
                                                 PlanetSurfacePatchPlan& plan) {
    std::vector<PlanetSurfacePatchInstance> coarsened;
    coarsened.reserve(plan.selected_patches.size());
    std::unordered_set<PlanetSurfacePatchId, PlanetSurfacePatchIdHash> seen;
    seen.reserve(plan.selected_patches.size());
    bool changed = false;
    for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
        PlanetSurfacePatchId id = patch.id;
        if (id.level > 0U) {
            id = parent_patch_id(id);
            changed = true;
        }
        if (seen.insert(id).second) {
            coarsened.push_back(patch_instance_for_id(config, view, id));
        }
    }
    if (!changed) {
        return false;
    }
    plan.selected_patches = std::move(coarsened);
    ++plan.diagnostics.budget_fallback_patch_count;
    return true;
}

void repair_neighbor_lod_transitions(const PlanetConfig& config, PlanetSurfaceView view,
                                     PlanetSurfacePatchPlan& plan) {
    const std::size_t iteration_limit =
        std::max<std::size_t>(1U, plan.selected_patches.size() * (config.max_lod_level + 1U));
    for (std::size_t iteration = 0; iteration < iteration_limit; ++iteration) {
        const NeighborRepairStep step = repair_neighbor_lod_once(config, view, plan);
        if (step == NeighborRepairStep::Done) {
            refresh_selected_patch_diagnostics(config, plan);
            update_neighbor_lod_diagnostics(
                plan.diagnostics,
                analyze_planet_surface_lod_neighbors(config, plan.selected_patches));
            return;
        }
        if (step == NeighborRepairStep::NeedsBudget &&
            !coarsen_selected_patches_once(config, view, plan)) {
            break;
        }
    }

    refresh_selected_patch_diagnostics(config, plan);
    update_neighbor_lod_diagnostics(
        plan.diagnostics, analyze_planet_surface_lod_neighbors(config, plan.selected_patches));
    if (plan.diagnostics.max_lod_neighbor_delta > 1U) {
        throw std::runtime_error("planet LOD neighbor repair could not enforce single-step deltas");
    }
}

[[nodiscard]] cubey::math::Vec3
vertex_position(const cubey::render::VertexPositionColorNormalUv& vertex) {
    return {vertex.position[0], vertex.position[1], vertex.position[2]};
}

[[nodiscard]] cubey::math::Vec3
vertex_normal(const cubey::render::VertexPositionColorNormalUv& vertex) {
    return glm::normalize(cubey::math::Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]});
}

[[nodiscard]] float patch_skirt_depth_m(const PlanetConfig& config,
                                        const PlanetSurfacePatchInstance& patch) {
    const PlanetSurfacePatchBounds bounds = planet_surface_patch_bounds(config, patch.id);
    const float u_mid = (bounds.u0 + bounds.u1) * 0.5F;
    const float v_mid = (bounds.v0 + bounds.v1) * 0.5F;
    const cubey::math::DVec3 edge_a =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u0, v_mid);
    const cubey::math::DVec3 edge_b =
        planet_surface_sphere_world_position_m(config, patch.id.face, bounds.u1, v_mid);
    const cubey::math::DVec3 edge_c =
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, bounds.v0);
    const cubey::math::DVec3 edge_d =
        planet_surface_sphere_world_position_m(config, patch.id.face, u_mid, bounds.v1);
    const float horizontal_cell_m = static_cast<float>(glm::length(edge_b - edge_a)) /
                                    static_cast<float>(config.patch_resolution);
    const float vertical_cell_m = static_cast<float>(glm::length(edge_d - edge_c)) /
                                  static_cast<float>(config.patch_resolution);
    return std::max(std::min(horizontal_cell_m, vertical_cell_m) * config.skirt_depth_scale,
                    config.radius_m * 0.00001F);
}

void update_skirt_depth_range(PlanetSurfaceDiagnostics& diagnostics, float depth_m) {
    if (diagnostics.skirt_triangle_count == 0U || diagnostics.min_skirt_depth_m == 0.0F) {
        diagnostics.min_skirt_depth_m = depth_m;
    } else {
        diagnostics.min_skirt_depth_m = std::min(diagnostics.min_skirt_depth_m, depth_m);
    }
    diagnostics.max_skirt_depth_m = std::max(diagnostics.max_skirt_depth_m, depth_m);
}

[[nodiscard]] std::uint32_t append_skirt_vertex(const PlanetConfig& config,
                                                const PlanetFrame& frame,
                                                PlanetSurfaceBuildResult& result,
                                                std::uint32_t top_index, float depth_m) {
    const cubey::render::VertexPositionColorNormalUv& top = result.mesh.vertices[top_index];
    const cubey::math::Vec3 normal = vertex_normal(top);
    const cubey::math::DVec3 top_world =
        planet_frame_render_to_world_m(frame, vertex_position(top));
    const cubey::math::DVec3 bottom_world =
        top_world - cubey::math::DVec3{normal.x, normal.y, normal.z} * static_cast<double>(depth_m);
    const cubey::math::Vec3 bottom_render = planet_frame_world_to_render_m(frame, bottom_world);
    const std::uint32_t bottom_index = static_cast<std::uint32_t>(result.mesh.vertices.size());
    result.mesh.vertices.push_back(VertexPositionColorNormalUv{
        .position = to_primitive(bottom_render),
        .color = skirt_color(config, normal),
        .normal = to_primitive(normal),
        .uv = top.uv,
    });
    return bottom_index;
}

void append_skirt_segment(const PlanetConfig& config, const PlanetFrame& frame,
                          PlanetSurfaceBuildResult& result, std::uint32_t top0, std::uint32_t top1,
                          float depth_m) {
    const std::uint32_t bottom0 = append_skirt_vertex(config, frame, result, top0, depth_m);
    const std::uint32_t bottom1 = append_skirt_vertex(config, frame, result, top1, depth_m);
    const auto push_triangle = [&result](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        result.mesh.indices.push_back(a);
        result.mesh.indices.push_back(b);
        result.mesh.indices.push_back(c);
    };

    push_triangle(top0, bottom0, top1);
    push_triangle(top1, bottom0, bottom1);
    push_triangle(top1, bottom0, top0);
    push_triangle(bottom1, bottom0, top1);
    result.diagnostics.skirt_triangle_count += 4U;
}

void append_patch_skirts(const PlanetConfig& config, const PlanetFrame& frame,
                         const PlanetSurfacePatchInstance& patch, std::uint32_t base_vertex,
                         std::uint32_t vertices_per_side, PlanetSurfaceBuildResult& result) {
    if (!config.skirts_enabled) {
        return;
    }

    const float depth_m = patch_skirt_depth_m(config, patch);
    update_skirt_depth_range(result.diagnostics, depth_m);
    result.diagnostics.seam_edge_count += 4U;

    const std::uint32_t patch_resolution = config.patch_resolution;
    for (std::uint32_t x = 0; x < patch_resolution; ++x) {
        append_skirt_segment(config, frame, result, base_vertex + x, base_vertex + x + 1U, depth_m);
        const std::uint32_t bottom_row = base_vertex + patch_resolution * vertices_per_side;
        append_skirt_segment(config, frame, result, bottom_row + x, bottom_row + x + 1U, depth_m);
    }
    for (std::uint32_t y = 0; y < patch_resolution; ++y) {
        append_skirt_segment(config, frame, result, base_vertex + y * vertices_per_side,
                             base_vertex + (y + 1U) * vertices_per_side, depth_m);
        append_skirt_segment(
            config, frame, result, base_vertex + y * vertices_per_side + patch_resolution,
            base_vertex + (y + 1U) * vertices_per_side + patch_resolution, depth_m);
    }
}

void append_patch_mesh(const PlanetConfig& config, const PlanetFrame& frame,
                       const PlanetSurfacePatchInstance& patch, PlanetSurfaceBuildResult& result) {
    const std::uint32_t patch_resolution = config.patch_resolution;
    const std::uint32_t vertices_per_side = patch_resolution + 1U;
    const std::uint32_t base_vertex = static_cast<std::uint32_t>(result.mesh.vertices.size());
    const PlanetSurfacePatchBounds bounds = planet_surface_patch_bounds(config, patch.id);

    for (std::uint32_t y = 0; y <= patch_resolution; ++y) {
        const float tv = static_cast<float>(y) / static_cast<float>(patch_resolution);
        const float v = bounds.v0 + (bounds.v1 - bounds.v0) * tv;
        for (std::uint32_t x = 0; x <= patch_resolution; ++x) {
            const float tu = static_cast<float>(x) / static_cast<float>(patch_resolution);
            const float u = bounds.u0 + (bounds.u1 - bounds.u0) * tu;
            const PlanetSurfaceSample sample = planet_surface_sample_field(config, patch.id, u, v);
            const cubey::math::Vec3 render_position =
                planet_frame_world_to_render_m(frame, sample.world_position_m);
            result.mesh.vertices.push_back(VertexPositionColorNormalUv{
                .position = to_primitive(render_position),
                .color = vertex_color(config, patch, sample),
                .normal = to_primitive(sample.normal),
                .uv = PrimitiveVec2{tu, tv},
            });
        }
    }

    for (std::uint32_t y = 0; y < patch_resolution; ++y) {
        for (std::uint32_t x = 0; x < patch_resolution; ++x) {
            const std::uint32_t i0 = base_vertex + y * vertices_per_side + x;
            const std::uint32_t i1 = i0 + 1U;
            const std::uint32_t i2 = i0 + vertices_per_side;
            const std::uint32_t i3 = i2 + 1U;
            result.mesh.indices.push_back(i0);
            result.mesh.indices.push_back(i1);
            result.mesh.indices.push_back(i2);
            result.mesh.indices.push_back(i1);
            result.mesh.indices.push_back(i3);
            result.mesh.indices.push_back(i2);

            const cubey::math::DVec3 w0 = planet_frame_render_to_world_m(
                frame, {result.mesh.vertices[i0].position[0], result.mesh.vertices[i0].position[1],
                        result.mesh.vertices[i0].position[2]});
            const cubey::math::DVec3 w1 = planet_frame_render_to_world_m(
                frame, {result.mesh.vertices[i1].position[0], result.mesh.vertices[i1].position[1],
                        result.mesh.vertices[i1].position[2]});
            const cubey::math::DVec3 w2 = planet_frame_render_to_world_m(
                frame, {result.mesh.vertices[i2].position[0], result.mesh.vertices[i2].position[1],
                        result.mesh.vertices[i2].position[2]});
            update_edge_range(result.diagnostics, w0, w1);
            update_edge_range(result.diagnostics, w0, w2);
        }
    }

    append_patch_skirts(config, frame, patch, base_vertex, vertices_per_side, result);
}

void validate_planet_cpu_debug_mesh_config(const PlanetConfig& config) {
    validate_planet_config(config);

    std::uint64_t patch_multiplier = 1;
    for (std::uint32_t level = 0; level < config.max_lod_level; ++level) {
        patch_multiplier *= 4ULL;
    }
    const std::uint64_t worst_case_vertices =
        6ULL * static_cast<std::uint64_t>(config.patches_per_face) *
        static_cast<std::uint64_t>(config.patches_per_face) * patch_multiplier *
        (static_cast<std::uint64_t>(config.patch_resolution + 1U) *
             static_cast<std::uint64_t>(config.patch_resolution + 1U) +
         8ULL * static_cast<std::uint64_t>(config.patch_resolution));
    if (worst_case_vertices > kPlanetCpuMeshVertexCap) {
        throw std::runtime_error("planet surface LOD settings are too dense for CPU debug mesh");
    }
}

} // namespace

PlanetSurfacePatchBounds planet_surface_patch_bounds(const PlanetConfig& config,
                                                     PlanetSurfacePatchId id) {
    validate_planet_config(config);
    if (id.face >= 6U) {
        throw std::runtime_error("planet surface patch face must be < 6");
    }
    if (id.level > config.max_lod_level) {
        throw std::runtime_error("planet surface patch level exceeds max LOD");
    }
    const std::uint32_t divisions_per_face = config.patches_per_face << id.level;
    if (id.x >= divisions_per_face || id.y >= divisions_per_face) {
        throw std::runtime_error("planet surface patch coordinates exceed level divisions");
    }
    const float inv_divisions = 1.0F / static_cast<float>(divisions_per_face);
    return {
        .u0 = -1.0F + 2.0F * static_cast<float>(id.x) * inv_divisions,
        .v0 = -1.0F + 2.0F * static_cast<float>(id.y) * inv_divisions,
        .u1 = -1.0F + 2.0F * static_cast<float>(id.x + 1U) * inv_divisions,
        .v1 = -1.0F + 2.0F * static_cast<float>(id.y + 1U) * inv_divisions,
    };
}

PlanetSurfacePatchId planet_surface_child_patch_id(PlanetSurfacePatchId id,
                                                   std::uint32_t child_index) {
    if (child_index >= 4U) {
        throw std::runtime_error("planet surface child patch index must be < 4");
    }
    return {
        .face = id.face,
        .level = id.level + 1U,
        .x = id.x * 2U + (child_index & 1U),
        .y = id.y * 2U + (child_index >> 1U),
    };
}

float planet_surface_nominal_cell_edge_m(const PlanetConfig& config, std::uint32_t lod_level) {
    validate_planet_config(config);
    if (lod_level > config.max_lod_level || lod_level > kPlanetMaxLiveLodLevel) {
        throw std::runtime_error("planet nominal cell edge LOD is out of range");
    }
    return patch_cell_edge_m(config, PlanetSurfacePatchInstance{
                                         .id = PlanetSurfacePatchId{
                                             .face = 0,
                                             .level = lod_level,
                                             .x = 0,
                                             .y = 0,
                                         },
                                     });
}

PlanetSurfaceLodNeighborDiagnostics
analyze_planet_surface_lod_neighbors(const PlanetConfig& config,
                                     std::span<const PlanetSurfacePatchInstance> patches) {
    validate_planet_config(config);
    PlanetSurfaceLodNeighborDiagnostics diagnostics{};
    const PlanetPatchSelectionSet set{patches};
    for (const PlanetSurfacePatchInstance& patch : patches) {
        for (std::uint32_t edge = 0; edge < 4U; ++edge) {
            const NeighborEdgeProbe probe = analyze_neighbor_edge(config, set, patch.id, edge);
            if (probe.boundary) {
                ++diagnostics.boundary_edge_count;
                continue;
            }
            if (!probe.found_neighbor) {
                continue;
            }
            ++diagnostics.edge_count;
            diagnostics.max_lod_delta = std::max(diagnostics.max_lod_delta, probe.max_delta);
            if (probe.max_delta > 0U) {
                ++diagnostics.mismatch_edge_count;
            }
        }
    }
    return diagnostics;
}

PlanetSurfacePatchPlan plan_planet_surface_patches(const PlanetConfig& config,
                                                   PlanetSurfaceView view,
                                                   PlanetSurfacePatchSelectionHints hints) {
    validate_planet_config(config);
    PlanetSurfacePatchPlan plan = make_surface_patch_plan(config, view, hints);
    if (plan.selected_patches.size() > kPlanetMaxLivePatchInstances) {
        throw std::runtime_error("planet live LOD fallback exceeded the patch instance budget");
    }
    repair_neighbor_lod_transitions(config, view, plan);
    return plan;
}

PlanetPatchGridMeshData make_planet_patch_grid_mesh(const PlanetConfig& config) {
    validate_planet_config(config);

    PlanetPatchGridMeshData mesh;
    const std::uint32_t resolution = config.patch_resolution;
    const std::uint32_t vertices_per_side = resolution + 1U;
    mesh.vertices.reserve(static_cast<std::size_t>(vertices_per_side) * vertices_per_side);
    mesh.indices.reserve(static_cast<std::size_t>(resolution) * resolution * 6U);

    for (std::uint32_t y = 0; y <= resolution; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(resolution);
        for (std::uint32_t x = 0; x <= resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(resolution);
            mesh.vertices.push_back(PlanetPatchGridVertex{
                .uv = PrimitiveVec2{u, v},
            });
        }
    }

    for (std::uint32_t y = 0; y < resolution; ++y) {
        for (std::uint32_t x = 0; x < resolution; ++x) {
            const std::uint32_t i0 = y * vertices_per_side + x;
            const std::uint32_t i1 = i0 + 1U;
            const std::uint32_t i2 = i0 + vertices_per_side;
            const std::uint32_t i3 = i2 + 1U;
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i3);
            mesh.indices.push_back(i2);
        }
    }

    if (config.skirts_enabled) {
        const auto append_skirt_segment = [&mesh](std::uint32_t top0, std::uint32_t top1) {
            const std::uint32_t bottom0 = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(PlanetPatchGridVertex{
                .uv = mesh.vertices[top0].uv,
                .skirt = 1.0F,
            });
            const std::uint32_t bottom1 = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(PlanetPatchGridVertex{
                .uv = mesh.vertices[top1].uv,
                .skirt = 1.0F,
            });

            const auto push_triangle = [&mesh](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                mesh.indices.push_back(a);
                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
            };
            push_triangle(top0, bottom0, top1);
            push_triangle(top1, bottom0, bottom1);
            push_triangle(top1, bottom0, top0);
            push_triangle(bottom1, bottom0, top1);
        };

        for (std::uint32_t x = 0; x < resolution; ++x) {
            append_skirt_segment(x, x + 1U);
            const std::uint32_t bottom_row = resolution * vertices_per_side;
            append_skirt_segment(bottom_row + x, bottom_row + x + 1U);
        }
        for (std::uint32_t y = 0; y < resolution; ++y) {
            append_skirt_segment(y * vertices_per_side, (y + 1U) * vertices_per_side);
            append_skirt_segment(y * vertices_per_side + resolution,
                                 (y + 1U) * vertices_per_side + resolution);
        }
    }

    return mesh;
}

std::vector<PlanetSurfaceGpuPatchInstance>
make_planet_surface_gpu_patch_instances(const PlanetConfig& config,
                                        const PlanetSurfacePatchPlan& plan) {
    std::vector<PlanetSurfaceGpuPatchInstance> instances;
    instances.reserve(plan.selected_patches.size());
    const PlanetPatchSelectionSet set{plan.selected_patches};
    for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
        instances.push_back({
            .face = patch.id.face,
            .level = patch.id.level,
            .x = patch.id.x,
            .y = patch.id.y,
            .edge_transition_mask = edge_transition_mask(config, set, patch.id),
            .screen_error_px = patch.screen_error_px,
        });
    }
    return instances;
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config) {
    validate_planet_cpu_debug_mesh_config(config);
    const float camera_distance =
        std::max(config.radius_m + config.camera_altitude_m, config.radius_m * 1.01F);
    const PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, camera_distance},
    };
    return make_planet_surface_mesh(config, view);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view) {
    validate_planet_cpu_debug_mesh_config(config);
    PlanetFrame frame{};
    frame.planet_radius_m = config.radius_m;
    frame.camera_world_position_m = view.camera_world_position_m;
    frame.render_origin_world_m = {0.0, 0.0, 0.0};
    return make_planet_surface_mesh(config, view, frame);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view,
                                                  const PlanetFrame& frame) {
    validate_planet_cpu_debug_mesh_config(config);
    const PlanetSurfacePatchPlan plan = plan_planet_surface_patches(config, view);
    return make_planet_surface_mesh(config, view, frame, plan);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view, const PlanetFrame& frame,
                                                  const PlanetSurfacePatchPlan& plan) {
    validate_planet_cpu_debug_mesh_config(config);
    (void)view;

    PlanetSurfaceBuildResult result{};
    result.diagnostics = plan.diagnostics;
    for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
        append_patch_mesh(config, frame, patch, result);
    }

    result.diagnostics.vertex_count = static_cast<std::uint32_t>(result.mesh.vertices.size());
    result.diagnostics.triangle_count = static_cast<std::uint32_t>(result.mesh.indices.size() / 3U);
    if (plan.selected_patches.empty()) {
        result.diagnostics.min_lod_level = 0;
    }
    return result;
}

} // namespace cubey::projects::planet
