#pragma once

#include "terrain_height_source.h"

namespace cubey::projects::terrain {

struct TerrainDirectionalReliefParameters {
    cubey::math::Vec2 focus_xz{0.0F, 0.0F};
    float mountain_yaw_radians = 0.0F;
    float floor_footprint_m = 6'000.0F;
    float floor_relief_fraction = 0.08F;
    float structure_footprint_m = 1'500.0F;
    float broad_start_m = 2'500.0F;
    float broad_full_m = 8'000.0F;
    float detail_start_m = 5'000.0F;
    float detail_full_m = 10'000.0F;
    float warp_period_m = 14'000.0F;
    float warp_amplitude_m = 1'250.0F;
    std::uint32_t warp_octaves = 2U;
};

struct TerrainDirectionalReliefSample {
    float height_m = 0.0F;
    float source_height_m = 0.0F;
    float floor_height_m = 0.0F;
    float structure_height_m = 0.0F;
    float directional_distance_m = 0.0F;
    float broad_gate = 0.0F;
    float detail_gate = 0.0F;
};

void validate_terrain_directional_relief_parameters(
    const TerrainDirectionalReliefParameters& parameters);

class TerrainDirectionalReliefSource final : public TerrainHeightSource {
  public:
    TerrainDirectionalReliefSource(const TerrainHeightSource& source,
                                   TerrainDirectionalReliefParameters parameters);

    [[nodiscard]] TerrainHeightSourceMetadata metadata() const noexcept override;
    [[nodiscard]] float sample_height(const TerrainQuery& query) const override;
    [[nodiscard]] TerrainDirectionalReliefSample sample_composition(
        const TerrainQuery& query) const;
    [[nodiscard]] const TerrainDirectionalReliefParameters& parameters() const noexcept;

  private:
    const TerrainHeightSource& source_;
    TerrainDirectionalReliefParameters parameters_{};
    TerrainHeightSourceMetadata source_metadata_{};
};

} // namespace cubey::projects::terrain
