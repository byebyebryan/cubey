#pragma once

#include <cubey/core/math.h>

#include <cstdint>

namespace cubey::projects::terrain {

constexpr std::uint32_t kTerrainShadowMapExtent = 2048U;
constexpr float kTerrainShadowDirectionThresholdRadians =
    0.5F * 0.01745329251994329577F;

struct TerrainShadowProductBounds {
    float outer_radius_m = 0.0F;
    float minimum_height_m = 0.0F;
    float maximum_height_m = 0.0F;
};

struct TerrainShadowProjection {
    cubey::math::Mat4 light_view_projection{1.0F};
    cubey::math::Vec3 light_direction{0.0F, 1.0F, 0.0F};
    float orthographic_span_m = 0.0F;
    float depth_span_m = 0.0F;
    float texel_world_size_m = 0.0F;
    bool light_above_horizon = false;
};

struct TerrainShadowCacheState {
    TerrainShadowProjection projection{};
    std::uint64_t product_content_hash = 0U;
    std::uint64_t update_count = 0U;
    bool valid = false;
};

[[nodiscard]] bool
terrain_shadow_light_above_horizon(cubey::math::Vec3 light_direction) noexcept;

[[nodiscard]] TerrainShadowProjection
terrain_shadow_projection(const TerrainShadowProductBounds& bounds,
                          cubey::math::Vec3 light_direction);

[[nodiscard]] bool terrain_shadow_update_required(
    const TerrainShadowCacheState& cache, bool shadows_enabled,
    std::uint64_t product_content_hash, cubey::math::Vec3 light_direction,
    float angular_threshold_radians = kTerrainShadowDirectionThresholdRadians) noexcept;

void update_terrain_shadow_cache(TerrainShadowCacheState& cache,
                                 std::uint64_t product_content_hash,
                                 const TerrainShadowProjection& projection) noexcept;

void invalidate_terrain_shadow_cache(TerrainShadowCacheState& cache) noexcept;

} // namespace cubey::projects::terrain
