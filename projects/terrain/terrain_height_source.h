#pragma once

#include "terrain_source.h"

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

struct TerrainHeightSourceMetadata {
    std::string_view id{};
    std::uint64_t seed = 0U;
    float base_height_m = 0.0F;
    float relief_scale_m = 1.0F;
    float gradient_step_m = 2.0F;
};

void validate_terrain_height_source_metadata(const TerrainHeightSourceMetadata& metadata);

class TerrainHeightSource {
  public:
    virtual ~TerrainHeightSource() = default;

    [[nodiscard]] virtual TerrainHeightSourceMetadata metadata() const noexcept = 0;
    [[nodiscard]] virtual float sample_height(const TerrainQuery& query) const = 0;
    [[nodiscard]] virtual TerrainSample sample(const TerrainQuery& query) const;
};

class ParameterTerrainHeightSource final : public TerrainHeightSource {
  public:
    ParameterTerrainHeightSource(TerrainSourceParameters parameters, std::uint64_t seed);

    [[nodiscard]] TerrainHeightSourceMetadata metadata() const noexcept override;
    [[nodiscard]] float sample_height(const TerrainQuery& query) const override;
    [[nodiscard]] TerrainSample sample(const TerrainQuery& query) const override;
    [[nodiscard]] const TerrainSourceParameters& parameters() const noexcept;

  private:
    TerrainSourceParameters parameters_{};
    TerrainHeightSourceMetadata metadata_{};
};

} // namespace cubey::projects::terrain
