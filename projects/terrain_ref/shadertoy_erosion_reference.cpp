#include "shadertoy_erosion_reference.h"

#include "terrain_engine_reference.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cubey::projects::terrain_ref {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTau = 2.0F * kPi;
constexpr float kBaseFrequency = 1.0F / 14000.0F;
constexpr float kBaseHeightM = 180.0F;
constexpr float kBaseReliefM = 3200.0F;
constexpr float kErosionScaleM = 1800.0F;
constexpr float kErosionStrengthM = 240.0F;
constexpr float kErosionGain = 0.5F;
constexpr float kErosionLacunarity = 2.0F;
constexpr int kErosionOctaves = 5;

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct HeightSlope {
    float height = 0.0F;
    Vec2 gradient{};
};

struct DirectionalSample {
    float value = 0.0F;
    Vec2 gradient{};
};

[[nodiscard]] Vec2 operator+(Vec2 lhs, Vec2 rhs) {
    return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
}

[[nodiscard]] Vec2 operator-(Vec2 lhs, Vec2 rhs) {
    return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y};
}

[[nodiscard]] Vec2 operator*(Vec2 value, float scale) {
    return {.x = value.x * scale, .y = value.y * scale};
}

[[nodiscard]] Vec2 operator/(Vec2 value, float scale) {
    return {.x = value.x / scale, .y = value.y / scale};
}

Vec2& operator+=(Vec2& lhs, Vec2 rhs) {
    lhs.x += rhs.x;
    lhs.y += rhs.y;
    return lhs;
}

[[nodiscard]] float dot(Vec2 lhs, Vec2 rhs) {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y);
}

[[nodiscard]] float length(Vec2 value) {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] Vec2 rotate(Vec2 value, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {
        .x = (cosine * value.x) - (sine * value.y),
        .y = (sine * value.x) + (cosine * value.y),
    };
}

[[nodiscard]] float fract_positive(float value) {
    return value - std::floor(value);
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] float smootherstep01(float value) {
    return value * value * value * (value * (value * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] float smootherstep_derivative(float value) {
    return 30.0F * value * value * (value * (value - 2.0F) + 1.0F);
}

[[nodiscard]] float hash01(Vec2 p, Vec2 seed, float salt) {
    const float value = ((p.x + seed.x + salt) * 127.1F) + ((p.y + seed.y - salt * 0.37F) * 311.7F);
    return fract_positive(std::sin(value) * 43758.5453F);
}

[[nodiscard]] Vec2 gradient_hash(Vec2 p, Vec2 seed) {
    const float angle = hash01(p, seed, 0.0F) * kTau;
    return {.x = std::cos(angle), .y = std::sin(angle)};
}

[[nodiscard]] HeightSlope gradient_noise(Vec2 p, Vec2 seed) {
    const Vec2 integer{.x = std::floor(p.x), .y = std::floor(p.y)};
    const Vec2 fractional = p - integer;
    const Vec2 ga = gradient_hash(integer, seed);
    const Vec2 gb = gradient_hash(integer + Vec2{.x = 1.0F}, seed);
    const Vec2 gc = gradient_hash(integer + Vec2{.y = 1.0F}, seed);
    const Vec2 gd = gradient_hash(integer + Vec2{.x = 1.0F, .y = 1.0F}, seed);
    const float va = dot(ga, fractional);
    const float vb = dot(gb, fractional - Vec2{.x = 1.0F});
    const float vc = dot(gc, fractional - Vec2{.y = 1.0F});
    const float vd = dot(gd, fractional - Vec2{.x = 1.0F, .y = 1.0F});
    const float ux = smootherstep01(fractional.x);
    const float uy = smootherstep01(fractional.y);
    const float dux = smootherstep_derivative(fractional.x);
    const float duy = smootherstep_derivative(fractional.y);
    const float x0 = va + ((vb - va) * ux);
    const float x1 = vc + ((vd - vc) * ux);
    const float dx0 = ga.x + ((gb.x - ga.x) * ux) + (vb - va) * dux;
    const float dx1 = gc.x + ((gd.x - gc.x) * ux) + (vd - vc) * dux;
    const float dy0 = ga.y + ((gb.y - ga.y) * ux);
    const float dy1 = gc.y + ((gd.y - gc.y) * ux);
    return {
        .height = x0 + ((x1 - x0) * uy),
        .gradient =
            {
                .x = dx0 + ((dx1 - dx0) * uy),
                .y = dy0 + ((dy1 - dy0) * uy) + (x1 - x0) * duy,
            },
    };
}

[[nodiscard]] Vec2 seed_value(std::uint64_t seed) {
    const TerrainEngineReferenceSeedComponents components =
        terrain_engine_reference_seed_components(seed);
    return {.x = components.x, .y = components.y};
}

[[nodiscard]] HeightSlope base_mountain(Vec2 world, Vec2 seed) {
    constexpr std::array<float, 4> kAmplitudes{1.0F, 0.42F, 0.18F, 0.075F};
    float raw = 0.0F;
    Vec2 raw_gradient{};
    float amplitude_sum = 0.0F;
    float frequency = kBaseFrequency;
    for (std::size_t octave = 0; octave < kAmplitudes.size(); ++octave) {
        const float angle = static_cast<float>(octave) * 0.61F;
        const Vec2 domain = rotate(world * frequency, angle);
        const HeightSlope noise =
            gradient_noise(domain, seed + Vec2{.x = static_cast<float>(octave) * 7.13F,
                                               .y = -static_cast<float>(octave) * 5.71F});
        const float amplitude = kAmplitudes[octave];
        raw += noise.height * amplitude;
        raw_gradient += rotate(noise.gradient, -angle) * (frequency * amplitude);
        amplitude_sum += amplitude;
        frequency *= 2.03F;
    }

    const float normalized = raw / amplitude_sum;
    const Vec2 normalized_gradient = raw_gradient / amplitude_sum;
    const float unit = std::clamp((normalized * 0.82F) + 0.52F, 0.0F, 1.0F);
    const float profile_t = std::clamp((unit - 0.24F) / 0.56F, 0.0F, 1.0F);
    const float profile = profile_t * profile_t * (3.0F - (2.0F * profile_t));
    const float profile_derivative = profile_t > 0.0F && profile_t < 1.0F
                                         ? (6.0F * profile_t * (1.0F - profile_t) / 0.56F) * 0.82F
                                         : 0.0F;
    return {
        .height = kBaseHeightM + (profile * kBaseReliefM) + (normalized * 180.0F),
        .gradient = normalized_gradient * ((profile_derivative * kBaseReliefM) + 180.0F),
    };
}

[[nodiscard]] DirectionalSample directional_cells(Vec2 p, Vec2 slope, Vec2 seed) {
    const float slope_length = length(slope);
    if (slope_length <= 1.0e-5F) {
        return {.value = 1.0F};
    }
    const Vec2 downhill = slope * (-1.0F / slope_length);
    const float shaped_slope = std::pow(slope_length, 0.68F);
    const Vec2 side_direction{.x = -downhill.y * shaped_slope * kTau,
                              .y = downhill.x * shaped_slope * kTau};
    const Vec2 integer{.x = std::floor(p.x), .y = std::floor(p.y)};
    const Vec2 fractional = p - integer;
    float value_sum = 0.0F;
    Vec2 gradient_sum{};
    float weight_sum = 0.0F;
    Vec2 weight_gradient_sum{};

    for (int y = -1; y <= 2; ++y) {
        for (int x = -1; x <= 2; ++x) {
            const Vec2 cell{.x = integer.x + static_cast<float>(x),
                            .y = integer.y + static_cast<float>(y)};
            const Vec2 jitter{
                .x = (hash01(cell, seed, 17.0F) - 0.5F) * 0.82F,
                .y = (hash01(cell, seed, 43.0F) - 0.5F) * 0.82F,
            };
            const Vec2 relative =
                fractional - Vec2{.x = static_cast<float>(x), .y = static_cast<float>(y)} - jitter;
            const float distance_squared = dot(relative, relative);
            const float exponential = std::exp(-2.0F * distance_squared);
            const float weight = std::max(0.0F, exponential - 0.0111F);
            if (weight <= 0.0F) {
                continue;
            }
            const Vec2 weight_gradient = relative * (-4.0F * exponential);
            const float phase =
                dot(relative, side_direction) + (hash01(cell, seed, 79.0F) - 0.5F) * 0.70F;
            const float value = std::cos(phase);
            const Vec2 value_gradient = side_direction * -std::sin(phase);
            value_sum += value * weight;
            gradient_sum += value_gradient * weight + weight_gradient * value;
            weight_sum += weight;
            weight_gradient_sum += weight_gradient;
        }
    }

    if (weight_sum <= 1.0e-6F) {
        return {};
    }
    const float value = value_sum / weight_sum;
    const Vec2 gradient =
        (gradient_sum * weight_sum - weight_gradient_sum * value_sum) / (weight_sum * weight_sum);
    return {.value = value, .gradient = gradient};
}

[[nodiscard]] HeightSlope filtered_source(Vec2 world, Vec2 seed, HeightSlope base,
                                          float activity) {
    HeightSlope filtered = base;
    float scale_m = kErosionScaleM;
    float strength_m = kErosionStrengthM;
    const float bounded_activity = std::clamp(activity, 0.0F, 1.0F);
    for (int octave = 0; octave < kErosionOctaves; ++octave) {
        const float slope_gate = smoothstep(0.025F, 0.32F, length(filtered.gradient));
        const float gate = slope_gate * bounded_activity;
        const Vec2 octave_seed = seed + Vec2{.x = 101.0F + static_cast<float>(octave) * 37.0F,
                                             .y = -131.0F - static_cast<float>(octave) * 29.0F};
        const DirectionalSample gully =
            directional_cells(world / scale_m, filtered.gradient, octave_seed);
        const float height_delta = (gully.value - 0.28F) * strength_m * gate;
        filtered.height += height_delta;
        filtered.gradient += gully.gradient * ((strength_m / scale_m) * gate);
        strength_m *= kErosionGain;
        scale_m /= kErosionLacunarity;
    }
    return filtered;
}

} // namespace

ShadertoyErosionReferenceSample shadertoy_erosion_filter_sample(
    float world_x, float world_z, std::uint64_t seed, ShadertoyErosionSourceSample source,
    float activity) {
    const Vec2 world{.x = world_x, .y = world_z};
    const Vec2 seed_components = seed_value(seed);
    const HeightSlope base{
        .height = source.height_m,
        .gradient = {.x = source.gradient_x, .y = source.gradient_z},
    };
    const HeightSlope filtered = filtered_source(world, seed_components, base, activity);
    return {
        .base_height_m = base.height,
        .filtered_height_m = filtered.height,
        .erosion_delta_m = base.height - filtered.height,
        .base_gradient_x = base.gradient.x,
        .base_gradient_z = base.gradient.y,
        .gradient_x = filtered.gradient.x,
        .gradient_z = filtered.gradient.y,
    };
}

ShadertoyErosionReferenceSample shadertoy_erosion_reference_sample(float world_x, float world_z,
                                                                   std::uint64_t seed) {
    const Vec2 world{.x = world_x, .y = world_z};
    const Vec2 seed_components = seed_value(seed);
    const HeightSlope base = base_mountain(world, seed_components);
    const float mountain_activity = smoothstep(420.0F, 1180.0F, base.height);
    return shadertoy_erosion_filter_sample(
        world_x, world_z, seed,
        {.height_m = base.height,
         .gradient_x = base.gradient.x,
         .gradient_z = base.gradient.y},
        mountain_activity);
}

float shadertoy_erosion_reference_height(float world_x, float world_z, std::uint64_t seed,
                                         ShadertoyErosionReferenceSurface surface) {
    const ShadertoyErosionReferenceSample sample =
        shadertoy_erosion_reference_sample(world_x, world_z, seed);
    return surface == ShadertoyErosionReferenceSurface::Base ? sample.base_height_m
                                                             : sample.filtered_height_m;
}

float shadertoy_erosion_reference_normal_cos_v(float world_x, float world_z, std::uint64_t seed) {
    const ShadertoyErosionReferenceSample sample =
        shadertoy_erosion_reference_sample(world_x, world_z, seed);
    return 1.0F / std::sqrt(1.0F + (sample.gradient_x * sample.gradient_x) +
                            (sample.gradient_z * sample.gradient_z));
}

} // namespace cubey::projects::terrain_ref
