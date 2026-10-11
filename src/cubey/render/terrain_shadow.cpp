#include <cubey/render/terrain_shadow.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::render {
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

TerrainShadowProjection terrain_shadow_box_projection(const TerrainShadowBoxBounds& bounds,
                                                      cubey::math::Vec3 light_direction,
                                                      std::uint32_t map_extent) {
    const auto direction = normalized_or_zero(light_direction);
    if (!finite(bounds.minimum_m) || !finite(bounds.maximum_m) ||
        bounds.maximum_m.x <= bounds.minimum_m.x || bounds.maximum_m.z <= bounds.minimum_m.z ||
        bounds.maximum_m.y < bounds.minimum_m.y ||
        glm::dot(direction, direction) <= kMinimumDirectionLengthSquared || map_extent == 0U)
        throw std::runtime_error("terrain shadow box must be finite, ordered, and nondegenerate");
    const auto center = (bounds.minimum_m + bounds.maximum_m) * 0.5F;
    const float radius = glm::length((bounds.maximum_m - bounds.minimum_m) * 0.5F);
    const auto up =
        std::abs(direction.y) > 0.95F ? cubey::math::Vec3{0, 0, 1} : cubey::math::Vec3{0, 1, 0};
    const auto view =
        glm::lookAtRH(center + direction * (radius + 2.0F * kShadowDepthGuardM), center, up);
    cubey::math::Vec3 low{std::numeric_limits<float>::max()};
    cubey::math::Vec3 high{std::numeric_limits<float>::lowest()};
    for (unsigned corner = 0; corner < 8U; ++corner) {
        const cubey::math::Vec3 world{corner & 1U ? bounds.maximum_m.x : bounds.minimum_m.x,
                                      corner & 2U ? bounds.maximum_m.y : bounds.minimum_m.y,
                                      corner & 4U ? bounds.maximum_m.z : bounds.minimum_m.z};
        const auto p = cubey::math::Vec3(view * cubey::math::Vec4(world, 1.0F));
        low = glm::min(low, p);
        high = glm::max(high, p);
    }
    const float near_z = std::max(1.0F, -high.z - kShadowDepthGuardM);
    const float far_z = -low.z + kShadowDepthGuardM;
    const float span = std::max(high.x - low.x, high.y - low.y) + 2.0F * kShadowBoundsGuardM;
    return {.light_view_projection =
                cubey::math::orthographic(low.x - kShadowBoundsGuardM, high.x + kShadowBoundsGuardM,
                                          low.y - kShadowBoundsGuardM, high.y + kShadowBoundsGuardM,
                                          near_z, far_z) *
                view,
            .light_direction = direction,
            .orthographic_span_m = span,
            .depth_span_m = far_z - near_z,
            .texel_world_size_m = span / static_cast<float>(map_extent),
            .light_above_horizon = direction.y > 0.0F};
}

TerrainShadowProjection terrain_shadow_projection(const TerrainShadowProductBounds& bounds,
                                                  cubey::math::Vec3 light_direction,
                                                  cubey::math::Vec2 center_xz) {
    if (!std::isfinite(bounds.outer_radius_m) || bounds.outer_radius_m <= 0.0F ||
        !std::isfinite(bounds.minimum_height_m) || !std::isfinite(bounds.maximum_height_m) ||
        bounds.maximum_height_m < bounds.minimum_height_m || !std::isfinite(center_xz.x) ||
        !std::isfinite(center_xz.y)) {
        throw std::runtime_error("terrain shadow bounds must be finite and ordered");
    }

    const cubey::math::Vec3 normalized_light = normalized_or_zero(light_direction);
    if (glm::dot(normalized_light, normalized_light) <= kMinimumDirectionLengthSquared) {
        throw std::runtime_error("terrain shadow light direction must be finite and nonzero");
    }

    const float center_height_m = (bounds.minimum_height_m + bounds.maximum_height_m) * 0.5F;
    const float half_height_m = (bounds.maximum_height_m - bounds.minimum_height_m) * 0.5F;
    const float bounding_radius_m =
        std::hypot(bounds.outer_radius_m, half_height_m) + kShadowBoundsGuardM;
    const float eye_distance_m = bounding_radius_m + kShadowDepthGuardM;
    const float near_z = 1.0F;
    const float far_z = eye_distance_m + bounding_radius_m + kShadowDepthGuardM;
    const cubey::math::Vec3 center{center_xz.x, center_height_m, center_xz.y};
    const cubey::math::Vec3 eye = center + normalized_light * eye_distance_m;
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    const cubey::math::Vec3 forward = glm::normalize(center - eye);
    if (std::abs(glm::dot(forward, up)) > 0.95F) {
        up = {0.0F, 0.0F, 1.0F};
    }

    const cubey::math::Mat4 view = glm::lookAtRH(eye, center, up);
    const cubey::math::Mat4 projection =
        cubey::math::orthographic(-bounding_radius_m, bounding_radius_m, -bounding_radius_m,
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

bool terrain_shadow_update_required(const TerrainShadowCacheState& cache, bool shadows_enabled,
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

void update_terrain_shadow_cache(TerrainShadowCacheState& cache, std::uint64_t product_content_hash,
                                 const TerrainShadowProjection& projection) noexcept {
    cache.projection = projection;
    cache.product_content_hash = product_content_hash;
    ++cache.update_count;
    cache.valid = true;
}

void invalidate_terrain_shadow_cache(TerrainShadowCacheState& cache) noexcept {
    cache.valid = false;
}

} // namespace cubey::render
