#pragma once

#include "terrain_height_source.h"

namespace cubey::projects::terrain {

struct TerrainRadialReliefParameters {
    cubey::math::Vec2 focus_xz{0.0F, 0.0F};
    float floor_footprint_m = 6'000.0F;
    float floor_relief_fraction = 0.08F;
    float structure_footprint_m = 2'500.0F;
    float detail_footprint_m = 0.0F;
    float broad_start_m = 1'000.0F;
    float broad_full_m = 24'000.0F;
    float detail_start_m = 5'000.0F;
    float detail_full_m = 30'000.0F;
};

struct TerrainRadialReliefSample {
    float height_m = 0.0F;
    float source_height_m = 0.0F;
    float floor_height_m = 0.0F;
    float structure_height_m = 0.0F;
    float detail_height_m = 0.0F;
    float radial_distance_m = 0.0F;
    float broad_gate = 0.0F;
    float detail_gate = 0.0F;
};

void validate_terrain_radial_relief_parameters(const TerrainRadialReliefParameters& parameters);

class TerrainRadialReliefSource final : public TerrainHeightSource {
  public:
    TerrainRadialReliefSource(const TerrainHeightSource& source,
                              TerrainRadialReliefParameters parameters);

    [[nodiscard]] TerrainHeightSourceMetadata metadata() const noexcept override;
    [[nodiscard]] float sample_height(const TerrainQuery& query) const override;
    [[nodiscard]] TerrainRadialReliefSample sample_composition(const TerrainQuery& query) const;
    [[nodiscard]] const TerrainRadialReliefParameters& parameters() const noexcept;

  private:
    const TerrainHeightSource& source_;
    TerrainRadialReliefParameters parameters_{};
    TerrainHeightSourceMetadata source_metadata_{};
};

} // namespace cubey::projects::terrain
