#include "planet_surface.h"

#include <cubey/core/math.h>

#include <algorithm>
#include <array>
#include <cmath>
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

[[nodiscard]] cubey::math::Vec3 cube_face_point(std::uint32_t face, float u, float v) {
    switch (face) {
    case 0:
        return {1.0F, v, -u};
    case 1:
        return {-1.0F, v, u};
    case 2:
        return {u, 1.0F, -v};
    case 3:
        return {u, -1.0F, v};
    case 4:
        return {u, v, 1.0F};
    case 5:
        return {-u, v, -1.0F};
    default:
        return {0.0F, 1.0F, 0.0F};
    }
}

[[nodiscard]] cubey::math::DVec3 sphere_world_position(const PlanetConfig& config,
                                                       std::uint32_t face, float u, float v) {
    const cubey::math::Vec3 normal = glm::normalize(cube_face_point(face, u, v));
    return {
        static_cast<double>(normal.x) * static_cast<double>(config.radius_m),
        static_cast<double>(normal.y) * static_cast<double>(config.radius_m),
        static_cast<double>(normal.z) * static_cast<double>(config.radius_m),
    };
}

[[nodiscard]] float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] float smootherstep(float value) {
    const float x = std::clamp(value, 0.0F, 1.0F);
    return x * x * x * (x * (x * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::int32_t z,
                                     std::uint32_t seed) {
    std::uint32_t value = seed;
    value ^= static_cast<std::uint32_t>(x) * 0x9e3779b9U;
    value ^= static_cast<std::uint32_t>(y) * 0x85ebca6bU;
    value ^= static_cast<std::uint32_t>(z) * 0xc2b2ae35U;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float hash01(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    constexpr float kInv24Bit = 1.0F / 16777215.0F;
    return static_cast<float>(hash_u32(x, y, z, seed) >> 8U) * kInv24Bit;
}

[[nodiscard]] float value_noise(cubey::math::Vec3 p, std::uint32_t seed) {
    const auto x0 = static_cast<std::int32_t>(std::floor(p.x));
    const auto y0 = static_cast<std::int32_t>(std::floor(p.y));
    const auto z0 = static_cast<std::int32_t>(std::floor(p.z));
    const float tx = smootherstep(p.x - static_cast<float>(x0));
    const float ty = smootherstep(p.y - static_cast<float>(y0));
    const float tz = smootherstep(p.z - static_cast<float>(z0));

    const auto lattice = [seed](std::int32_t x, std::int32_t y, std::int32_t z) {
        return hash01(x, y, z, seed) * 2.0F - 1.0F;
    };
    const float x00 = lerp(lattice(x0, y0, z0), lattice(x0 + 1, y0, z0), tx);
    const float x10 = lerp(lattice(x0, y0 + 1, z0), lattice(x0 + 1, y0 + 1, z0), tx);
    const float x01 = lerp(lattice(x0, y0, z0 + 1), lattice(x0 + 1, y0, z0 + 1), tx);
    const float x11 = lerp(lattice(x0, y0 + 1, z0 + 1), lattice(x0 + 1, y0 + 1, z0 + 1), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

[[nodiscard]] float fbm(cubey::math::Vec3 p, std::uint32_t seed, std::uint32_t octaves) {
    float amplitude = 0.5F;
    float frequency = 1.0F;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += value_noise(p * frequency, seed + octave * 1013U) * amplitude;
        weight += amplitude;
        frequency *= 2.03F;
        amplitude *= 0.5F;
    }
    return weight > 0.0F ? sum / weight : 0.0F;
}

[[nodiscard]] float terrain_height_m(const PlanetConfig& config, cubey::math::Vec3 normal) {
    if (!config.terrain_enabled || config.terrain_height_scale_m <= 0.0F) {
        return 0.0F;
    }
    const cubey::math::Vec3 p = normal * config.terrain_noise_scale;
    const float broad = fbm(p + cubey::math::Vec3{1.7F, -3.2F, 5.1F}, config.terrain_seed, 4U);
    const float ridge_source =
        fbm(p * 1.85F + cubey::math::Vec3{-4.0F, 2.4F, 8.5F}, config.terrain_seed + 37U, 4U);
    const float ridged = 1.0F - std::abs(ridge_source);
    const float height = (broad * 0.68F + (ridged - 0.48F) * 0.64F) * config.terrain_height_scale_m;
    return std::clamp(height, -config.terrain_height_scale_m, config.terrain_height_scale_m);
}

[[nodiscard]] cubey::math::DVec3 terrain_world_position(const PlanetConfig& config,
                                                        std::uint32_t face, float u, float v) {
    const cubey::math::Vec3 normal = glm::normalize(cube_face_point(face, u, v));
    const double radius = static_cast<double>(config.radius_m) +
                          static_cast<double>(terrain_height_m(config, normal));
    return {
        static_cast<double>(normal.x) * radius,
        static_cast<double>(normal.y) * radius,
        static_cast<double>(normal.z) * radius,
    };
}

[[nodiscard]] cubey::math::Vec3 terrain_normal(const PlanetConfig& config, std::uint32_t face,
                                               float u, float v) {
    const cubey::math::Vec3 base_normal = glm::normalize(cube_face_point(face, u, v));
    if (!config.terrain_enabled || config.terrain_height_scale_m <= 0.0F) {
        return base_normal;
    }

    constexpr float kNormalStep = 0.0015F;
    const float u0 = std::clamp(u - kNormalStep, -1.0F, 1.0F);
    const float u1 = std::clamp(u + kNormalStep, -1.0F, 1.0F);
    const float v0 = std::clamp(v - kNormalStep, -1.0F, 1.0F);
    const float v1 = std::clamp(v + kNormalStep, -1.0F, 1.0F);
    const cubey::math::DVec3 tangent_u =
        terrain_world_position(config, face, u1, v) - terrain_world_position(config, face, u0, v);
    const cubey::math::DVec3 tangent_v =
        terrain_world_position(config, face, u, v1) - terrain_world_position(config, face, u, v0);
    cubey::math::DVec3 normal_d = glm::cross(tangent_u, tangent_v);
    if (glm::length(normal_d) <= 0.0000001) {
        return base_normal;
    }
    normal_d = glm::normalize(normal_d);
    cubey::math::Vec3 normal{
        static_cast<float>(normal_d.x),
        static_cast<float>(normal_d.y),
        static_cast<float>(normal_d.z),
    };
    if (glm::dot(normal, base_normal) < 0.0F) {
        normal = -normal;
    }
    return glm::normalize(normal);
}

[[nodiscard]] PrimitiveVec3 to_primitive(cubey::math::Vec3 value) {
    return {value.x, value.y, value.z};
}

[[nodiscard]] PrimitiveVec3 final_color(const PlanetConfig& config, cubey::math::Vec3 normal,
                                        float height_m) {
    if (config.terrain_enabled && config.terrain_height_scale_m > 0.0F) {
        const float t =
            std::clamp(height_m / std::max(config.terrain_height_scale_m, 1.0F), -1.0F, 1.0F);
        if (t < -0.15F) {
            return {0.035F, 0.105F, 0.190F};
        }
        if (t < 0.22F) {
            const float blend = (t + 0.15F) / 0.37F;
            return {
                lerp(0.070F, 0.120F, blend),
                lerp(0.170F, 0.280F, blend),
                lerp(0.130F, 0.100F, blend),
            };
        }
        if (t < 0.62F) {
            const float blend = (t - 0.22F) / 0.40F;
            return {
                lerp(0.130F, 0.360F, blend),
                lerp(0.260F, 0.310F, blend),
                lerp(0.110F, 0.230F, blend),
            };
        }
        return {0.66F, 0.70F, 0.76F};
    }
    const float latitude = normal.y * 0.5F + 0.5F;
    return {
        0.035F + 0.030F * latitude,
        0.100F + 0.070F * latitude,
        0.230F + 0.200F * latitude,
    };
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

[[nodiscard]] PrimitiveVec3 seam_surface_color(cubey::math::Vec3 normal) {
    const PrimitiveVec3 color = final_color(PlanetConfig{}, normal, 0.0F);
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
    return final_color(config, normal, 0.0F);
}

[[nodiscard]] PrimitiveVec3 vertex_color(const PlanetConfig& config,
                                         const PlanetSurfacePatchInstance& patch,
                                         cubey::math::Vec3 normal, float height_m) {
    switch (config.debug_view) {
    case PlanetDebugView::Final:
        return final_color(config, normal, height_m);
    case PlanetDebugView::FaceId:
        return kFaceColors[patch.id.face];
    case PlanetDebugView::PatchId:
        return patch_color(patch.id);
    case PlanetDebugView::LodLevel:
        return lod_color(patch.id.level, config.max_lod_level);
    case PlanetDebugView::ScreenError:
        return screen_error_color(patch.screen_error_px, config.lod_target_edge_px);
    case PlanetDebugView::Seams:
        return seam_surface_color(normal);
    }
    return final_color(config, normal, height_m);
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
        sphere_world_position(config, patch.id.face, u_mid, v_mid),
        sphere_world_position(config, patch.id.face, bounds.u0, bounds.v0),
        sphere_world_position(config, patch.id.face, bounds.u1, bounds.v0),
        sphere_world_position(config, patch.id.face, bounds.u0, bounds.v1),
        sphere_world_position(config, patch.id.face, bounds.u1, bounds.v1),
    };
}

struct PatchBounds {
    cubey::math::DVec3 center_m{0.0, 0.0, 0.0};
    double radius_m = 0.0;
};

[[nodiscard]] PatchBounds patch_bounds(const PlanetConfig& config,
                                       const PlanetSurfacePatchInstance& patch) {
    const std::array<cubey::math::DVec3, 5> samples = patch_sample_points(config, patch);
    PatchBounds bounds{
        .center_m = samples[0],
    };
    for (cubey::math::DVec3 sample : samples) {
        bounds.radius_m = std::max(bounds.radius_m, glm::length(sample - bounds.center_m));
    }
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
    const cubey::math::DVec3 center = sphere_world_position(config, patch.id.face, u_mid, v_mid);
    const cubey::math::DVec3 edge_a =
        sphere_world_position(config, patch.id.face, bounds.u0, v_mid);
    const cubey::math::DVec3 edge_b =
        sphere_world_position(config, patch.id.face, bounds.u1, v_mid);
    const float patch_edge_m = static_cast<float>(glm::length(edge_b - edge_a));
    const float cell_edge_m = patch_edge_m / static_cast<float>(config.patch_resolution);
    const float distance_m =
        std::max(static_cast<float>(glm::length(center - view.camera_world_position_m)), 1.0F);
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
        sphere_world_position(config, patch.id.face, bounds.u0, v_mid);
    const cubey::math::DVec3 edge_b =
        sphere_world_position(config, patch.id.face, bounds.u1, v_mid);
    const cubey::math::DVec3 edge_c =
        sphere_world_position(config, patch.id.face, u_mid, bounds.v0);
    const cubey::math::DVec3 edge_d =
        sphere_world_position(config, patch.id.face, u_mid, bounds.v1);
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

void record_visible_patch(const PlanetConfig& config, PlanetSurfacePatchPlan& plan,
                          const PlanetSurfacePatchInstance& patch) {
    plan.diagnostics.min_lod_level = plan.diagnostics.visible_patch_count == 0U
                                         ? patch.id.level
                                         : std::min(plan.diagnostics.min_lod_level, patch.id.level);
    plan.diagnostics.max_lod_level = std::max(plan.diagnostics.max_lod_level, patch.id.level);
    if (patch.id.level < plan.diagnostics.patches_by_lod.size()) {
        ++plan.diagnostics.patches_by_lod[patch.id.level];
    }
    update_screen_error_range(plan.diagnostics, patch.screen_error_px);
    update_lod_cell_edge_range(plan.diagnostics, patch.id.level, patch_cell_edge_m(config, patch));
    if (patch.id.level == 0U) {
        ++plan.diagnostics.base_patch_count;
    } else {
        ++plan.diagnostics.refined_patch_count;
    }
    ++plan.diagnostics.visible_patch_count;
    plan.diagnostics.patch_count = plan.diagnostics.visible_patch_count;
    plan.selected_patches.push_back(patch);
}

void append_coverage_patches(const PlanetConfig& config, PlanetSurfaceView view,
                             PlanetSurfacePatchInstance patch, PlanetSurfacePatchPlan& plan) {
    patch.screen_error_px = patch_screen_error_px(config, view, patch);
    ++plan.diagnostics.planned_patch_count;

    const bool wants_refinement =
        patch.id.level < config.max_lod_level && patch.screen_error_px > config.lod_target_edge_px;
    if (wants_refinement && !patch_passes_horizon_cull(config, view, patch)) {
        record_refinement_cull(plan, true);
        ++plan.diagnostics.refinement_fallback_patch_count;
        record_visible_patch(config, plan, patch);
        return;
    }
    if (wants_refinement && !patch_passes_view_cull(config, view, patch)) {
        record_refinement_cull(plan, false);
        ++plan.diagnostics.refinement_fallback_patch_count;
        record_visible_patch(config, plan, patch);
        return;
    }

    if (wants_refinement) {
        ++plan.diagnostics.subdivided_patch_count;
        append_coverage_patches(
            config, view,
            PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 0U)}, plan);
        append_coverage_patches(
            config, view,
            PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 1U)}, plan);
        append_coverage_patches(
            config, view,
            PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 2U)}, plan);
        append_coverage_patches(
            config, view,
            PlanetSurfacePatchInstance{.id = planet_surface_child_patch_id(patch.id, 3U)}, plan);
        return;
    }
    record_visible_patch(config, plan, patch);
}

[[nodiscard]] PlanetSurfacePatchPlan make_surface_patch_plan(const PlanetConfig& config,
                                                             PlanetSurfaceView view) {
    PlanetSurfacePatchPlan plan{};
    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t py = 0; py < config.patches_per_face; ++py) {
            for (std::uint32_t px = 0; px < config.patches_per_face; ++px) {
                append_coverage_patches(config, view,
                                        PlanetSurfacePatchInstance{
                                            .id =
                                                {
                                                    .face = face,
                                                    .level = 0,
                                                    .x = px,
                                                    .y = py,
                                                },
                                        },
                                        plan);
            }
        }
    }
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
        sphere_world_position(config, patch.id.face, bounds.u0, v_mid);
    const cubey::math::DVec3 edge_b =
        sphere_world_position(config, patch.id.face, bounds.u1, v_mid);
    const cubey::math::DVec3 edge_c =
        sphere_world_position(config, patch.id.face, u_mid, bounds.v0);
    const cubey::math::DVec3 edge_d =
        sphere_world_position(config, patch.id.face, u_mid, bounds.v1);
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
            const cubey::math::Vec3 sphere_normal =
                glm::normalize(cube_face_point(patch.id.face, u, v));
            const cubey::math::Vec3 normal = terrain_normal(config, patch.id.face, u, v);
            const float height_m = terrain_height_m(config, sphere_normal);
            const cubey::math::DVec3 world_position =
                terrain_world_position(config, patch.id.face, u, v);
            const cubey::math::Vec3 render_position =
                planet_frame_world_to_render_m(frame, world_position);
            result.mesh.vertices.push_back(VertexPositionColorNormalUv{
                .position = to_primitive(render_position),
                .color = vertex_color(config, patch, normal, height_m),
                .normal = to_primitive(normal),
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

PlanetSurfacePatchPlan plan_planet_surface_patches(const PlanetConfig& config,
                                                   PlanetSurfaceView view) {
    validate_planet_config(config);
    return make_surface_patch_plan(config, view);
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

    return mesh;
}

std::vector<PlanetSurfaceGpuPatchInstance>
make_planet_surface_gpu_patch_instances(const PlanetSurfacePatchPlan& plan) {
    std::vector<PlanetSurfaceGpuPatchInstance> instances;
    instances.reserve(plan.selected_patches.size());
    for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
        instances.push_back({
            .face = patch.id.face,
            .level = patch.id.level,
            .x = patch.id.x,
            .y = patch.id.y,
            .screen_error_px = patch.screen_error_px,
        });
    }
    return instances;
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config) {
    const float camera_distance =
        std::max(config.radius_m + config.camera_altitude_m, config.radius_m * 1.01F);
    const PlanetSurfaceView view{
        .camera_world_position_m = {0.0, 0.0, camera_distance},
    };
    return make_planet_surface_mesh(config, view);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view) {
    PlanetFrame frame{};
    frame.planet_radius_m = config.radius_m;
    frame.camera_world_position_m = view.camera_world_position_m;
    frame.render_origin_world_m = {0.0, 0.0, 0.0};
    return make_planet_surface_mesh(config, view, frame);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view,
                                                  const PlanetFrame& frame) {
    const PlanetSurfacePatchPlan plan = plan_planet_surface_patches(config, view);
    return make_planet_surface_mesh(config, view, frame, plan);
}

PlanetSurfaceBuildResult make_planet_surface_mesh(const PlanetConfig& config,
                                                  PlanetSurfaceView view, const PlanetFrame& frame,
                                                  const PlanetSurfacePatchPlan& plan) {
    validate_planet_config(config);
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
