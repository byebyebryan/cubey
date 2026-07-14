#include "terrain_source.h"

#include <cubey/procedural/noise.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

constexpr float kRotationCos = 0.8F;
constexpr float kRotationSin = 0.6F;
// Neutral filtered ridge value approximates the mean of 1 - abs(OpenSimplex2S).
constexpr float kRidgeNeutral = 0.65F;

struct TerrainBandSplitSample {
    float full = 0.5F;
    float fine_delta = 0.0F;
};

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
        const float neutral = std::lerp(0.5F, kRidgeNeutral, band.ridge_mix);
        float shaped = neutral;
        if (footprint_weight > 0.0F) {
            noise.seed = band.seed + static_cast<std::int32_t>(octave * 1013U);
            const float octave_f = static_cast<float>(octave);
            const float sample = cubey::procedural::sample_coherent_noise_2d(
                rotated.x * frequency + octave_f * 17.31F, rotated.y * frequency - octave_f * 9.17F,
                noise);
            const float unit = sample * 0.5F + 0.5F;
            const float ridge = 1.0F - std::abs(sample);
            shaped = std::lerp(unit, ridge, band.ridge_mix);
        }
        value += std::lerp(neutral, shaped, footprint_weight) * amplitude;
        weight += amplitude;
        frequency *= band.lacunarity;
        amplitude *= band.gain;
    }
    return weight > 0.0F ? value / weight : 0.5F;
}

[[nodiscard]] TerrainBandSplitSample
sample_terrain_band_split(const TerrainSourceBandParameters& band, cubey::math::Vec2 world_xz,
                          float footprint_m, std::uint32_t core_octaves) {
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
    float fine_delta = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < band.octaves; ++octave) {
        const float footprint_weight = octave_footprint_weight(frequency, footprint_m);
        const float neutral = std::lerp(0.5F, kRidgeNeutral, band.ridge_mix);
        float shaped = neutral;
        if (footprint_weight > 0.0F) {
            noise.seed = band.seed + static_cast<std::int32_t>(octave * 1013U);
            const float octave_f = static_cast<float>(octave);
            const float sample = cubey::procedural::sample_coherent_noise_2d(
                rotated.x * frequency + octave_f * 17.31F, rotated.y * frequency - octave_f * 9.17F,
                noise);
            const float unit = sample * 0.5F + 0.5F;
            const float ridge = 1.0F - std::abs(sample);
            shaped = std::lerp(unit, ridge, band.ridge_mix);
        }
        const float filtered = std::lerp(neutral, shaped, footprint_weight);
        value += filtered * amplitude;
        if (octave >= core_octaves) {
            fine_delta += (filtered - neutral) * amplitude;
        }
        weight += amplitude;
        frequency *= band.lacunarity;
        amplitude *= band.gain;
    }
    return weight > 0.0F ? TerrainBandSplitSample{value / weight, fine_delta / weight}
                         : TerrainBandSplitSample{};
}

[[nodiscard]] float sample_v3_signed_band(const TerrainSourceBandParameters& band,
                                          cubey::math::Vec2 world_xz, float footprint_m) {
    cubey::math::Vec2 octave_position{
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
            const float octave_f = static_cast<float>(octave);
            const float sample = cubey::procedural::gradient_noise_3d(
                octave_position.x * frequency + octave_f * 17.31F,
                octave_position.y * frequency - octave_f * 9.17F, octave_f * 0.713F + 0.37F,
                static_cast<std::uint32_t>(band.seed) + octave * 1013U);
            value += sample * footprint_weight * amplitude;
        }
        weight += amplitude;
        octave_position = {
            kRotationCos * octave_position.x - kRotationSin * octave_position.y,
            kRotationSin * octave_position.x + kRotationCos * octave_position.y,
        };
        frequency *= band.lacunarity;
        amplitude *= band.gain;
    }
    return weight > 0.0F ? value / weight : 0.0F;
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

[[nodiscard]] float sample_terrain_weathering_delta(const TerrainSourceParameters& parameters,
                                                    const TerrainQuery& query,
                                                    float center_height_m) {
    if (parameters.weathering != TerrainWeatheringMode::Local ||
        parameters.weathering_strength <= 0.0F ||
        query.footprint_m >= parameters.weathering_radius_m * 0.75F) {
        return 0.0F;
    }

    constexpr float kDiagonal = 0.70710678118F;
    constexpr std::array<cubey::math::Vec2, 8> directions{
        cubey::math::Vec2{1.0F, 0.0F},  cubey::math::Vec2{kDiagonal, kDiagonal},
        cubey::math::Vec2{0.0F, 1.0F},  cubey::math::Vec2{-kDiagonal, kDiagonal},
        cubey::math::Vec2{-1.0F, 0.0F}, cubey::math::Vec2{-kDiagonal, -kDiagonal},
        cubey::math::Vec2{0.0F, -1.0F}, cubey::math::Vec2{kDiagonal, -kDiagonal},
    };

    float neighbor_sum = 0.0F;
    cubey::math::Vec2 gradient_sum{0.0F, 0.0F};
    for (const cubey::math::Vec2 direction : directions) {
        TerrainQuery neighbor_query = query;
        neighbor_query.world_xz += direction * parameters.weathering_radius_m;
        const float neighbor_height = sample_terrain_base_height(parameters, neighbor_query);
        neighbor_sum += neighbor_height;
        gradient_sum += direction * neighbor_height;
    }
    const float neighbor_mean = neighbor_sum / static_cast<float>(directions.size());
    const cubey::math::Vec2 gradient = gradient_sum / (4.0F * parameters.weathering_radius_m);
    const float slope = std::sqrt(gradient.x * gradient.x + gradient.y * gradient.y);
    const float slope_activity = smoothstep(0.04F, 0.55F, slope);
    const float footprint_visibility =
        1.0F - smoothstep(parameters.weathering_radius_m * 0.25F,
                          parameters.weathering_radius_m * 0.75F, query.footprint_m);
    const float curvature_detail = center_height_m - neighbor_mean;
    return std::clamp(curvature_detail * 0.45F * slope_activity * footprint_visibility *
                          parameters.weathering_strength,
                      -parameters.weathering_max_delta_m, parameters.weathering_max_delta_m);
}

[[nodiscard]] float sample_terrain_height(const TerrainSourceParameters& parameters,
                                          const TerrainQuery& query) {
    const float base_height = sample_terrain_base_height(parameters, query);
    return base_height + sample_terrain_weathering_delta(parameters, query, base_height);
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

std::string_view terrain_source_version_name(TerrainSourceVersion version) {
    switch (version) {
    case TerrainSourceVersion::V1:
        return "v1";
    case TerrainSourceVersion::V2:
        return "v2";
    case TerrainSourceVersion::V2_1:
        return "v2.1";
    case TerrainSourceVersion::V3:
        return "v3";
    }
    throw std::runtime_error("unknown terrain source version");
}

TerrainSourceVersion terrain_source_version_from_name(std::string_view name) {
    if (name.empty() || name == "v1") {
        return TerrainSourceVersion::V1;
    }
    if (name == "v2") {
        return TerrainSourceVersion::V2;
    }
    if (name == "v2.1") {
        return TerrainSourceVersion::V2_1;
    }
    if (name == "v3") {
        return TerrainSourceVersion::V3;
    }
    throw std::runtime_error("unknown terrain source version: " + std::string(name));
}

void validate_terrain_source_config(const TerrainSourceConfig& config) {
    if (config.version != TerrainSourceVersion::V1 && config.preset != TerrainPreset::Mountain) {
        throw std::runtime_error(
            "terrain source v2/v2.1/v3 currently supports only the mountain preset");
    }
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
    if (parameters.version == TerrainSourceVersion::V2_1) {
        if (parameters.v2_1.core_detail_octaves == 0U ||
            parameters.v2_1.core_detail_octaves >= parameters.detail.octaves ||
            !std::isfinite(parameters.v2_1.fine_detail_strength) ||
            !std::isfinite(parameters.v2_1.fine_detail_cap_m) ||
            parameters.v2_1.fine_detail_strength < 0.0F ||
            parameters.v2_1.fine_detail_cap_m < 0.0F) {
            throw std::runtime_error("invalid terrain source v2.1 parameters");
        }
    }
    if (parameters.version == TerrainSourceVersion::V3) {
        validate_band(parameters.v3.warp);
        validate_band(parameters.v3.range);
        validate_band(parameters.v3.massif);
        validate_band(parameters.v3.ridge);
        validate_band(parameters.v3.meso);
        if (!std::isfinite(parameters.v3.warp_strength_m) ||
            !std::isfinite(parameters.v3.valley_ratio) ||
            !std::isfinite(parameters.v3.valley_cap_m) ||
            !std::isfinite(parameters.v3.ridge_ratio) ||
            !std::isfinite(parameters.v3.ridge_cap_m) || !std::isfinite(parameters.v3.meso_ratio) ||
            !std::isfinite(parameters.v3.meso_cap_m) || parameters.v3.warp_strength_m < 0.0F ||
            parameters.v3.valley_ratio < 0.0F || parameters.v3.valley_ratio > 1.0F ||
            parameters.v3.valley_cap_m < 0.0F || parameters.v3.ridge_ratio < 0.0F ||
            parameters.v3.ridge_ratio > 1.0F || parameters.v3.ridge_cap_m < 0.0F ||
            parameters.v3.meso_ratio < 0.0F || parameters.v3.meso_ratio > 1.0F ||
            parameters.v3.meso_cap_m < 0.0F) {
            throw std::runtime_error("invalid terrain source v3 parameters");
        }
    }
    if (!std::isfinite(parameters.height_scale_m) || parameters.height_scale_m <= 0.0F ||
        !std::isfinite(parameters.elevation_power) || parameters.elevation_power <= 0.0F ||
        !std::isfinite(parameters.gradient_step_m) || parameters.gradient_step_m <= 0.0F ||
        !std::isfinite(parameters.weathering_radius_m) || parameters.weathering_radius_m <= 0.0F ||
        !std::isfinite(parameters.weathering_max_delta_m) ||
        parameters.weathering_max_delta_m < 0.0F ||
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
    result.version = config.version;
    result.weathering = config.weathering;
    result.weathering_strength = config.weathering_strength;
    switch (config.preset) {
    case TerrainPreset::Mountain:
        result.macro = band(macro_seed, 4U, 14'000.0F, 2.01F, 0.50F, 0.02F);
        result.structure = band(structure_seed, 5U, 4'200.0F, 1.98F, 0.48F, 0.28F);
        result.detail = band(detail_seed, 4U, 900.0F, 2.03F, 0.43F, 0.20F);
        result.macro_weight = 0.50F;
        result.structure_weight = 0.52F;
        result.detail_weight = 0.10F;
        result.elevation_bias = -0.08F;
        result.height_scale_m = 5'500.0F;
        result.elevation_power = 2.55F;
        result.gradient_step_m = 2.0F;
        result.weathering_radius_m = 18.0F;
        result.weathering_max_delta_m = 60.0F;
        if (config.version == TerrainSourceVersion::V2 ||
            config.version == TerrainSourceVersion::V2_1) {
            result.detail = band(detail_seed, 8U, 900.0F, 2.03F, 0.52F, 0.24F);
            result.detail_weight = 0.16F;
            if (config.version == TerrainSourceVersion::V2_1) {
                result.v2_1.core_detail_octaves = 3U;
                result.v2_1.fine_detail_strength = 0.50F;
                result.v2_1.fine_detail_cap_m = 30.0F;
            }
        } else if (config.version == TerrainSourceVersion::V3) {
            result.v3.warp = band(terrain_band_seed(config.seed, "terrain.v3.warp"), 2U, 32'000.0F,
                                  2.0F, 0.50F, 0.0F);
            result.v3.range = band(terrain_band_seed(config.seed, "terrain.v3.range"), 3U,
                                   24'000.0F, 2.0F, 0.50F, 0.0F);
            result.v3.massif = band(terrain_band_seed(config.seed, "terrain.v3.massif"), 6U,
                                    8'000.0F, 2.0F, 0.55F, 0.0F);
            result.v3.ridge = band(terrain_band_seed(config.seed, "terrain.v3.ridge"), 5U, 6'000.0F,
                                   2.0F, 0.52F, 0.0F);
            result.v3.meso = band(terrain_band_seed(config.seed, "terrain.v3.meso"), 4U, 1'200.0F,
                                  2.0F, 0.45F, 0.0F);
            result.v3.warp_strength_m = 2'000.0F;
            result.v3.valley_ratio = 0.65F;
            result.v3.valley_cap_m = 600.0F;
            result.v3.ridge_ratio = 0.14F;
            result.v3.ridge_cap_m = 450.0F;
            result.v3.meso_ratio = 0.05F;
            result.v3.meso_cap_m = 140.0F;
            result.height_scale_m = 4'000.0F;
            result.elevation_power = 1.55F;
        }
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

TerrainSourceComponents sample_terrain_source_components(const TerrainSourceParameters& parameters,
                                                         const TerrainQuery& query) {
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain query");
    }
    if (parameters.version != TerrainSourceVersion::V3) {
        return {};
    }

    TerrainSourceBandParameters warp_z_band = parameters.v3.warp;
    warp_z_band.seed += 7'919;
    const float warp_x = sample_v3_signed_band(
        parameters.v3.warp, query.world_xz + cubey::math::Vec2{12'031.0F, -4'507.0F},
        query.footprint_m);
    const float warp_z = sample_v3_signed_band(
        warp_z_band, query.world_xz + cubey::math::Vec2{-8'213.0F, 15'119.0F}, query.footprint_m);
    const cubey::math::Vec2 warped =
        query.world_xz + cubey::math::Vec2{warp_x, warp_z} * parameters.v3.warp_strength_m;

    const float range_noise = sample_v3_signed_band(parameters.v3.range, warped, query.footprint_m);
    const float range_support = smoothstep(-0.32F, 0.42F, range_noise);
    const float massif_noise = sample_v3_signed_band(
        parameters.v3.massif, warped + cubey::math::Vec2{3'107.0F, -1'903.0F}, query.footprint_m);
    const float massif_unit = std::clamp(massif_noise * 0.5F + 0.5F, 0.0F, 1.0F);
    const float massif_shape = 0.20F + 0.80F * smoothstep(0.28F, 0.74F, massif_unit);
    const float macro_profile = std::pow(range_support, 1.45F) * massif_shape;
    const float massif_height_m =
        parameters.base_height_m +
        parameters.height_scale_m * std::pow(macro_profile, parameters.elevation_power);

    const float valley_gate = 1.0F - smoothstep(0.26F, 0.48F, massif_unit);
    const float valley_delta_m =
        -std::min(massif_height_m * parameters.v3.valley_ratio, parameters.v3.valley_cap_m) *
        valley_gate * range_support;

    const float ridge_field = sample_v3_signed_band(
        parameters.v3.ridge, warped + cubey::math::Vec2{-2'411.0F, 5'327.0F}, query.footprint_m);
    const float ridge_signal = std::clamp(1.0F - std::abs(ridge_field), 0.0F, 1.0F);
    const float ridge_body = ridge_signal * ridge_signal * ridge_signal * ridge_signal;
    const float highland_gate = smoothstep(0.10F, 0.58F, macro_profile);
    const float ridge_delta_m =
        std::min(massif_height_m * parameters.v3.ridge_ratio, parameters.v3.ridge_cap_m) *
        ridge_body * highland_gate;

    const float meso_field = sample_v3_signed_band(
        parameters.v3.meso, warped + cubey::math::Vec2{1'127.0F, 2'813.0F}, query.footprint_m);
    const float face_gate =
        smoothstep(0.10F, 0.48F, macro_profile) * (1.0F - smoothstep(0.78F, 0.96F, macro_profile));
    const float meso_delta_m =
        meso_field *
        std::min(massif_height_m * parameters.v3.meso_ratio, parameters.v3.meso_cap_m) * face_gate;
    const float base_height_m =
        std::max(massif_height_m + valley_delta_m + ridge_delta_m + meso_delta_m, 0.0F);
    return {
        .range_support = range_support,
        .massif_height_m = massif_height_m,
        .valley_delta_m = valley_delta_m,
        .ridge_delta_m = ridge_delta_m,
        .meso_delta_m = meso_delta_m,
        .base_height_m = base_height_m,
    };
}

float sample_terrain_base_height(const TerrainSourceParameters& parameters,
                                 const TerrainQuery& query) {
    if (parameters.version == TerrainSourceVersion::V3) {
        return sample_terrain_source_components(parameters, query).base_height_m;
    }
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain query");
    }
    const float macro = sample_terrain_band(parameters.macro, query.world_xz, query.footprint_m);
    const float structure =
        sample_terrain_band(parameters.structure, query.world_xz, query.footprint_m);
    TerrainBandSplitSample split_detail{};
    const bool split_fine_detail = parameters.version == TerrainSourceVersion::V2_1;
    float detail = 0.0F;
    if (split_fine_detail) {
        split_detail =
            sample_terrain_band_split(parameters.detail, query.world_xz, query.footprint_m,
                                      parameters.v2_1.core_detail_octaves);
        detail = split_detail.full;
    } else {
        detail = sample_terrain_band(parameters.detail, query.world_xz, query.footprint_m);
    }
    const float mass = smoothstep(0.18F, 0.82F, macro);
    const float structured = structure * (0.30F + 0.70F * mass);
    const float detail_gate = 0.15F + 0.85F * mass;
    const float local_detail =
        (detail - (split_fine_detail ? split_detail.fine_delta : 0.0F) - 0.5F) * detail_gate;
    const float composed =
        std::clamp(parameters.macro_weight * macro + parameters.structure_weight * structured +
                       parameters.detail_weight * local_detail + parameters.elevation_bias,
                   0.0F, 1.0F);
    const float core_height =
        parameters.base_height_m +
        parameters.height_scale_m * std::pow(composed, parameters.elevation_power);
    if (!split_fine_detail) {
        return core_height;
    }
    const float fine_detail_m =
        std::clamp(parameters.height_scale_m * parameters.detail_weight *
                       parameters.v2_1.fine_detail_strength * split_detail.fine_delta * detail_gate,
                   -parameters.v2_1.fine_detail_cap_m, parameters.v2_1.fine_detail_cap_m);
    return std::max(core_height + fine_detail_m, parameters.base_height_m);
}

TerrainSample sample_terrain(const TerrainSourceParameters& parameters, const TerrainQuery& query) {
    const float base_height = sample_terrain_base_height(parameters, query);
    const float center = sample_terrain_height(parameters, query);
    const float step_m = std::max(parameters.gradient_step_m, query.footprint_m * 0.5F);
    TerrainQuery offset = query;
    offset.world_xz.x -= step_m;
    const float x0 = sample_terrain_height(parameters, offset);
    offset.world_xz.x += 2.0F * step_m;
    const float x1 = sample_terrain_height(parameters, offset);
    offset = query;
    offset.world_xz.y -= step_m;
    const float z0 = sample_terrain_height(parameters, offset);
    offset.world_xz.y += 2.0F * step_m;
    const float z1 = sample_terrain_height(parameters, offset);
    return {
        .base_height_m = base_height,
        .height_m = center,
        .gradient_xz = {(x1 - x0) / (2.0F * step_m), (z1 - z0) / (2.0F * step_m)},
        .weathering_delta_m = center - base_height,
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

TerrainSourceComponentSummary
summarize_terrain_source_components(const TerrainSourceParameters& parameters,
                                    cubey::math::Vec2 center_xz, float extent_m,
                                    std::uint32_t samples_per_axis) {
    if (parameters.version != TerrainSourceVersion::V3 || !std::isfinite(extent_m) ||
        extent_m <= 0.0F || samples_per_axis < 2U) {
        throw std::runtime_error("invalid terrain source component summary domain");
    }
    TerrainSourceComponentSummary result{};
    double range_sum = 0.0;
    double range_coverage = 0.0;
    double massif_squared_sum = 0.0;
    double valley_squared_sum = 0.0;
    double ridge_squared_sum = 0.0;
    double meso_squared_sum = 0.0;
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
            const TerrainSourceComponents components =
                sample_terrain_source_components(parameters, query);
            range_sum += components.range_support;
            range_coverage += components.range_support >= 0.5F ? 1.0 : 0.0;
            massif_squared_sum += static_cast<double>(components.massif_height_m) *
                                  static_cast<double>(components.massif_height_m);
            valley_squared_sum += static_cast<double>(components.valley_delta_m) *
                                  static_cast<double>(components.valley_delta_m);
            ridge_squared_sum += static_cast<double>(components.ridge_delta_m) *
                                 static_cast<double>(components.ridge_delta_m);
            meso_squared_sum += static_cast<double>(components.meso_delta_m) *
                                static_cast<double>(components.meso_delta_m);
            result.massif_max_m = std::max(result.massif_max_m, components.massif_height_m);
            result.valley_max_abs_m =
                std::max(result.valley_max_abs_m, std::abs(components.valley_delta_m));
            result.ridge_max_m = std::max(result.ridge_max_m, components.ridge_delta_m);
            result.meso_max_abs_m =
                std::max(result.meso_max_abs_m, std::abs(components.meso_delta_m));
        }
    }
    const double sample_count =
        static_cast<double>(samples_per_axis) * static_cast<double>(samples_per_axis);
    result.range_support_mean = static_cast<float>(range_sum / sample_count);
    result.range_support_coverage = static_cast<float>(range_coverage / sample_count);
    result.massif_rms_m = static_cast<float>(std::sqrt(massif_squared_sum / sample_count));
    result.valley_rms_m = static_cast<float>(std::sqrt(valley_squared_sum / sample_count));
    result.ridge_rms_m = static_cast<float>(std::sqrt(ridge_squared_sum / sample_count));
    result.meso_rms_m = static_cast<float>(std::sqrt(meso_squared_sum / sample_count));
    return result;
}

TerrainSourceScaleResponseSummary
summarize_terrain_source_scale_response(const TerrainSourceParameters& parameters,
                                        cubey::math::Vec2 center_xz, float extent_m,
                                        std::uint32_t samples_per_axis) {
    if (!std::isfinite(extent_m) || extent_m <= 0.0F || samples_per_axis < 2U) {
        throw std::runtime_error("invalid terrain source scale response domain");
    }
    double fine_squared_sum = 0.0;
    double meso_squared_sum = 0.0;
    double structure_squared_sum = 0.0;
    const float denominator = static_cast<float>(samples_per_axis - 1U);
    for (std::uint32_t z = 0; z < samples_per_axis; ++z) {
        for (std::uint32_t x = 0; x < samples_per_axis; ++x) {
            const cubey::math::Vec2 world_xz{
                center_xz.x - extent_m * 0.5F + extent_m * static_cast<float>(x) / denominator,
                center_xz.y - extent_m * 0.5F + extent_m * static_cast<float>(z) / denominator,
            };
            const float height_0 =
                sample_terrain_base_height(parameters, {.world_xz = world_xz, .footprint_m = 0.0F});
            const float height_64 = sample_terrain_base_height(
                parameters, {.world_xz = world_xz, .footprint_m = 64.0F});
            const float height_256 = sample_terrain_base_height(
                parameters, {.world_xz = world_xz, .footprint_m = 256.0F});
            const float height_1024 = sample_terrain_base_height(
                parameters, {.world_xz = world_xz, .footprint_m = 1024.0F});
            const double fine = static_cast<double>(height_0 - height_64);
            const double meso = static_cast<double>(height_64 - height_256);
            const double structure = static_cast<double>(height_256 - height_1024);
            fine_squared_sum += fine * fine;
            meso_squared_sum += meso * meso;
            structure_squared_sum += structure * structure;
        }
    }
    const double sample_count =
        static_cast<double>(samples_per_axis) * static_cast<double>(samples_per_axis);
    return {
        .fine_residual_rms_m = static_cast<float>(std::sqrt(fine_squared_sum / sample_count)),
        .meso_residual_rms_m = static_cast<float>(std::sqrt(meso_squared_sum / sample_count)),
        .structure_residual_rms_m =
            static_cast<float>(std::sqrt(structure_squared_sum / sample_count)),
    };
}

} // namespace cubey::projects::terrain
