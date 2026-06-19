#include <cubey/procedural/noise.h>

#include <cubey/procedural/operators.h>

#include <FastNoiseLite.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cubey::procedural {
namespace {

[[nodiscard]] FastNoiseLite::NoiseType to_fast_noise_type(CoherentNoiseType type) {
    switch (type) {
    case CoherentNoiseType::OpenSimplex2:
        return FastNoiseLite::NoiseType_OpenSimplex2;
    case CoherentNoiseType::OpenSimplex2S:
        return FastNoiseLite::NoiseType_OpenSimplex2S;
    case CoherentNoiseType::Cellular:
        return FastNoiseLite::NoiseType_Cellular;
    case CoherentNoiseType::Perlin:
        return FastNoiseLite::NoiseType_Perlin;
    case CoherentNoiseType::ValueCubic:
        return FastNoiseLite::NoiseType_ValueCubic;
    case CoherentNoiseType::Value:
        return FastNoiseLite::NoiseType_Value;
    }
    return FastNoiseLite::NoiseType_OpenSimplex2;
}

[[nodiscard]] FastNoiseLite::FractalType to_fast_fractal_type(CoherentFractalType type) {
    switch (type) {
    case CoherentFractalType::None:
        return FastNoiseLite::FractalType_None;
    case CoherentFractalType::Fbm:
        return FastNoiseLite::FractalType_FBm;
    case CoherentFractalType::Ridged:
        return FastNoiseLite::FractalType_Ridged;
    case CoherentFractalType::PingPong:
        return FastNoiseLite::FractalType_PingPong;
    }
    return FastNoiseLite::FractalType_None;
}

[[nodiscard]] FastNoiseLite::CellularDistanceFunction
to_fast_cellular_distance(CoherentCellularDistance distance) {
    switch (distance) {
    case CoherentCellularDistance::Euclidean:
        return FastNoiseLite::CellularDistanceFunction_Euclidean;
    case CoherentCellularDistance::EuclideanSquared:
        return FastNoiseLite::CellularDistanceFunction_EuclideanSq;
    case CoherentCellularDistance::Manhattan:
        return FastNoiseLite::CellularDistanceFunction_Manhattan;
    case CoherentCellularDistance::Hybrid:
        return FastNoiseLite::CellularDistanceFunction_Hybrid;
    }
    return FastNoiseLite::CellularDistanceFunction_EuclideanSq;
}

[[nodiscard]] FastNoiseLite::CellularReturnType
to_fast_cellular_return(CoherentCellularReturn return_type) {
    switch (return_type) {
    case CoherentCellularReturn::CellValue:
        return FastNoiseLite::CellularReturnType_CellValue;
    case CoherentCellularReturn::Distance:
        return FastNoiseLite::CellularReturnType_Distance;
    case CoherentCellularReturn::Distance2:
        return FastNoiseLite::CellularReturnType_Distance2;
    case CoherentCellularReturn::Distance2Add:
        return FastNoiseLite::CellularReturnType_Distance2Add;
    case CoherentCellularReturn::Distance2Sub:
        return FastNoiseLite::CellularReturnType_Distance2Sub;
    case CoherentCellularReturn::Distance2Mul:
        return FastNoiseLite::CellularReturnType_Distance2Mul;
    case CoherentCellularReturn::Distance2Div:
        return FastNoiseLite::CellularReturnType_Distance2Div;
    }
    return FastNoiseLite::CellularReturnType_Distance;
}

[[nodiscard]] FastNoiseLite::DomainWarpType to_fast_domain_warp_type(
    CoherentDomainWarpType type) {
    switch (type) {
    case CoherentDomainWarpType::OpenSimplex2:
        return FastNoiseLite::DomainWarpType_OpenSimplex2;
    case CoherentDomainWarpType::OpenSimplex2Reduced:
        return FastNoiseLite::DomainWarpType_OpenSimplex2Reduced;
    case CoherentDomainWarpType::BasicGrid:
        return FastNoiseLite::DomainWarpType_BasicGrid;
    }
    return FastNoiseLite::DomainWarpType_OpenSimplex2;
}

[[nodiscard]] FastNoiseLite::FractalType to_fast_domain_warp_fractal_type(
    CoherentDomainWarpFractalType type) {
    switch (type) {
    case CoherentDomainWarpFractalType::None:
        return FastNoiseLite::FractalType_None;
    case CoherentDomainWarpFractalType::Progressive:
        return FastNoiseLite::FractalType_DomainWarpProgressive;
    case CoherentDomainWarpFractalType::Independent:
        return FastNoiseLite::FractalType_DomainWarpIndependent;
    }
    return FastNoiseLite::FractalType_None;
}

[[nodiscard]] int octave_count(std::uint32_t octaves) {
    return static_cast<int>(std::max<std::uint32_t>(octaves, 1U));
}

[[nodiscard]] FastNoiseLite make_noise(const CoherentNoiseConfig& config) {
    FastNoiseLite noise(static_cast<int>(config.seed));
    noise.SetFrequency(config.frequency);
    noise.SetNoiseType(to_fast_noise_type(config.noise_type));
    noise.SetFractalType(to_fast_fractal_type(config.fractal_type));
    noise.SetFractalOctaves(octave_count(config.octaves));
    noise.SetFractalLacunarity(config.lacunarity);
    noise.SetFractalGain(config.gain);
    noise.SetFractalWeightedStrength(config.weighted_strength);
    noise.SetFractalPingPongStrength(config.ping_pong_strength);
    noise.SetCellularDistanceFunction(to_fast_cellular_distance(config.cellular_distance));
    noise.SetCellularReturnType(to_fast_cellular_return(config.cellular_return));
    noise.SetCellularJitter(config.cellular_jitter);
    return noise;
}

[[nodiscard]] FastNoiseLite make_domain_warp_noise(const CoherentDomainWarpConfig& config) {
    FastNoiseLite noise(static_cast<int>(config.seed));
    noise.SetFrequency(config.frequency);
    noise.SetDomainWarpType(to_fast_domain_warp_type(config.warp_type));
    noise.SetDomainWarpAmp(config.amplitude);
    noise.SetFractalType(to_fast_domain_warp_fractal_type(config.fractal_type));
    noise.SetFractalOctaves(octave_count(config.octaves));
    noise.SetFractalLacunarity(config.lacunarity);
    noise.SetFractalGain(config.gain);
    noise.SetFractalWeightedStrength(config.weighted_strength);
    noise.SetFractalPingPongStrength(config.ping_pong_strength);
    return noise;
}

} // namespace

float sample_coherent_noise_2d(float x, float y, const CoherentNoiseConfig& config) {
    FastNoiseLite noise = make_noise(config);
    return noise.GetNoise(x, y);
}

float sample_coherent_noise_3d(float x, float y, float z, const CoherentNoiseConfig& config) {
    FastNoiseLite noise = make_noise(config);
    return noise.GetNoise(x, y, z);
}

CoherentWarp2D domain_warp_2d(float x, float y, const CoherentDomainWarpConfig& config) {
    FastNoiseLite noise = make_domain_warp_noise(config);
    noise.DomainWarp(x, y);
    return {.x = x, .y = y};
}

CoherentWarp3D domain_warp_3d(float x, float y, float z,
                              const CoherentDomainWarpConfig& config) {
    FastNoiseLite noise = make_domain_warp_noise(config);
    noise.DomainWarp(x, y, z);
    return {.x = x, .y = y, .z = z};
}

std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::uint64_t seed) {
    std::uint64_t value = seed;
    value ^= static_cast<std::uint32_t>(x) + 0x9e37'79b9U + (value << 6U) + (value >> 2U);
    value ^= static_cast<std::uint32_t>(y) + 0x85eb'ca6bU + (value << 6U) + (value >> 2U);
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return static_cast<std::uint32_t>(value);
}

std::uint32_t hash_u32(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    std::uint32_t value = seed;
    value ^= static_cast<std::uint32_t>(x) * 0x9e3779b9U;
    value ^= static_cast<std::uint32_t>(y) * 0x85ebca6bU;
    value ^= static_cast<std::uint32_t>(z) * 0xc2b2ae35U;
    return hash_u32(value);
}

float hash_to_unit(std::uint32_t value) {
    constexpr float kInv24Bit = 1.0F / 16'777'215.0F;
    return static_cast<float>(hash_u32(value) >> 8U) * kInv24Bit;
}

float hash_to_unit_masked_24(std::uint32_t value) {
    constexpr float kInv24Bit = 1.0F / 16'777'216.0F;
    return static_cast<float>(hash_u32(value) & 0x00ff'ffffU) * kInv24Bit;
}

float random01(std::uint64_t seed, std::uint32_t index, std::uint32_t channel) {
    constexpr float kScale = 1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(hash_u32(static_cast<std::int32_t>(index),
                                       static_cast<std::int32_t>(channel), seed)) *
           kScale;
}

float value_noise_2d(float x, float y, std::uint64_t seed) {
    const float floor_x = std::floor(x);
    const float floor_y = std::floor(y);
    const auto x0 = static_cast<std::int32_t>(floor_x);
    const auto y0 = static_cast<std::int32_t>(floor_y);
    const float tx = smoothstep(0.0F, 1.0F, x - floor_x);
    const float ty = smoothstep(0.0F, 1.0F, y - floor_y);

    const auto corner = [seed](std::int32_t ix, std::int32_t iy) {
        constexpr float kScale =
            1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
        return (static_cast<float>(hash_u32(ix, iy, seed)) * kScale * 2.0F) - 1.0F;
    };

    const float a = lerp(corner(x0, y0), corner(x0 + 1, y0), tx);
    const float b = lerp(corner(x0, y0 + 1), corner(x0 + 1, y0 + 1), tx);
    return lerp(a, b, ty);
}

float fbm_2d(float x, float y, std::uint64_t seed, const Fbm2DConfig& config) {
    float frequency = 1.0F;
    float amplitude = config.initial_amplitude;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < config.octaves; ++octave) {
        sum += value_noise_2d(x * frequency, y * frequency,
                              seed + static_cast<std::uint64_t>(octave) * config.seed_stride) *
               amplitude;
        weight += amplitude;
        frequency *= config.lacunarity;
        amplitude *= config.gain;
    }
    return weight == 0.0F ? 0.0F : sum / weight;
}

float ridged_fbm_2d(float x, float y, std::uint64_t seed, const Fbm2DConfig& config) {
    const float noise = fbm_2d(x, y, seed, config);
    const float ridge = 1.0F - std::abs(noise);
    return ridge * ridge;
}

float value_noise_3d(float x, float y, float z, std::uint32_t seed) {
    const float floor_x = std::floor(x);
    const float floor_y = std::floor(y);
    const float floor_z = std::floor(z);
    const auto x0 = static_cast<std::int32_t>(floor_x);
    const auto y0 = static_cast<std::int32_t>(floor_y);
    const auto z0 = static_cast<std::int32_t>(floor_z);
    const float tx = smootherstep01(x - floor_x);
    const float ty = smootherstep01(y - floor_y);
    const float tz = smootherstep01(z - floor_z);

    const auto corner = [seed](std::int32_t ix, std::int32_t iy, std::int32_t iz) {
        constexpr float kInv24Bit = 1.0F / 16'777'215.0F;
        return static_cast<float>(hash_u32(ix, iy, iz, seed) >> 8U) * kInv24Bit * 2.0F -
               1.0F;
    };

    const float x00 = lerp(corner(x0, y0, z0), corner(x0 + 1, y0, z0), tx);
    const float x10 = lerp(corner(x0, y0 + 1, z0), corner(x0 + 1, y0 + 1, z0), tx);
    const float x01 = lerp(corner(x0, y0, z0 + 1), corner(x0 + 1, y0, z0 + 1), tx);
    const float x11 = lerp(corner(x0, y0 + 1, z0 + 1), corner(x0 + 1, y0 + 1, z0 + 1), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

float fbm_3d(float x, float y, float z, std::uint32_t seed, const Fbm3DConfig& config) {
    float frequency = 1.0F;
    float amplitude = config.initial_amplitude;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < config.octaves; ++octave) {
        sum += value_noise_3d(x * frequency, y * frequency, z * frequency,
                              seed + octave * config.seed_stride) *
               amplitude;
        weight += amplitude;
        frequency *= config.lacunarity;
        amplitude *= config.gain;
    }
    return weight == 0.0F ? 0.0F : sum / weight;
}

float ridged_fbm_3d(float x, float y, float z, std::uint32_t seed, const Fbm3DConfig& config) {
    const float noise = fbm_3d(x, y, z, seed, config);
    const float ridge = 1.0F - std::abs(noise);
    return ridge * ridge;
}

} // namespace cubey::procedural
