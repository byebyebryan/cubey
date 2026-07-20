#include "terrain_shadow.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

constexpr float kMinimumDirectionLengthSquared = 1.0e-8F;
constexpr float kShadowBoundsGuardM = 32.0F;
constexpr float kShadowDepthGuardM = 64.0F;

[[nodiscard]] bool finite(cubey::math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] cubey::math::Vec3 normalized_or_zero(cubey::math::Vec3 direction) noexcept {
    if (!finite(direction) || glm::dot(direction, direction) <= kMinimumDirectionLengthSquared) {
        return {0.0F, 0.0F, 0.0F};
    }
    return glm::normalize(direction);
}

} // namespace

bool terrain_shadow_light_above_horizon(cubey::math::Vec3 light_direction) noexcept {
    return normalized_or_zero(light_direction).y > 0.0F;
}

TerrainShadowProjection terrain_shadow_projection(const TerrainShadowProductBounds& bounds,
                                                  cubey::math::Vec3 light_direction) {
    if (!std::isfinite(bounds.outer_radius_m) || bounds.outer_radius_m <= 0.0F ||
        !std::isfinite(bounds.minimum_height_m) ||
        !std::isfinite(bounds.maximum_height_m) ||
        bounds.maximum_height_m < bounds.minimum_height_m) {
        throw std::runtime_error("terrain shadow bounds must be finite and ordered");
    }

    const cubey::math::Vec3 normalized_light = normalized_or_zero(light_direction);
    if (glm::dot(normalized_light, normalized_light) <= kMinimumDirectionLengthSquared) {
        throw std::runtime_error("terrain shadow light direction must be finite and nonzero");
    }

    const float center_height_m =
        (bounds.minimum_height_m + bounds.maximum_height_m) * 0.5F;
    const float half_height_m =
        (bounds.maximum_height_m - bounds.minimum_height_m) * 0.5F;
    const float bounding_radius_m =
        std::hypot(bounds.outer_radius_m, half_height_m) + kShadowBoundsGuardM;
    const float eye_distance_m = bounding_radius_m + kShadowDepthGuardM;
    const float near_z = 1.0F;
    const float far_z = eye_distance_m + bounding_radius_m + kShadowDepthGuardM;
    const cubey::math::Vec3 center{0.0F, center_height_m, 0.0F};
    const cubey::math::Vec3 eye = center + normalized_light * eye_distance_m;
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    const cubey::math::Vec3 forward = glm::normalize(center - eye);
    if (std::abs(glm::dot(forward, up)) > 0.95F) {
        up = {0.0F, 0.0F, 1.0F};
    }

    const cubey::math::Mat4 view = glm::lookAtRH(eye, center, up);
    const cubey::math::Mat4 projection = cubey::math::orthographic(
        -bounding_radius_m, bounding_radius_m, -bounding_radius_m,
        bounding_radius_m, near_z, far_z);
    return {
        .light_view_projection = projection * view,
        .light_direction = normalized_light,
        .orthographic_span_m = bounding_radius_m * 2.0F,
        .depth_span_m = far_z - near_z,
        .texel_world_size_m =
            (bounding_radius_m * 2.0F) / static_cast<float>(kTerrainShadowMapExtent),
        .light_above_horizon = normalized_light.y > 0.0F,
    };
}

bool terrain_shadow_update_required(const TerrainShadowCacheState& cache,
                                    bool shadows_enabled,
                                    std::uint64_t product_content_hash,
                                    cubey::math::Vec3 light_direction,
                                    float angular_threshold_radians) noexcept {
    const cubey::math::Vec3 normalized_light = normalized_or_zero(light_direction);
    if (!shadows_enabled || normalized_light.y <= 0.0F ||
        !std::isfinite(angular_threshold_radians) || angular_threshold_radians < 0.0F) {
        return false;
    }
    if (!cache.valid || cache.product_content_hash != product_content_hash) {
        return true;
    }

    const float direction_dot =
        std::clamp(glm::dot(cache.projection.light_direction, normalized_light), -1.0F, 1.0F);
    return direction_dot <= std::cos(angular_threshold_radians);
}

void update_terrain_shadow_cache(TerrainShadowCacheState& cache,
                                 std::uint64_t product_content_hash,
                                 const TerrainShadowProjection& projection) noexcept {
    cache.projection = projection;
    cache.product_content_hash = product_content_hash;
    ++cache.update_count;
    cache.valid = true;
}

void invalidate_terrain_shadow_cache(TerrainShadowCacheState& cache) noexcept {
    cache.valid = false;
}

} // namespace cubey::projects::terrain
