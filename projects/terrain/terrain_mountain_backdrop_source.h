#pragma once

#include "terrain_height_source.h"

#include <cstdint>

namespace cubey::projects::terrain {

struct TerrainMountainBackdropCalibration {
    float raw_p05 = 0.0927082449F;
    float raw_p95 = 0.6683836579F;
    float scale_m = 6'079.8149414F;
    std::uint64_t sample_count = 198'147U;
};

[[nodiscard]] constexpr TerrainMountainBackdropCalibration
terrain_mountain_backdrop_calibration() noexcept {
    return {};
}

[[nodiscard]] float sample_terrain_mountain_backdrop_raw(std::uint64_t seed,
                                                         const TerrainQuery& query);

class TerrainMountainBackdropSource final : public TerrainHeightSource {
  public:
    explicit TerrainMountainBackdropSource(std::uint64_t seed);

    [[nodiscard]] TerrainHeightSourceMetadata metadata() const noexcept override;
    [[nodiscard]] float sample_height(const TerrainQuery& query) const override;
    [[nodiscard]] float sample_raw_height(const TerrainQuery& query) const;
    [[nodiscard]] TerrainMountainBackdropCalibration calibration() const noexcept;

  private:
    std::uint64_t seed_ = 0U;
};

} // namespace cubey::projects::terrain
