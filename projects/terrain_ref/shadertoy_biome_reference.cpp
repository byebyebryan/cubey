#include "shadertoy_biome_reference.h"

#include "terrain_engine_reference.h"

#include <algorithm>
#include <cmath>

namespace cubey::projects::terrain_ref {
namespace {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] float fract_positive(float value) {
    return value - std::floor(value);
}

[[nodiscard]] float mix(float a, float b, float t) {
    return a + ((b - a) * t);
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] Vec2 rotate(Vec2 value, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {.x = (value.x * c) - (value.y * s), .y = (value.x * s) + (value.y * c)};
}

[[nodiscard]] float seeded_hash(Vec2 p, Vec2 seed) {
    const float dot_value =
        ((p.x + seed.x) * 127.113F) + ((p.y + seed.y) * 311.731F) + 41.17F;
    return fract_positive(std::sin(dot_value) * 43758.5453F);
}

[[nodiscard]] float value_noise(Vec2 p, Vec2 seed) {
    const Vec2 i{.x = std::floor(p.x), .y = std::floor(p.y)};
    const Vec2 f{.x = p.x - i.x, .y = p.y - i.y};
    const float wx = f.x * f.x * f.x * (f.x * ((f.x * 6.0F) - 15.0F) + 10.0F);
    const float wy = f.y * f.y * f.y * (f.y * ((f.y * 6.0F) - 15.0F) + 10.0F);
    const float a = seeded_hash(i, seed);
    const float b = seeded_hash({.x = i.x + 1.0F, .y = i.y}, seed);
    const float c = seeded_hash({.x = i.x, .y = i.y + 1.0F}, seed);
    const float d = seeded_hash({.x = i.x + 1.0F, .y = i.y + 1.0F}, seed);
    return mix(mix(a, b, wx), mix(c, d, wx), wy);
}

[[nodiscard]] float fbm(Vec2 p, Vec2 seed, int octaves, float gain = 0.52F) {
    float value = 0.0F;
    float amplitude = 0.55F;
    float total_amplitude = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        value += value_noise(p, {.x = seed.x + float(octave) * 8.13F,
                                 .y = seed.y - float(octave) * 5.71F}) *
                 amplitude;
        total_amplitude += amplitude;
        p = rotate({.x = (p.x * 2.03F) + 17.0F, .y = (p.y * 2.03F) - 11.0F}, 0.72F);
        amplitude *= gain;
    }
    return total_amplitude > 0.0F ? value / total_amplitude : 0.0F;
}

[[nodiscard]] float ridged_fbm(Vec2 p, Vec2 seed, int octaves) {
    float value = 0.0F;
    float amplitude = 0.58F;
    float total_amplitude = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        const float n = value_noise(p, {.x = seed.x + float(octave) * 3.91F,
                                        .y = seed.y + float(octave) * 6.43F});
        const float ridge = 1.0F - std::abs((n * 2.0F) - 1.0F);
        value += std::pow(std::max(ridge, 0.0F), 1.65F) * amplitude;
        total_amplitude += amplitude;
        p = rotate({.x = (p.x * 2.08F) + 9.0F, .y = (p.y * 2.08F) + 13.0F}, -0.63F);
        amplitude *= 0.50F;
    }
    return total_amplitude > 0.0F ? value / total_amplitude : 0.0F;
}

[[nodiscard]] Vec2 seed_components(std::uint64_t seed) {
    const TerrainEngineReferenceSeedComponents components =
        terrain_engine_reference_seed_components(seed);
    return {.x = components.x, .y = components.y};
}

[[nodiscard]] float triangle_wave(float value) {
    const float f = fract_positive(value);
    return 1.0F - std::abs((f * 2.0F) - 1.0F);
}

} // namespace

float shadertoy_alpine_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00018F) + (seed_value.x * 0.119F) - 3.0F,
        .y = (world_z * 0.00018F) + (seed_value.y * 0.143F) + 5.0F,
    };
    const float macro = fbm({.x = p.x * 0.48F, .y = p.y * 0.48F},
                            {.x = seed_value.x + 4.0F, .y = seed_value.y - 7.0F}, 5);
    const float mass = smoothstep(0.18F, 0.82F, macro);
    const float shoulder = smoothstep(0.06F, 0.58F, macro);
    const float warp = (fbm({.x = p.x * 1.10F, .y = p.y * 1.10F},
                            {.x = seed_value.x + 17.0F, .y = seed_value.y - 23.0F}, 4) -
                        0.5F) *
                       0.72F;
    Vec2 ridge_p = rotate({.x = (p.x * 1.24F) + warp, .y = (p.y * 0.82F) - warp}, 0.38F);
    const float ridges = ridged_fbm(ridge_p, {.x = seed_value.x - 11.0F, .y = seed_value.y + 19.0F}, 6);
    const float crest = std::pow(std::max(ridges, 0.0F), 1.32F) * mass;
    const float valley_source =
        fbm({.x = p.x * 0.92F, .y = p.y * 0.92F},
            {.x = seed_value.x + 29.0F, .y = seed_value.y + 31.0F}, 4);
    const float valley = std::pow(std::max(1.0F - valley_source, 0.0F), 2.20F) * shoulder;
    const float local_detail =
        (ridged_fbm({.x = p.x * 4.2F, .y = p.y * 4.2F},
                    {.x = seed_value.x + 37.0F, .y = seed_value.y - 41.0F}, 4) -
         0.42F) *
        mass;
    const float broad_height = 120.0F + (std::pow(mass, 1.35F) * 1900.0F) +
                               (std::pow(shoulder, 2.0F) * 520.0F);
    const float crest_height = crest * 2300.0F;
    const float valley_cut = valley * (360.0F + 520.0F * mass);
    return std::max(broad_height + crest_height - valley_cut + (local_detail * 170.0F), 0.0F);
}

float shadertoy_dunes_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00026F) + (seed_value.x * 0.071F),
        .y = (world_z * 0.00026F) + (seed_value.y * 0.083F),
    };
    const float wind_angle = 0.42F + (seed_value.x * 0.011F);
    const Vec2 wind{.x = std::cos(wind_angle), .y = std::sin(wind_angle)};
    const Vec2 cross{.x = -wind.y, .y = wind.x};
    const float along = (p.x * wind.x) + (p.y * wind.y);
    const float across = (p.x * cross.x) + (p.y * cross.y);
    const float broad =
        fbm({.x = p.x * 0.30F, .y = p.y * 0.30F},
            {.x = seed_value.x - 13.0F, .y = seed_value.y + 17.0F}, 4);
    const float warp =
        (fbm({.x = p.x * 1.25F, .y = p.y * 1.25F},
             {.x = seed_value.x + 23.0F, .y = seed_value.y - 29.0F}, 4) -
         0.5F) *
        0.68F;
    const float primary = std::pow(std::max(triangle_wave((across * 1.42F) + warp +
                                                          std::sin(along * 0.82F) * 0.16F),
                                            0.0F),
                                   1.52F);
    const float secondary = std::pow(
        std::max(triangle_wave((across * 2.10F) + (along * 0.10F) - warp * 0.55F), 0.0F),
        2.10F);
    const float dune_envelope = smoothstep(0.18F, 0.72F, broad);
    const float rolling = primary * (0.58F + dune_envelope * 0.42F);
    const float ripple =
        (triangle_wave((across * 9.0F) + std::sin(along * 2.1F) * 0.35F) - 0.5F) * 7.5F;
    return 24.0F + (broad * 72.0F) + (rolling * 360.0F) + (secondary * 82.0F) + ripple;
}

float shadertoy_lake_basin_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00020F) + (seed_value.x * 0.093F),
        .y = (world_z * 0.00020F) + (seed_value.y * 0.107F),
    };
    const float macro =
        fbm({.x = p.x * 0.54F, .y = p.y * 0.54F},
            {.x = seed_value.x + 41.0F, .y = seed_value.y - 43.0F}, 5);
    const float hills = std::pow(std::max(macro, 0.0F), 1.45F) * 760.0F;
    const float warp_x =
        (fbm({.x = p.x * 0.92F, .y = p.y * 0.92F},
             {.x = seed_value.x - 47.0F, .y = seed_value.y + 53.0F}, 4) -
         0.5F) *
        1.25F;
    const float warp_y =
        (fbm({.x = p.x * 0.88F + 9.0F, .y = p.y * 0.88F - 4.0F},
             {.x = seed_value.x + 59.0F, .y = seed_value.y - 61.0F}, 4) -
         0.5F) *
        0.92F;
    const Vec2 q = rotate({.x = (p.x + warp_x) * 0.62F, .y = (p.y + warp_y) * 1.18F}, -0.34F);
    const float basin_distance = std::sqrt((q.x * q.x) + (q.y * q.y));
    const float basin = smoothstep(1.32F, 0.22F, basin_distance);
    const float lowland =
        smoothstep(0.66F, 0.20F,
                   fbm({.x = p.x * 0.76F, .y = p.y * 0.76F},
                       {.x = seed_value.x + 67.0F, .y = seed_value.y + 71.0F}, 4));
    const float shoreline_shelf =
        smoothstep(0.32F, 0.72F, basin) * (1.0F - smoothstep(0.72F, 1.0F, basin));
    const float ridge_detail =
        (ridged_fbm({.x = p.x * 2.1F, .y = p.y * 2.1F},
                    {.x = seed_value.x - 73.0F, .y = seed_value.y + 79.0F}, 4) -
         0.40F) *
        (1.0F - basin * 0.75F);
    const float basin_cut = (basin * 520.0F) + (lowland * basin * 180.0F);
    return std::max(75.0F + hills + (ridge_detail * 120.0F) - basin_cut +
                        (shoreline_shelf * 44.0F),
                    -25.0F);
}

} // namespace cubey::projects::terrain_ref
