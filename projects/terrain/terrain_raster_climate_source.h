#pragma once

#include "terrain_height_source.h"
#include "terrain_raster_height_source.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainClimateSample {
    float temperature_mean_c = 0.0F;
    float temperature_stddev_c = 0.0F;
    float precipitation_annual_mm = 0.0F;
    float precipitation_cv = 0.0F;
};

struct TerrainRasterClimateMetadata {
    std::filesystem::path manifest_path{};
    std::uint64_t seed = 0U;
    std::string elevation_sha256{};
    std::string climate_sha256{};
    std::string generator{};
    std::string model_id{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    float sample_spacing_m = 0.0F;
};

class TerrainRasterClimateSource final {
  public:
    explicit TerrainRasterClimateSource(const std::filesystem::path& field_path);

    [[nodiscard]] TerrainClimateSample sample(cubey::math::Vec2 world_xz) const;
    [[nodiscard]] TerrainHeightSourceBounds bounds() const noexcept;
    [[nodiscard]] bool contains(cubey::math::Vec2 world_xz) const noexcept;
    [[nodiscard]] const TerrainRasterClimateMetadata& metadata() const noexcept;

  private:
    [[nodiscard]] float sample_channel(std::uint32_t channel,
                                       cubey::math::Vec2 world_xz) const;

    TerrainRasterClimateMetadata metadata_{};
    float origin_x_m_ = 0.0F;
    float origin_z_m_ = 0.0F;
    std::vector<float> values_{};
};

void validate_terrain_climate_binding(const TerrainRasterHeightSource& height,
                                      const TerrainRasterClimateSource& climate);

} // namespace cubey::projects::terrain
