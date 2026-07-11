#include "upland_mountain_source.h"

#include "terrain_patch.h"

#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cubey::projects::terrain_hydrology_lab {
namespace {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct NoiseSample {
    float value = 0.0F;
    Vec2 gradient{};
};

[[nodiscard]] float fract_positive(float value) {
    return value - std::floor(value);
}

[[nodiscard]] float smootherstep(float value) {
    return value * value * value * (10.0F + value * (-15.0F + 6.0F * value));
}

[[nodiscard]] float mix(float a, float b, float t) {
    return a + ((b - a) * t);
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] Vec2 rotate(Vec2 value) {
    return {
        .x = (0.82F * value.x) - (0.57F * value.y),
        .y = (0.57F * value.x) + (0.82F * value.y),
    };
}

[[nodiscard]] float seed_component(std::uint64_t seed, int shift, float scale) {
    constexpr std::uint64_t kMask24 = 0x00ff'ffffULL;
    constexpr float kMask24F = 16'777'215.0F;
    const std::uint64_t bits = (seed >> shift) & kMask24;
    return (static_cast<float>(bits) / kMask24F) * scale;
}

[[nodiscard]] Vec2 source_seed(std::uint64_t seed) {
    const std::uint64_t mixed =
        cubey::procedural::derive_seed(seed, "terrain.upland-catchment-v1.source");
    return {
        .x = seed_component(mixed, 0, 17.0F),
        .y = seed_component(mixed, 32, 31.0F),
    };
}

[[nodiscard]] float seeded_hash(Vec2 p, Vec2 seed) {
    const float dot_value = ((p.x + seed.x) * 97.1371F) + ((p.y + seed.y) * 213.357F) + 19.19F;
    return fract_positive(std::sin(dot_value) * 15347.9162F);
}

[[nodiscard]] float value_noise(Vec2 p, Vec2 seed) {
    const Vec2 integer_part{.x = std::floor(p.x), .y = std::floor(p.y)};
    const Vec2 fractional{.x = p.x - integer_part.x, .y = p.y - integer_part.y};
    const float a = seeded_hash(integer_part, seed);
    const float b = seeded_hash({.x = integer_part.x + 1.0F, .y = integer_part.y}, seed);
    const float c = seeded_hash({.x = integer_part.x, .y = integer_part.y + 1.0F}, seed);
    const float d = seeded_hash({.x = integer_part.x + 1.0F, .y = integer_part.y + 1.0F}, seed);
    const float wx = smootherstep(fractional.x);
    const float wy = smootherstep(fractional.y);
    return mix(mix(a, b, wx), mix(c, d, wx), wy);
}

[[nodiscard]] NoiseSample mountain_noise(Vec2 p, Vec2 seed) {
    constexpr float kGradientStep = 0.35F;
    const float center = value_noise(p, seed);
    const float dx = value_noise({.x = p.x + kGradientStep, .y = p.y}, seed) -
                     value_noise({.x = p.x - kGradientStep, .y = p.y}, seed);
    const float dy = value_noise({.x = p.x, .y = p.y + kGradientStep}, seed) -
                     value_noise({.x = p.x, .y = p.y - kGradientStep}, seed);
    return {
        .value = center,
        .gradient =
            {
                .x = dx / (2.0F * kGradientStep),
                .y = dy / (2.0F * kGradientStep),
            },
    };
}

[[nodiscard]] float macro_fbm(Vec2 p, Vec2 seed) {
    float value = 0.0F;
    float amplitude = 0.55F;
    float total_amplitude = 0.0F;
    for (int octave = 0; octave < 5; ++octave) {
        const float octave_f = static_cast<float>(octave);
        value +=
            value_noise(p, {.x = seed.x + (octave_f * 5.31F), .y = seed.y - (octave_f * 3.73F)}) *
            amplitude;
        total_amplitude += amplitude;
        p = rotate({.x = (p.x * 1.93F) + 11.0F, .y = (p.y * 1.93F) - 7.0F});
        amplitude *= 0.52F;
    }
    return total_amplitude > 0.0F ? value / total_amplitude : 0.0F;
}

} // namespace

UplandMountainSampleV1 sample_upland_mountain_v1(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = source_seed(seed);
    Vec2 p{
        .x = (world_x * 0.00026F) + (seed_value.x * 0.137F) + 7.0F,
        .y = (world_z * 0.00026F) + (seed_value.y * 0.113F) - 5.0F,
    };
    const float macro = macro_fbm({.x = p.x * 0.42F, .y = p.y * 0.42F},
                                  {.x = seed_value.x + 2.0F, .y = seed_value.y - 3.0F});
    const float support = smoothstep(0.08F, 0.68F, macro);
    const float range = std::max(support, 0.0F);

    Vec2 warp{};
    float frequency = 1.0F;
    float amplitude = 1.0F;
    float total = 0.0F;
    float total_amplitude = 0.0F;
    for (int octave = 0; octave < 5; ++octave) {
        const float octave_f = static_cast<float>(octave);
        const float octave_weight = octave < 3 ? 1.0F : 0.68F;
        const Vec2 sample_p{
            .x = (p.x * frequency) + warp.x + (octave_f * 17.31F),
            .y = (p.y * frequency) + warp.y - (octave_f * 9.17F),
        };
        const NoiseSample sample =
            mountain_noise(sample_p, {.x = seed_value.x + (octave_f * 1.91F),
                                      .y = seed_value.y - (octave_f * 2.47F)});
        const float ridge =
            std::pow(std::max(1.0F - std::abs((sample.value * 2.0F) - 1.0F), 0.0F), 1.75F);
        const float billow = std::pow(std::max(sample.value, 0.0F), 2.35F);
        const float shaped = mix(billow, ridge, 0.46F);
        total += shaped * amplitude * octave_weight * (0.64F + (0.36F * support));
        total_amplitude += amplitude * octave_weight;
        const float warp_scale = amplitude * octave_weight * (0.66F + (0.04F * octave_f));
        warp.x += sample.gradient.x * warp_scale;
        warp.y += sample.gradient.y * warp_scale;
        frequency *= 1.92F;
        amplitude *= 0.48F + (0.035F * sample.value);
    }

    const float detail = total_amplitude > 0.0F ? total / total_amplitude : 0.0F;
    const float broad_base = std::pow(std::max(macro, 0.0F), 2.20F) * 900.0F;
    const float mountain = std::pow(std::max(detail, 0.0F), 1.08F) * range * 3100.0F;
    const float valley_cut = std::pow(std::max(1.0F - macro, 0.0F), 2.0F) * 180.0F;
    return {
        .height_m = std::max(broad_base + mountain - valley_cut, 0.0F),
        .support = support,
    };
}

cubey::procedural::FieldSet2D sample_upland_mountain_fields_v1(cubey::procedural::Grid2DDesc desc,
                                                               std::uint64_t seed) {
    cubey::procedural::ScalarField2D height(desc, 0.0F);
    cubey::procedural::ScalarField2D support(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const UplandMountainSampleV1 sample =
                sample_upland_mountain_v1(cubey::procedural::grid_sample_x(desc, x),
                                          cubey::procedural::grid_sample_y(desc, y), seed);
            height.at(x, y) = sample.height_m;
            support.at(x, y) = sample.support;
        }
    }

    cubey::procedural::FieldSet2D result(desc);
    result.add_field(std::string(kTerrainFieldSourceHeightM), std::move(height));
    result.add_field(std::string(kTerrainFieldMountainSupport), std::move(support));
    return result;
}

} // namespace cubey::projects::terrain_hydrology_lab
