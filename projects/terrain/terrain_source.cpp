#include "terrain_source.h"

#include <cubey/procedural/noise.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

constexpr float kRotationCos = 0.8F;
constexpr float kRotationSin = 0.6F;

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] std::int32_t terrain_band_seed(std::uint64_t seed, std::string_view domain) {
    const std::uint64_t derived = cubey::procedural::derive_seed(seed, domain);
    return static_cast<std::int32_t>((derived ^ (derived >> 32U)) & 0x7fff'ffffU);
}

[[nodiscard]] float octave_footprint_weight(float frequency, float footprint_m) {
    if (footprint_m <= 0.0F) {
        return 1.0F;
    }
    const float wavelength_m = 1.0F / frequency;
    return 1.0F - smoothstep(wavelength_m * 0.25F, wavelength_m * 0.5F, footprint_m);
}

[[nodiscard]] float sample_terrain_band(const TerrainSourceBandParameters& band,
                                        cubey::math::Vec2 world_xz, float footprint_m) {
    cubey::procedural::CoherentNoiseConfig noise{
        .seed = band.seed,
        .frequency = 1.0F,
        .noise_type = cubey::procedural::CoherentNoiseType::OpenSimplex2S,
        .fractal_type = cubey::procedural::CoherentFractalType::None,
    };

    const cubey::math::Vec2 rotated{
        kRotationCos * world_xz.x - kRotationSin * world_xz.y,
        kRotationSin * world_xz.x + kRotationCos * world_xz.y,
    };
    float frequency = band.frequency;
    float amplitude = 1.0F;
    float value = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < band.octaves; ++octave) {
        const float footprint_weight = octave_footprint_weight(frequency, footprint_m);
        if (footprint_weight > 0.0F) {
            noise.seed = band.seed + static_cast<std::int32_t>(octave * 1013U);
            const float octave_f = static_cast<float>(octave);
            const float sample = cubey::procedural::sample_coherent_noise_2d(
                rotated.x * frequency + octave_f * 17.31F, rotated.y * frequency - octave_f * 9.17F,
                noise);
            const float unit = sample * 0.5F + 0.5F;
            const float ridge = 1.0F - std::abs(sample);
            const float shaped = std::lerp(unit, ridge, band.ridge_mix);
            const float octave_weight = amplitude * footprint_weight;
            value += shaped * octave_weight;
            weight += octave_weight;
        }
        frequency *= band.lacunarity;
        amplitude *= band.gain;
    }
    return weight > 0.0F ? value / weight : 0.5F;
}

[[nodiscard]] TerrainSourceBandParameters band(std::int32_t seed, std::uint32_t octaves,
                                               float wavelength_m, float lacunarity, float gain,
                                               float ridge_mix) {
    return {
        .seed = seed,
        .octaves = octaves,
        .frequency = 1.0F / wavelength_m,
        .lacunarity = lacunarity,
        .gain = gain,
        .ridge_mix = ridge_mix,
    };
}

} // namespace

std::string_view terrain_preset_name(TerrainPreset preset) {
    switch (preset) {
    case TerrainPreset::Mountain:
        return "mountain";
    case TerrainPreset::Upland:
        return "upland";
    case TerrainPreset::Plains:
        return "plains";
    }
    throw std::runtime_error("unknown terrain preset");
}

TerrainPreset terrain_preset_from_name(std::string_view name) {
    if (name.empty() || name == "mountain") {
        return TerrainPreset::Mountain;
    }
    if (name == "upland") {
        return TerrainPreset::Upland;
    }
    if (name == "plains") {
        return TerrainPreset::Plains;
    }
    throw std::runtime_error("unknown terrain preset: " + std::string(name));
}

std::string_view terrain_weathering_mode_name(TerrainWeatheringMode mode) {
    switch (mode) {
    case TerrainWeatheringMode::Off:
        return "off";
    case TerrainWeatheringMode::Local:
        return "local";
    }
    throw std::runtime_error("unknown terrain weathering mode");
}

TerrainWeatheringMode terrain_weathering_mode_from_name(std::string_view name) {
    if (name.empty() || name == "off") {
        return TerrainWeatheringMode::Off;
    }
    if (name == "local") {
        return TerrainWeatheringMode::Local;
    }
    throw std::runtime_error("unknown terrain weathering mode: " + std::string(name));
}

void validate_terrain_source_config(const TerrainSourceConfig& config) {
    if (!std::isfinite(config.weathering_strength) || config.weathering_strength < 0.0F ||
        config.weathering_strength > 1.0F) {
        throw std::runtime_error("terrain weathering strength must be within [0, 1]");
    }
}

void validate_terrain_source_parameters(const TerrainSourceParameters& parameters) {
    const auto validate_band = [](const TerrainSourceBandParameters& value) {
        if (value.octaves == 0U || value.octaves > 12U || !std::isfinite(value.frequency) ||
            !std::isfinite(value.lacunarity) || !std::isfinite(value.gain) ||
            !std::isfinite(value.ridge_mix) || value.frequency <= 0.0F ||
            value.lacunarity <= 1.0F || value.gain <= 0.0F || value.gain >= 1.0F ||
            value.ridge_mix < 0.0F || value.ridge_mix > 1.0F) {
            throw std::runtime_error("invalid terrain source band parameters");
        }
    };
    validate_band(parameters.macro);
    validate_band(parameters.structure);
    validate_band(parameters.detail);
    if (!std::isfinite(parameters.height_scale_m) || parameters.height_scale_m <= 0.0F ||
        !std::isfinite(parameters.elevation_power) || parameters.elevation_power <= 0.0F ||
        !std::isfinite(parameters.gradient_step_m) || parameters.gradient_step_m <= 0.0F ||
        !std::isfinite(parameters.weathering_strength) || parameters.weathering_strength < 0.0F ||
        parameters.weathering_strength > 1.0F) {
        throw std::runtime_error("invalid terrain source composition parameters");
    }
}

TerrainSourceParameters resolve_terrain_source_parameters(const TerrainSourceConfig& config) {
    validate_terrain_source_config(config);
    const std::int32_t macro_seed = terrain_band_seed(config.seed, "terrain.v1.macro");
    const std::int32_t structure_seed = terrain_band_seed(config.seed, "terrain.v1.structure");
    const std::int32_t detail_seed = terrain_band_seed(config.seed, "terrain.v1.detail");

    TerrainSourceParameters result{};
    result.weathering = config.weathering;
    result.weathering_strength = config.weathering_strength;
    switch (config.preset) {
    case TerrainPreset::Mountain:
        result.macro = band(macro_seed, 4U, 12'000.0F, 2.01F, 0.52F, 0.04F);
        result.structure = band(structure_seed, 6U, 3'400.0F, 1.98F, 0.50F, 0.24F);
        result.detail = band(detail_seed, 5U, 680.0F, 2.03F, 0.47F, 0.38F);
        result.macro_weight = 0.52F;
        result.structure_weight = 0.48F;
        result.detail_weight = 0.18F;
        result.elevation_bias = -0.06F;
        result.height_scale_m = 4'200.0F;
        result.elevation_power = 2.35F;
        result.gradient_step_m = 2.0F;
        result.weathering_radius_m = 18.0F;
        result.weathering_max_delta_m = 60.0F;
        break;
    case TerrainPreset::Upland:
        result.macro = band(macro_seed, 4U, 14'000.0F, 2.01F, 0.50F, 0.02F);
        result.structure = band(structure_seed, 5U, 4'200.0F, 1.97F, 0.48F, 0.12F);
        result.detail = band(detail_seed, 4U, 900.0F, 2.02F, 0.45F, 0.20F);
        result.macro_weight = 0.60F;
        result.structure_weight = 0.38F;
        result.detail_weight = 0.08F;
        result.elevation_bias = 0.02F;
        result.height_scale_m = 1'100.0F;
        result.elevation_power = 1.55F;
        result.gradient_step_m = 2.0F;
        result.weathering_radius_m = 14.0F;
        result.weathering_max_delta_m = 20.0F;
        break;
    case TerrainPreset::Plains:
        result.macro = band(macro_seed, 3U, 18'000.0F, 2.0F, 0.48F, 0.0F);
        result.structure = band(structure_seed, 4U, 6'000.0F, 1.96F, 0.45F, 0.04F);
        result.detail = band(detail_seed, 3U, 1'400.0F, 2.0F, 0.42F, 0.08F);
        result.macro_weight = 0.70F;
        result.structure_weight = 0.25F;
        result.detail_weight = 0.04F;
        result.elevation_bias = 0.08F;
        result.height_scale_m = 260.0F;
        result.elevation_power = 1.25F;
        result.gradient_step_m = 2.0F;
        result.weathering_radius_m = 10.0F;
        result.weathering_max_delta_m = 4.0F;
        break;
    }
    validate_terrain_source_parameters(result);
    return result;
}

float sample_terrain_base_height(const TerrainSourceParameters& parameters,
                                 const TerrainQuery& query) {
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain query");
    }
    const float macro = sample_terrain_band(parameters.macro, query.world_xz, query.footprint_m);
    const float structure =
        sample_terrain_band(parameters.structure, query.world_xz, query.footprint_m);
    const float detail = sample_terrain_band(parameters.detail, query.world_xz, query.footprint_m);
    const float mass = smoothstep(0.18F, 0.82F, macro);
    const float structured = structure * (0.30F + 0.70F * mass);
    const float local_detail = (detail - 0.5F) * (0.15F + 0.85F * mass);
    const float composed =
        std::clamp(parameters.macro_weight * macro + parameters.structure_weight * structured +
                       parameters.detail_weight * local_detail + parameters.elevation_bias,
                   0.0F, 1.0F);
    return parameters.base_height_m +
           parameters.height_scale_m * std::pow(composed, parameters.elevation_power);
}

TerrainSample sample_terrain(const TerrainSourceParameters& parameters, const TerrainQuery& query) {
    const float center = sample_terrain_base_height(parameters, query);
    const float step_m = std::max(parameters.gradient_step_m, query.footprint_m * 0.5F);
    TerrainQuery offset = query;
    offset.world_xz.x -= step_m;
    const float x0 = sample_terrain_base_height(parameters, offset);
    offset.world_xz.x += 2.0F * step_m;
    const float x1 = sample_terrain_base_height(parameters, offset);
    offset = query;
    offset.world_xz.y -= step_m;
    const float z0 = sample_terrain_base_height(parameters, offset);
    offset.world_xz.y += 2.0F * step_m;
    const float z1 = sample_terrain_base_height(parameters, offset);
    return {
        .base_height_m = center,
        .height_m = center,
        .gradient_xz = {(x1 - x0) / (2.0F * step_m), (z1 - z0) / (2.0F * step_m)},
        .weathering_delta_m = 0.0F,
    };
}

TerrainSourceSummary summarize_terrain_source(const TerrainSourceParameters& parameters,
                                              cubey::math::Vec2 center_xz, float extent_m,
                                              std::uint32_t samples_per_axis) {
    if (!std::isfinite(extent_m) || extent_m <= 0.0F || samples_per_axis < 2U) {
        throw std::runtime_error("invalid terrain source summary domain");
    }
    TerrainSourceSummary result{
        .min_height_m = std::numeric_limits<float>::max(),
        .max_height_m = std::numeric_limits<float>::lowest(),
    };
    double height_sum = 0.0;
    double slope_sum = 0.0;
    const float denominator = static_cast<float>(samples_per_axis - 1U);
    for (std::uint32_t z = 0; z < samples_per_axis; ++z) {
        for (std::uint32_t x = 0; x < samples_per_axis; ++x) {
            const TerrainQuery query{
                .world_xz =
                    {
                        center_xz.x - extent_m * 0.5F +
                            extent_m * static_cast<float>(x) / denominator,
                        center_xz.y - extent_m * 0.5F +
                            extent_m * static_cast<float>(z) / denominator,
                    },
            };
            const TerrainSample sample = sample_terrain(parameters, query);
            result.min_height_m = std::min(result.min_height_m, sample.height_m);
            result.max_height_m = std::max(result.max_height_m, sample.height_m);
            height_sum += sample.height_m;
            slope_sum += std::sqrt(sample.gradient_xz.x * sample.gradient_xz.x +
                                   sample.gradient_xz.y * sample.gradient_xz.y);
        }
    }
    const double sample_count =
        static_cast<double>(samples_per_axis) * static_cast<double>(samples_per_axis);
    result.mean_height_m = static_cast<float>(height_sum / sample_count);
    result.mean_slope = static_cast<float>(slope_sum / sample_count);
    return result;
}

} // namespace cubey::projects::terrain
