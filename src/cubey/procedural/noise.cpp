#include <cubey/procedural/noise.h>

#include <cubey/procedural/operators.h>

#include <cmath>
#include <limits>

namespace cubey::procedural {

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

} // namespace cubey::procedural
