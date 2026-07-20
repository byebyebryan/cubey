#pragma once

#include <cubey/core/math.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

struct TerrainQuery {
    cubey::math::Vec2 world_xz{0.0F, 0.0F};
    float footprint_m = 0.0F;
};

struct TerrainSample {
    float height_m = 0.0F;
    cubey::math::Vec2 gradient_xz{0.0F, 0.0F};
};

struct TerrainHeightSourceMetadata {
    std::string_view id{};
    std::uint64_t seed = 0U;
    float base_height_m = 0.0F;
    float relief_scale_m = 1.0F;
    float gradient_step_m = 2.0F;
};

struct TerrainHeightSourceBounds {
    cubey::math::Vec2 minimum_xz{0.0F, 0.0F};
    cubey::math::Vec2 maximum_xz{0.0F, 0.0F};
};

void validate_terrain_height_source_metadata(const TerrainHeightSourceMetadata& metadata);
void validate_terrain_height_source_bounds(const TerrainHeightSourceBounds& bounds);
[[nodiscard]] cubey::math::Vec2
terrain_height_source_bounds_center(const TerrainHeightSourceBounds& bounds);
[[nodiscard]] bool
terrain_height_source_bounds_contains_disk(const TerrainHeightSourceBounds& bounds,
                                           cubey::math::Vec2 center_xz, float radius_m) noexcept;

class TerrainHeightSource {
  public:
    virtual ~TerrainHeightSource() = default;

    [[nodiscard]] virtual TerrainHeightSourceMetadata metadata() const noexcept = 0;
    [[nodiscard]] virtual float sample_height(const TerrainQuery& query) const = 0;
    [[nodiscard]] virtual TerrainSample sample(const TerrainQuery& query) const;
};

} // namespace cubey::projects::terrain
