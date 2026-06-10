#include "planet_surface.h"

#include "planet_surface_field.h"

#include <cubey/core/math.h>
#include <cubey/render/adaptive_patch_lod.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
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

[[nodiscard]] float effective_lod_target_edge_px(const PlanetConfig& config,
                                                 PlanetSurfaceView view) {
    if (std::isfinite(view.lod_target_edge_px) && view.lod_target_edge_px > 0.0F) {
        return view.lod_target_edge_px;
    }
    return config.lod_target_edge_px;
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

[[nodiscard]] PrimitiveVec3 terrain_band_color(const PlanetConfig& config, float band_m) {
    const float t =
        std::clamp(band_m / std::max(config.terrain_height_scale_m * 0.55F, 1.0F), -1.0F,
                   1.0F) *
            0.5F +
        0.5F;
    const PrimitiveVec3 low{0.12F, 0.18F, 0.48F};
    const PrimitiveVec3 mid{0.10F, 0.12F, 0.14F};
    const PrimitiveVec3 high{0.92F, 0.64F, 0.18F};
    if (t < 0.5F) {
        const float blend = t * 2.0F;
        return {
            lerp(low[0], mid[0], blend),
            lerp(low[1], mid[1], blend),
            lerp(low[2], mid[2], blend),
        };
    }
    const float blend = (t - 0.5F) * 2.0F;
    return {
        lerp(mid[0], high[0], blend),
        lerp(mid[1], high[1], blend),
        lerp(mid[2], high[2], blend),
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
    case PlanetDebugView::LocalDetailLod:
        return {0.48F, 0.34F, 0.86F};
    case PlanetDebugView::LocalDetailHeight:
        return terrain_height_color(config, sample.height_m);
    case PlanetDebugView::LocalDetailFeatures:
        return {0.72F, 0.38F, 0.24F};
    case PlanetDebugView::LocalDetailFinal:
        return final_color(config, sample);
    case PlanetDebugView::TerrainBandBase:
        return terrain_height_color(config, sample.terrain_bands.base_shape_m);
    case PlanetDebugView::TerrainBandRelief:
        return terrain_band_color(config, sample.terrain_bands.broad_relief_m);
    case PlanetDebugView::TerrainBandDetail:
        return terrain_band_color(config,
                                  sample.terrain_bands.mid_detail_m +
                                      sample.terrain_bands.fine_detail_m);
    case PlanetDebugView::LocalDetailHorizon:
        return final_color(config, sample);
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

[[nodiscard]] float patch_cell_edge_m(const PlanetConfig& config,
                                      const PlanetSurfacePatchInstance& patch);

[[nodiscard]] float terrain_displacement_bound_m(const PlanetConfig& config) {
    return config.terrain_enabled ? std::max(config.terrain_height_scale_m, 0.0F) : 0.0F;
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

[[nodiscard]] cubey::render::AdaptivePatchLodPatchId
to_adaptive_patch_id(PlanetSurfacePatchId id) {
    return {
        .root = id.face,
        .level = id.level,
        .x = id.x,
        .y = id.y,
    };
}

[[nodiscard]] PlanetSurfacePatchId
from_adaptive_patch_id(cubey::render::AdaptivePatchLodPatchId id) {
    return {
        .face = id.root,
        .level = id.level,
        .x = id.x,
        .y = id.y,
    };
}

[[nodiscard]] cubey::render::AdaptivePatchLodConfig
planet_adaptive_lod_config(const PlanetConfig& config, float target_error_px) {
    return {
        .root_count = 6,
        .root_divisions_per_axis = config.patches_per_face,
        .max_lod_level = config.max_lod_level,
        .max_selected_patches = kPlanetMaxLivePatchInstances,
        .target_error_px = target_error_px,
        .hysteresis = config.lod_hysteresis,
    };
}

[[nodiscard]] cubey::render::AdaptivePatchLodConfig
planet_adaptive_lod_config(const PlanetConfig& config) {
    return planet_adaptive_lod_config(config, config.lod_target_edge_px);
}

[[nodiscard]] cubey::render::AdaptivePatchLodPatchInstance
to_adaptive_patch_instance(const PlanetSurfacePatchInstance& patch) {
    return {
        .id = to_adaptive_patch_id(patch.id),
        .screen_error_px = patch.screen_error_px,
    };
}

[[nodiscard]] PlanetSurfacePatchInstance
from_adaptive_patch_instance(const cubey::render::AdaptivePatchLodPatchInstance& patch) {
    return {
        .id = from_adaptive_patch_id(patch.id),
        .screen_error_px = patch.screen_error_px,
    };
}

[[nodiscard]] std::vector<cubey::render::AdaptivePatchLodPatchInstance>
to_adaptive_patch_instances(std::span<const PlanetSurfacePatchInstance> patches) {
    std::vector<cubey::render::AdaptivePatchLodPatchInstance> result;
    result.reserve(patches.size());
    for (const PlanetSurfacePatchInstance& patch : patches) {
        result.push_back(to_adaptive_patch_instance(patch));
    }
    return result;
}

[[nodiscard]] std::vector<cubey::render::AdaptivePatchLodPatchId>
to_adaptive_patch_ids(std::span<const PlanetSurfacePatchId> ids) {
    std::vector<cubey::render::AdaptivePatchLodPatchId> result;
    result.reserve(ids.size());
    for (PlanetSurfacePatchId id : ids) {
        result.push_back(to_adaptive_patch_id(id));
    }
    return result;
}

[[nodiscard]] PlanetSurfaceDiagnostics
planet_diagnostics_from_adaptive_lod(
    const PlanetConfig& config,
    const cubey::render::AdaptivePatchLodDiagnostics& diagnostics,
    std::span<const PlanetSurfacePatchInstance> selected_patches) {
    PlanetSurfaceDiagnostics result{};
    result.planned_patch_count = diagnostics.planned_patch_count;
    result.visible_patch_count = diagnostics.visible_patch_count;
    result.culled_horizon_count = diagnostics.culled_horizon_count;
    result.culled_view_count = diagnostics.culled_view_count;
    result.patch_count = diagnostics.patch_count;
    result.base_patch_count = diagnostics.base_patch_count;
    result.refined_patch_count = diagnostics.refined_patch_count;
    result.subdivided_patch_count = diagnostics.subdivided_patch_count;
    result.refinement_fallback_patch_count = diagnostics.refinement_fallback_patch_count;
    result.budget_fallback_patch_count = diagnostics.budget_fallback_patch_count;
    result.hysteresis_delayed_split_count = diagnostics.hysteresis_delayed_split_count;
    result.hysteresis_delayed_merge_count = diagnostics.hysteresis_delayed_merge_count;
    result.transition_candidate_count = diagnostics.transition_candidate_count;
    result.lod_neighbor_edge_count = diagnostics.lod_neighbor_edge_count;
    result.lod_neighbor_boundary_edge_count = diagnostics.lod_neighbor_boundary_edge_count;
    result.lod_neighbor_mismatch_edge_count = diagnostics.lod_neighbor_mismatch_edge_count;
    result.max_lod_neighbor_delta = diagnostics.max_lod_neighbor_delta;
    result.lod_neighbor_repaired_split_count = diagnostics.lod_neighbor_repaired_split_count;
    result.min_lod_level = diagnostics.min_lod_level;
    result.max_lod_level = diagnostics.max_lod_level;
    result.min_screen_error_px = diagnostics.min_screen_error_px;
    result.max_screen_error_px = diagnostics.max_screen_error_px;
    result.max_transition_pressure = diagnostics.max_transition_pressure;
    for (std::size_t level = 0;
         level < std::min(result.patches_by_lod.size(), diagnostics.patches_by_lod.size());
         ++level) {
        result.patches_by_lod[level] = diagnostics.patches_by_lod[level];
    }
    for (const PlanetSurfacePatchInstance& patch : selected_patches) {
        update_lod_cell_edge_range(result, patch.id.level, patch_cell_edge_m(config, patch));
    }
    return result;
}

[[nodiscard]] PlanetSurfacePatchPlan
planet_plan_from_adaptive_lod(const PlanetConfig& config,
                              const cubey::render::AdaptivePatchLodPlan& adaptive_plan) {
    PlanetSurfacePatchPlan plan{};
    plan.selected_patches.reserve(adaptive_plan.selected_patches.size());
    for (const cubey::render::AdaptivePatchLodPatchInstance& patch :
         adaptive_plan.selected_patches) {
        plan.selected_patches.push_back(from_adaptive_patch_instance(patch));
    }
    plan.diagnostics =
        planet_diagnostics_from_adaptive_lod(config, adaptive_plan.diagnostics,
                                             plan.selected_patches);
    return plan;
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
    const std::vector<cubey::render::AdaptivePatchLodPatchInstance> adaptive_patches =
        to_adaptive_patch_instances(patches);
    const cubey::render::AdaptivePatchLodNeighborDiagnostics diagnostics =
        cubey::render::analyze_adaptive_patch_lod_neighbors(
            planet_adaptive_lod_config(config), adaptive_patches);
    return {
        .edge_count = diagnostics.edge_count,
        .boundary_edge_count = diagnostics.boundary_edge_count,
        .mismatch_edge_count = diagnostics.mismatch_edge_count,
        .max_lod_delta = diagnostics.max_lod_delta,
    };
}

PlanetSurfacePatchPlan plan_planet_surface_patches(const PlanetConfig& config,
                                                   PlanetSurfaceView view,
                                                   PlanetSurfacePatchSelectionHints hints) {
    validate_planet_config(config);
    const std::vector<cubey::render::AdaptivePatchLodPatchId> previous_selection =
        to_adaptive_patch_ids(hints.previous_selected_patches);
    const cubey::render::AdaptivePatchLodConfig adaptive_config =
        planet_adaptive_lod_config(config, effective_lod_target_edge_px(config, view));
    const cubey::render::AdaptivePatchLodPlan adaptive_plan =
        cubey::render::plan_adaptive_patch_lod(
            adaptive_config,
            {
                .screen_error_px =
                    [&config, view](cubey::render::AdaptivePatchLodPatchId id) {
                        const PlanetSurfacePatchId patch_id = from_adaptive_patch_id(id);
                        return patch_screen_error_px(config, view,
                                                     PlanetSurfacePatchInstance{.id = patch_id});
                    },
                .refinement_cull =
                    [&config, view](
                        const cubey::render::AdaptivePatchLodPatchInstance& patch) {
                        const PlanetSurfacePatchInstance planet_patch =
                            from_adaptive_patch_instance(patch);
                        if (!patch_passes_horizon_cull(config, view, planet_patch)) {
                            return cubey::render::AdaptivePatchLodCullResult::HorizonCulled;
                        }
                        if (!patch_passes_view_cull(config, view, planet_patch)) {
                            return cubey::render::AdaptivePatchLodCullResult::ViewCulled;
                        }
                        return cubey::render::AdaptivePatchLodCullResult::Visible;
                    },
            },
            cubey::render::AdaptivePatchLodSelectionHints{
                .previous_selected_patches = previous_selection,
            });
    return planet_plan_from_adaptive_lod(config, adaptive_plan);
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
    const std::vector<cubey::render::AdaptivePatchLodPatchInstance> adaptive_patches =
        to_adaptive_patch_instances(plan.selected_patches);
    const cubey::render::AdaptivePatchLodConfig adaptive_config =
        planet_adaptive_lod_config(config);
    for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
        instances.push_back({
            .face = patch.id.face,
            .level = patch.id.level,
            .x = patch.id.x,
            .y = patch.id.y,
            .edge_transition_mask = cubey::render::adaptive_patch_lod_edge_transition_mask(
                adaptive_config, adaptive_patches, to_adaptive_patch_id(patch.id)),
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
