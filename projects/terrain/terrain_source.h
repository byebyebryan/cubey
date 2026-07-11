#pragma once

#include <cubey/core/math.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainPreset : std::uint8_t {
    Mountain,
    Upland,
    Plains,
};

enum class TerrainWeatheringMode : std::uint8_t {
    Off,
    Local,
};

inline constexpr std::uint64_t kTerrainDefaultSeed = 0x7465'7272'6169'6e01ULL;

struct TerrainSourceConfig {
    std::uint64_t seed = kTerrainDefaultSeed;
    TerrainPreset preset = TerrainPreset::Mountain;
    TerrainWeatheringMode weathering = TerrainWeatheringMode::Off;
    float weathering_strength = 1.0F;
};

struct TerrainSourceBandParameters {
    std::int32_t seed = 0;
    std::uint32_t octaves = 1;
    float frequency = 1.0F;
    float lacunarity = 2.0F;
    float gain = 0.5F;
    float ridge_mix = 0.0F;
};

struct TerrainSourceParameters {
    TerrainSourceBandParameters macro{};
    TerrainSourceBandParameters structure{};
    TerrainSourceBandParameters detail{};
    float macro_weight = 0.5F;
    float structure_weight = 0.5F;
    float detail_weight = 0.1F;
    float elevation_bias = 0.0F;
    float base_height_m = 0.0F;
    float height_scale_m = 1.0F;
    float elevation_power = 1.0F;
    float gradient_step_m = 2.0F;
    TerrainWeatheringMode weathering = TerrainWeatheringMode::Off;
    float weathering_radius_m = 16.0F;
    float weathering_max_delta_m = 0.0F;
    float weathering_strength = 1.0F;
};

struct TerrainQuery {
    cubey::math::Vec2 world_xz{0.0F, 0.0F};
    float footprint_m = 0.0F;
};

struct TerrainSample {
    float base_height_m = 0.0F;
    float height_m = 0.0F;
    cubey::math::Vec2 gradient_xz{0.0F, 0.0F};
    float weathering_delta_m = 0.0F;
};

struct TerrainSourceSummary {
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    float mean_height_m = 0.0F;
    float mean_slope = 0.0F;
};

[[nodiscard]] std::string_view terrain_preset_name(TerrainPreset preset);
[[nodiscard]] TerrainPreset terrain_preset_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_weathering_mode_name(TerrainWeatheringMode mode);
[[nodiscard]] TerrainWeatheringMode terrain_weathering_mode_from_name(std::string_view name);

void validate_terrain_source_config(const TerrainSourceConfig& config);
void validate_terrain_source_parameters(const TerrainSourceParameters& parameters);
[[nodiscard]] TerrainSourceParameters
resolve_terrain_source_parameters(const TerrainSourceConfig& config);
[[nodiscard]] float sample_terrain_base_height(const TerrainSourceParameters& parameters,
                                               const TerrainQuery& query);
[[nodiscard]] TerrainSample sample_terrain(const TerrainSourceParameters& parameters,
                                           const TerrainQuery& query);
[[nodiscard]] TerrainSourceSummary
summarize_terrain_source(const TerrainSourceParameters& parameters, cubey::math::Vec2 center_xz,
                         float extent_m, std::uint32_t samples_per_axis);

} // namespace cubey::projects::terrain
