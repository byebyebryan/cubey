#pragma once

#include "terrain_height_source.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainSourceStudyRecipe : std::uint8_t {
    ControlV2_1,
    TerrainEngineFbm,
    ElevatedDerivative,
    SwissDerivative,
    MountainsSigned,
    RainforestCliff,
    MountainPeakWarp,
};

struct TerrainSourceStudyRecipeInfo {
    TerrainSourceStudyRecipe recipe = TerrainSourceStudyRecipe::ControlV2_1;
    std::string_view id{};
    std::string_view operator_family{};
    std::string_view reference{};
};

struct TerrainSourceStudyCalibration {
    float raw_p05 = 0.0F;
    float raw_p95 = 1.0F;
    float scale_m = 3'500.0F;
    std::uint64_t sample_count = 0U;
};

[[nodiscard]] std::span<const TerrainSourceStudyRecipeInfo> terrain_source_study_recipes() noexcept;
[[nodiscard]] std::string_view
terrain_source_study_recipe_name(TerrainSourceStudyRecipe recipe) noexcept;
[[nodiscard]] TerrainSourceStudyRecipe terrain_source_study_recipe_from_name(std::string_view name);
[[nodiscard]] TerrainSourceStudyCalibration
terrain_source_study_calibration(TerrainSourceStudyRecipe recipe);

class TerrainSourceStudySource final : public TerrainHeightSource {
  public:
    TerrainSourceStudySource(TerrainSourceStudyRecipe recipe, std::uint64_t seed);
    TerrainSourceStudySource(TerrainSourceStudyRecipe recipe, std::uint64_t seed,
                             TerrainSourceStudyCalibration calibration);

    [[nodiscard]] TerrainHeightSourceMetadata metadata() const noexcept override;
    [[nodiscard]] float sample_height(const TerrainQuery& query) const override;
    [[nodiscard]] TerrainSourceStudyRecipe recipe() const noexcept;
    [[nodiscard]] TerrainSourceStudyCalibration calibration() const noexcept;
    [[nodiscard]] float sample_raw_height(const TerrainQuery& query) const;

  private:
    TerrainSourceStudyRecipe recipe_ = TerrainSourceStudyRecipe::ControlV2_1;
    std::uint64_t seed_ = 0U;
    TerrainSourceStudyCalibration calibration_{};
    TerrainSourceParameters control_parameters_{};
};

} // namespace cubey::projects::terrain
