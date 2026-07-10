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

[[nodiscard]] float dune_profile(float value, float crest_position, float slip_width) {
    const float phase = fract_positive(value);
    const float crest = std::clamp(crest_position, 0.32F, 0.74F);
    const float lee_end = std::min(crest + std::clamp(slip_width, 0.06F, 0.22F), 0.98F);
    const float windward = smoothstep(0.02F, crest, phase);
    const float lee = 1.0F - smoothstep(crest, lee_end, phase);
    return std::pow(std::max(std::min(windward, lee), 0.0F), 0.92F);
}

struct GorgeSourceField {
    Vec2 p{};
    Vec2 q{};
    float plateau_source = 0.0F;
    float plateau = 0.0F;
    float signed_corridor = 0.0F;
    float corridor_distance = 0.0F;
    float corridor_width = 0.0F;
    float main_corridor = 0.0F;
    float floor = 0.0F;
    float wall = 0.0F;
    float tributaries = 0.0F;
};

[[nodiscard]] GorgeSourceField gorge_source_field(Vec2 p, Vec2 seed_value) {
    const float plateau_source =
        fbm({.x = p.x * 0.24F, .y = p.y * 0.24F},
            {.x = seed_value.x + 293.0F, .y = seed_value.y - 307.0F}, 5);
    const float plateau = smoothstep(0.16F, 0.72F, plateau_source);
    const float warp_x =
        (fbm({.x = p.x * 0.54F, .y = p.y * 0.54F},
             {.x = seed_value.x - 311.0F, .y = seed_value.y + 313.0F}, 4) -
         0.5F) *
        1.10F;
    const float warp_y =
        (fbm({.x = (p.x * 0.46F) + 5.0F, .y = (p.y * 0.46F) - 3.0F},
             {.x = seed_value.x + 317.0F, .y = seed_value.y - 331.0F}, 4) -
         0.5F) *
        0.88F;
    const Vec2 q =
        rotate({.x = (p.x * 0.74F) + warp_x, .y = (p.y * 1.02F) + warp_y}, 0.42F);
    const float broad_wander =
        (fbm({.x = q.y * 0.34F, .y = (q.y * 0.16F) + 6.0F},
             {.x = seed_value.x - 337.0F, .y = seed_value.y + 347.0F}, 4) -
         0.5F) *
        1.05F;
    const float local_wander =
        (fbm({.x = q.y * 0.86F, .y = (q.y * 0.38F) - 4.0F},
             {.x = seed_value.x + 349.0F, .y = seed_value.y - 353.0F}, 3) -
         0.5F) *
        0.28F;
    const float signed_corridor = q.x + broad_wander + local_wander;
    const float corridor_distance = std::abs(signed_corridor);
    const float width_noise =
        fbm({.x = q.y * 0.28F, .y = q.x * 0.12F},
            {.x = seed_value.x - 359.0F, .y = seed_value.y + 367.0F}, 4);
    const float corridor_width = mix(0.22F, 0.44F, width_noise);
    const float main_corridor =
        1.0F - smoothstep(corridor_width * 0.52F, corridor_width * 1.74F, corridor_distance);
    const float floor =
        1.0F - smoothstep(corridor_width * 0.16F, corridor_width * 0.58F, corridor_distance);
    const float wall = smoothstep(corridor_width * 0.56F, corridor_width * 1.20F,
                                  corridor_distance) *
                       (1.0F - smoothstep(corridor_width * 1.22F, corridor_width * 2.08F,
                                           corridor_distance));
    const float branch_a =
        ridged_fbm({.x = (q.x * 0.82F) + q.y * 0.44F + warp_x * 0.18F,
                    .y = (q.y * 0.64F) - q.x * 0.48F + warp_y * 0.12F},
                   {.x = seed_value.x + 373.0F, .y = seed_value.y - 379.0F}, 5);
    const float branch_b =
        ridged_fbm({.x = (q.x * 0.78F) - q.y * 0.42F - warp_x * 0.20F,
                    .y = (q.y * 0.70F) + q.x * 0.44F - warp_y * 0.10F},
                   {.x = seed_value.x - 383.0F, .y = seed_value.y + 389.0F}, 5);
    const float branch_gate =
        smoothstep(corridor_width * 0.90F, corridor_width * 2.20F, corridor_distance) *
        (1.0F - smoothstep(corridor_width * 4.20F, corridor_width * 6.10F,
                            corridor_distance));
    const float tributaries = std::pow(std::max(std::max(branch_a, branch_b), 0.0F), 2.35F) *
                              branch_gate * smoothstep(0.18F, 0.88F, plateau) *
                              (1.0F - floor * 0.72F);
    return {.p = p,
            .q = q,
            .plateau_source = plateau_source,
            .plateau = plateau,
            .signed_corridor = signed_corridor,
            .corridor_distance = corridor_distance,
            .corridor_width = corridor_width,
            .main_corridor = main_corridor,
            .floor = floor,
            .wall = wall,
            .tributaries = tributaries};
}

struct GlacialSourceField {
    Vec2 p{};
    Vec2 q{};
    float macro = 0.0F;
    float uplift = 0.0F;
    float shoulder = 0.0F;
    float trunk_distance = 0.0F;
    float trunk_width = 0.0F;
    float trunk_valley = 0.0F;
    float trunk_floor = 0.0F;
    float trunk_wall = 0.0F;
    float side_valleys = 0.0F;
    float cirques = 0.0F;
    float rock_ribs = 0.0F;
    float ice_field = 0.0F;
    float talus = 0.0F;
};

[[nodiscard]] GlacialSourceField glacial_source_field(Vec2 p, Vec2 seed_value) {
    const float macro =
        fbm({.x = p.x * 0.34F, .y = p.y * 0.34F},
            {.x = seed_value.x + 373.0F, .y = seed_value.y - 379.0F}, 5);
    const float uplift = smoothstep(0.18F, 0.80F, macro);
    const float shoulder = smoothstep(0.03F, 0.60F, macro);
    const float warp_x =
        (fbm({.x = p.x * 0.58F, .y = p.y * 0.58F},
             {.x = seed_value.x - 383.0F, .y = seed_value.y + 389.0F}, 4) -
         0.5F) *
        1.02F;
    const float warp_y =
        (fbm({.x = (p.x * 0.54F) + 3.0F, .y = (p.y * 0.54F) - 5.0F},
             {.x = seed_value.x + 397.0F, .y = seed_value.y - 401.0F}, 4) -
         0.5F) *
        0.80F;
    const Vec2 q =
        rotate({.x = (p.x * 0.86F) + warp_x, .y = (p.y * 1.10F) + warp_y}, -0.42F);
    const float broad_wander =
        (fbm({.x = q.y * 0.30F, .y = (q.y * 0.15F) + 8.0F},
             {.x = seed_value.x - 409.0F, .y = seed_value.y + 419.0F}, 4) -
         0.5F) *
        0.95F;
    const float local_wander =
        (fbm({.x = q.y * 0.72F, .y = (q.y * 0.34F) - 4.0F},
             {.x = seed_value.x + 421.0F, .y = seed_value.y - 431.0F}, 3) -
         0.5F) *
        0.22F;
    const float trunk_distance = std::abs(q.x + broad_wander + local_wander);
    const float width_noise =
        fbm({.x = q.y * 0.24F, .y = q.x * 0.10F},
            {.x = seed_value.x - 433.0F, .y = seed_value.y + 439.0F}, 4);
    const float trunk_width = mix(0.42F, 0.86F, width_noise);
    const float trunk_valley =
        1.0F - smoothstep(trunk_width * 0.45F, trunk_width * 1.55F, trunk_distance);
    const float trunk_floor =
        1.0F - smoothstep(trunk_width * 0.16F, trunk_width * 0.55F, trunk_distance);
    const float trunk_wall =
        smoothstep(trunk_width * 0.50F, trunk_width * 1.05F, trunk_distance) *
        (1.0F - smoothstep(trunk_width * 1.05F, trunk_width * 1.85F, trunk_distance));
    const float branch_a =
        ridged_fbm({.x = (q.x * 0.62F) + (q.y * 0.42F) + warp_x * 0.16F,
                    .y = (q.y * 0.54F) - (q.x * 0.36F) + warp_y * 0.12F},
                   {.x = seed_value.x + 443.0F, .y = seed_value.y - 449.0F}, 5);
    const float branch_b =
        ridged_fbm({.x = (q.x * 0.60F) - (q.y * 0.38F) - warp_x * 0.18F,
                    .y = (q.y * 0.58F) + (q.x * 0.34F) - warp_y * 0.12F},
                   {.x = seed_value.x - 451.0F, .y = seed_value.y + 457.0F}, 5);
    const float side_gate =
        smoothstep(trunk_width * 0.72F, trunk_width * 2.35F, trunk_distance) *
        (1.0F - smoothstep(trunk_width * 4.40F, trunk_width * 6.40F, trunk_distance));
    const float side_valleys =
        smoothstep(0.34F, 0.78F, std::max(branch_a, branch_b)) * side_gate *
        smoothstep(0.14F, 0.88F, shoulder) * (1.0F - trunk_floor * 0.70F);
    const float cirque_source =
        fbm({.x = (q.x * 0.66F) + branch_a * 0.22F, .y = (q.y * 0.66F) - branch_b * 0.18F},
            {.x = seed_value.x + 463.0F, .y = seed_value.y - 467.0F}, 5);
    const float cirques =
        std::pow(smoothstep(0.56F, 0.88F, cirque_source), 1.35F) *
        smoothstep(0.36F, 0.96F, uplift) * (1.0F - trunk_floor * 0.46F);
    const float rib_source =
        ridged_fbm({.x = (q.x * 1.06F) + warp_x * 0.20F, .y = (q.y * 0.78F) - warp_y * 0.16F},
                   {.x = seed_value.x - 479.0F, .y = seed_value.y + 487.0F}, 6);
    const float rock_ribs =
        smoothstep(0.32F, 0.82F, rib_source) * smoothstep(0.10F, 0.92F, uplift) *
        (0.38F + trunk_wall * 0.68F + side_valleys * 0.28F);
    const float ice_source =
        fbm({.x = p.x * 0.54F, .y = p.y * 0.54F},
            {.x = seed_value.x + 491.0F, .y = seed_value.y - 499.0F}, 5);
    const float ice_field =
        std::clamp(smoothstep(0.30F, 0.76F, ice_source) * smoothstep(0.12F, 0.92F, uplift) +
                       trunk_floor * 0.58F + side_valleys * 0.22F,
                   0.0F, 1.0F);
    const float talus = std::clamp((trunk_wall * 0.72F) + (side_valleys * 0.36F) +
                                       (1.0F - smoothstep(0.66F, 0.96F, uplift)) * 0.18F,
                                   0.0F, 1.0F);
    return {.p = p,
            .q = q,
            .macro = macro,
            .uplift = uplift,
            .shoulder = shoulder,
            .trunk_distance = trunk_distance,
            .trunk_width = trunk_width,
            .trunk_valley = trunk_valley,
            .trunk_floor = trunk_floor,
            .trunk_wall = trunk_wall,
            .side_valleys = side_valleys,
            .cirques = cirques,
            .rock_ribs = rock_ribs,
            .ice_field = ice_field,
            .talus = talus};
}

struct CraterAccumulation {
    float depression = 0.0F;
    float rim = 0.0F;
    float ejecta = 0.0F;
    float floor_darkening = 0.0F;
};

void accumulate_crater_population(CraterAccumulation& accumulation,
                                  Vec2 p,
                                  Vec2 seed_value,
                                  float scale,
                                  float density,
                                  float min_radius,
                                  float radius_range,
                                  float depth_scale,
                                  float rim_scale,
                                  float ejecta_scale,
                                  float domain_angle,
                                  float warp_strength,
                                  Vec2 seed_offset) {
    const Vec2 layer_seed{.x = seed_value.x + seed_offset.x, .y = seed_value.y + seed_offset.y};
    const float warp_x =
        (fbm({.x = (p.x * 0.43F) + 7.0F, .y = (p.y * 0.43F) - 3.0F},
             {.x = layer_seed.x + 5.0F, .y = layer_seed.y - 9.0F}, 4) -
         0.5F) *
        warp_strength;
    const float warp_y =
        (fbm({.x = (p.x * 0.39F) - 4.0F, .y = (p.y * 0.39F) + 11.0F},
             {.x = layer_seed.x - 13.0F, .y = layer_seed.y + 15.0F}, 4) -
         0.5F) *
        warp_strength;
    const Vec2 crater_domain = rotate({.x = p.x + warp_x, .y = p.y + warp_y}, domain_angle);
    const Vec2 crater_p{.x = crater_domain.x * scale, .y = crater_domain.y * scale};
    const Vec2 base_cell{.x = std::floor(crater_p.x), .y = std::floor(crater_p.y)};
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Vec2 cell{.x = base_cell.x + float(dx), .y = base_cell.y + float(dz)};
            const float local_density =
                fbm({.x = cell.x * 0.17F, .y = cell.y * 0.17F},
                    {.x = layer_seed.x + 11.0F, .y = layer_seed.y - 17.0F}, 3);
            const float cell_density =
                std::clamp(density * mix(0.28F, 1.58F, local_density), 0.0F, 0.92F);
            const float h_presence =
                seeded_hash(cell, {.x = layer_seed.x + 19.0F, .y = layer_seed.y - 23.0F});
            if (h_presence > cell_density) {
                continue;
            }

            const float h0 =
                seeded_hash(cell, {.x = layer_seed.x + 29.0F, .y = layer_seed.y + 31.0F});
            const float h1 =
                seeded_hash(cell, {.x = layer_seed.x - 37.0F, .y = layer_seed.y + 41.0F});
            const float h2 =
                seeded_hash(cell, {.x = layer_seed.x + 43.0F, .y = layer_seed.y - 47.0F});
            const float h3 =
                seeded_hash(cell, {.x = layer_seed.x - 53.0F, .y = layer_seed.y + 59.0F});
            const float h4 =
                seeded_hash(cell, {.x = layer_seed.x + 61.0F, .y = layer_seed.y - 67.0F});
            const Vec2 center{.x = cell.x + 0.08F + h0 * 0.84F,
                              .y = cell.y + 0.08F + h1 * 0.84F};
            const float radius = min_radius + h2 * radius_range;
            const float dx_to_center = crater_p.x - center.x;
            const float dz_to_center = crater_p.y - center.y;
            const float d =
                std::sqrt(dx_to_center * dx_to_center + dz_to_center * dz_to_center);
            const float normalized_d = d / std::max(radius, 0.001F);
            const float asymmetry =
                0.88F + 0.18F * (h3 - 0.5F) +
                (value_noise({.x = crater_p.x * 2.2F, .y = crater_p.y * 2.2F},
                             {.x = layer_seed.x + 71.0F, .y = layer_seed.y + 73.0F}) -
                 0.5F) *
                    0.16F;
            const float shaped_d = normalized_d * asymmetry;
            const float freshness = smoothstep(0.18F, 0.96F, h4);
            const float bowl =
                std::pow(std::max(1.0F - smoothstep(0.08F, 1.02F, shaped_d), 0.0F),
                         1.10F);
            const float rim_band = smoothstep(0.74F, 1.02F, shaped_d) *
                                   (1.0F - smoothstep(1.02F, 1.48F, shaped_d));
            const float ejecta_band = 1.0F - smoothstep(1.02F, 2.35F, shaped_d);
            const float age_depth = mix(0.48F, 1.0F, freshness);
            accumulation.depression =
                std::max(accumulation.depression, bowl * depth_scale * age_depth);
            accumulation.rim += rim_band * rim_scale * mix(0.22F, 1.0F, freshness);
            accumulation.ejecta += ejecta_band * ejecta_scale * mix(0.20F, 0.74F, freshness);
            accumulation.floor_darkening =
                std::max(accumulation.floor_darkening, bowl * mix(0.20F, 0.90F, freshness));
        }
    }
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
        .x = (world_x * 0.00020F) + (seed_value.x * 0.071F),
        .y = (world_z * 0.00020F) + (seed_value.y * 0.083F),
    };
    const float wind_angle = 0.48F + (seed_value.x * 0.013F);
    const Vec2 wind{.x = std::cos(wind_angle), .y = std::sin(wind_angle)};
    const Vec2 cross{.x = -wind.y, .y = wind.x};
    const float along = (p.x * wind.x) + (p.y * wind.y);
    const float across = (p.x * cross.x) + (p.y * cross.y);
    const float broad =
        fbm({.x = p.x * 0.20F, .y = p.y * 0.20F},
            {.x = seed_value.x - 13.0F, .y = seed_value.y + 17.0F}, 5);
    const float dune_envelope = smoothstep(0.18F, 0.70F, broad);
    const float bend =
        (fbm({.x = p.x * 0.44F, .y = p.y * 0.44F},
             {.x = seed_value.x + 23.0F, .y = seed_value.y - 29.0F}, 4) -
         0.5F) *
        1.55F;
    const float phase_noise =
        fbm({.x = along * 0.50F, .y = across * 0.34F},
            {.x = seed_value.x + 31.0F, .y = seed_value.y - 37.0F}, 4) -
        0.5F;
    const float crest_noise =
        fbm({.x = along * 0.34F, .y = across * 0.46F},
            {.x = seed_value.x - 41.0F, .y = seed_value.y + 43.0F}, 4) -
        0.5F;
    const float lobe_source =
        fbm({.x = along * 0.30F, .y = across * 0.46F},
            {.x = seed_value.x + 47.0F, .y = seed_value.y + 53.0F}, 4);
    const float patch_breakup = smoothstep(0.18F, 0.72F, lobe_source);
    const float lobe_envelope =
        smoothstep(0.24F, 0.78F,
                   fbm({.x = along * 0.20F, .y = across * 0.30F},
                       {.x = seed_value.x - 83.0F, .y = seed_value.y + 89.0F}, 4));
    const float primary_phase =
        (along * 0.64F) + bend + std::sin((across * 0.68F) + phase_noise * 1.6F) * 0.22F;
    const float primary =
        dune_profile(primary_phase, 0.60F + crest_noise * 0.14F, 0.12F + patch_breakup * 0.060F);
    const float secondary_phase = (along * 0.36F) - (across * 0.12F) - (bend * 0.32F) +
                                  (phase_noise * 0.55F);
    const float secondary = dune_profile(secondary_phase, 0.66F - crest_noise * 0.10F, 0.16F);
    const float low_swell =
        fbm({.x = p.x * 0.16F, .y = p.y * 0.16F},
            {.x = seed_value.x + 59.0F, .y = seed_value.y - 61.0F}, 4) *
        84.0F;
    const float ripple =
        (triangle_wave((across * 8.0F) + (along * 0.42F) + phase_noise * 1.3F) - 0.5F) *
        (1.0F + patch_breakup * 2.2F);
    const float macro_dunes = primary * dune_envelope * (0.58F + lobe_envelope * 0.62F);
    const float secondary_dunes = secondary * dune_envelope * patch_breakup;
    return 18.0F + (broad * 86.0F) + low_swell +
           (macro_dunes * (290.0F + patch_breakup * 210.0F)) +
           (secondary_dunes * (70.0F + (1.0F - broad) * 120.0F)) + ripple;
}

float shadertoy_lake_basin_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00020F) + (seed_value.x * 0.093F),
        .y = (world_z * 0.00020F) + (seed_value.y * 0.107F),
    };
    const float macro =
        fbm({.x = p.x * 0.48F, .y = p.y * 0.48F},
            {.x = seed_value.x + 41.0F, .y = seed_value.y - 43.0F}, 5);
    const float hills = std::pow(std::max(macro, 0.0F), 1.30F) * 980.0F;
    const float warp_x =
        (fbm({.x = p.x * 0.92F, .y = p.y * 0.92F},
             {.x = seed_value.x - 47.0F, .y = seed_value.y + 53.0F}, 4) -
         0.5F) *
        1.35F;
    const float warp_y =
        (fbm({.x = p.x * 0.88F + 9.0F, .y = p.y * 0.88F - 4.0F},
             {.x = seed_value.x + 59.0F, .y = seed_value.y - 61.0F}, 4) -
         0.5F) *
        1.05F;
    const Vec2 q = rotate({.x = (p.x + warp_x) * 0.62F, .y = (p.y + warp_y) * 1.18F}, -0.34F);
    const float basin_distance = std::sqrt((q.x * q.x) + (q.y * q.y));
    const float basin = smoothstep(1.46F, 0.20F, basin_distance);
    const float lowland =
        smoothstep(0.70F, 0.18F,
                   fbm({.x = p.x * 0.70F, .y = p.y * 0.70F},
                       {.x = seed_value.x + 67.0F, .y = seed_value.y + 71.0F}, 4));
    const float basin_rim =
        smoothstep(1.58F, 1.02F, basin_distance) * smoothstep(0.60F, 1.02F, basin_distance);
    const float shoreline_shelf =
        smoothstep(0.50F, 0.82F, basin) * (1.0F - smoothstep(0.82F, 1.0F, basin));
    const float ridge_detail =
        (ridged_fbm({.x = p.x * 1.85F, .y = p.y * 1.85F},
                    {.x = seed_value.x - 73.0F, .y = seed_value.y + 79.0F}, 4) -
         0.42F) *
        (1.0F - basin * 0.68F);
    const float basin_cut = (basin * 760.0F) + (lowland * basin * 260.0F);
    return std::max(95.0F + hills + (basin_rim * 320.0F) + (ridge_detail * 165.0F) -
                        basin_cut + (shoreline_shelf * 88.0F),
                    -55.0F);
}

float shadertoy_badlands_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00022F) + (seed_value.x * 0.127F) - 4.0F,
        .y = (world_z * 0.00022F) + (seed_value.y * 0.101F) + 2.0F,
    };
    const float macro =
        fbm({.x = p.x * 0.28F, .y = p.y * 0.28F},
            {.x = seed_value.x + 101.0F, .y = seed_value.y - 103.0F}, 5);
    const float plateau = smoothstep(0.28F, 0.72F, macro);
    const float shoulder = smoothstep(0.06F, 0.62F, macro);
    const float warp =
        (fbm({.x = p.x * 0.72F, .y = p.y * 0.72F},
             {.x = seed_value.x - 107.0F, .y = seed_value.y + 109.0F}, 4) -
         0.5F) *
        1.25F;
    const Vec2 wash_p = rotate({.x = (p.x * 0.72F) + warp, .y = (p.y * 1.28F) - warp}, 0.58F);
    const float wash_source =
        ridged_fbm(wash_p, {.x = seed_value.x + 113.0F, .y = seed_value.y - 127.0F}, 6);
    const float dry_washes = std::pow(std::max(wash_source, 0.0F), 1.55F) * shoulder;
    const float tributary_source =
        ridged_fbm({.x = (p.x * 1.42F) - warp * 0.35F, .y = (p.y * 1.05F) + warp * 0.45F},
                   {.x = seed_value.x - 131.0F, .y = seed_value.y + 137.0F}, 5);
    const float tributaries =
        std::pow(std::max(tributary_source, 0.0F), 2.05F) * smoothstep(0.18F, 0.88F, plateau);
    const float mesa_breakup =
        fbm({.x = p.x * 0.92F, .y = p.y * 0.92F},
            {.x = seed_value.x + 139.0F, .y = seed_value.y + 149.0F}, 4);
    const float plateau_height = 90.0F + (std::pow(plateau, 0.88F) * 1260.0F) +
                                 (std::pow(shoulder, 2.15F) * 360.0F);
    const float cut_depth =
        (dry_washes * (820.0F + plateau * 420.0F)) + (tributaries * 320.0F);
    const float cliff_lift = smoothstep(0.50F, 0.86F, wash_source) *
                             smoothstep(0.20F, 0.82F, plateau) * 150.0F;
    const float strata =
        (triangle_wave((plateau_height - cut_depth) * 0.012F + mesa_breakup * 1.15F) - 0.5F) *
        (24.0F + plateau * 34.0F) * smoothstep(0.16F, 0.82F, dry_washes + tributaries);
    const float rough_detail =
        (ridged_fbm({.x = p.x * 3.2F, .y = p.y * 3.2F},
                    {.x = seed_value.x - 151.0F, .y = seed_value.y + 157.0F}, 4) -
         0.42F) *
        (24.0F + shoulder * 58.0F);
    return std::max(plateau_height - cut_depth + cliff_lift + strata + rough_detail, 0.0F);
}

float shadertoy_coast_island_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = world_x * 0.00018F,
        .y = world_z * 0.00018F,
    };
    const float warp_x =
        (fbm({.x = p.x * 0.58F, .y = p.y * 0.58F},
             {.x = seed_value.x + 163.0F, .y = seed_value.y - 167.0F}, 4) -
         0.5F) *
        0.82F;
    const float warp_y =
        (fbm({.x = p.x * 0.54F + 6.0F, .y = p.y * 0.54F - 3.0F},
             {.x = seed_value.x - 173.0F, .y = seed_value.y + 179.0F}, 4) -
         0.5F) *
        0.70F;
    const Vec2 q = rotate({.x = p.x + warp_x * 0.64F, .y = p.y + warp_y * 0.58F}, -0.34F);
    const float broad_coast =
        (fbm({.x = q.x * 0.54F, .y = q.y * 0.54F},
             {.x = seed_value.x + 181.0F, .y = seed_value.y - 191.0F}, 5) -
         0.5F) *
        0.58F;
    const float headland_noise =
        (fbm({.x = q.x * 1.32F + 4.0F, .y = q.y * 1.32F - 7.0F},
             {.x = seed_value.x - 193.0F, .y = seed_value.y + 197.0F}, 4) -
         0.5F) *
        0.24F;
    const float bay_cut =
        smoothstep(0.58F, 0.86F,
                   fbm({.x = q.x * 0.82F - 5.0F, .y = q.y * 0.82F + 2.0F},
                       {.x = seed_value.x + 229.0F, .y = seed_value.y - 233.0F}, 4)) *
        smoothstep(-1.10F, 0.12F, q.y) * 0.26F;
    const float coast_field = q.y + 0.18F + broad_coast + headland_noise - bay_cut;
    const float land = smoothstep(-0.12F, 0.15F, coast_field);
    const float shelf = smoothstep(-0.76F, 0.08F, coast_field);
    const float beach = smoothstep(-0.08F, 0.08F, coast_field) *
                        (1.0F - smoothstep(0.11F, 0.30F, coast_field));
    const float coastal_plain = smoothstep(0.02F, 0.45F, coast_field) *
                                (1.0F - smoothstep(0.70F, 1.22F, coast_field));
    const float inland = smoothstep(0.22F, 1.26F, coast_field);
    const float hills =
        fbm({.x = q.x * 0.72F, .y = q.y * 0.72F},
            {.x = seed_value.x + 199.0F, .y = seed_value.y + 211.0F}, 5);
    const float ridges =
        ridged_fbm({.x = q.x * 1.26F + warp_x * 0.34F, .y = q.y * 1.26F - warp_y * 0.28F},
                   {.x = seed_value.x - 223.0F, .y = seed_value.y + 227.0F}, 5);
    const float coastal_cliff =
        smoothstep(0.56F, 0.88F, ridges) * smoothstep(0.02F, 0.25F, coast_field) *
        (1.0F - smoothstep(0.45F, 0.92F, coast_field));
    const float underwater =
        kShadertoyCoastIslandReferenceWaterHeightM - 210.0F + shelf * 194.0F +
        (hills - 0.5F) * 30.0F;
    const float land_height = kShadertoyCoastIslandReferenceWaterHeightM + 16.0F +
                              beach * 30.0F + coastal_plain * (52.0F + hills * 84.0F) +
                              inland * (230.0F + hills * 620.0F) +
                              ridges * inland * 300.0F + coastal_cliff * 260.0F;
    const float height = mix(underwater, land_height, land);
    return std::max(height, -170.0F);
}

float shadertoy_plains_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00016F) + (seed_value.x * 0.061F),
        .y = (world_z * 0.00016F) + (seed_value.y * 0.073F),
    };
    const float macro =
        fbm({.x = p.x * 0.30F, .y = p.y * 0.30F},
            {.x = seed_value.x + 251.0F, .y = seed_value.y - 257.0F}, 5);
    const float roll =
        fbm({.x = p.x * 0.82F, .y = p.y * 0.82F},
            {.x = seed_value.x - 263.0F, .y = seed_value.y + 269.0F}, 4);
    const float wind_angle = 0.28F + seed_value.x * 0.021F;
    const Vec2 wind{.x = std::cos(wind_angle), .y = std::sin(wind_angle)};
    const float along = (p.x * wind.x) + (p.y * wind.y);
    const float cross = (p.x * -wind.y) + (p.y * wind.x);
    const float swale_source =
        ridged_fbm({.x = (along * 0.58F) + roll * 0.32F, .y = (cross * 0.24F) - roll * 0.20F},
                   {.x = seed_value.x + 271.0F, .y = seed_value.y - 277.0F}, 5);
    const float swales = std::pow(std::max(swale_source, 0.0F), 2.35F);
    const float prairie_detail =
        (fbm({.x = p.x * 2.40F, .y = p.y * 2.40F},
             {.x = seed_value.x - 281.0F, .y = seed_value.y + 283.0F}, 4) -
         0.5F) *
        16.0F;
    return 90.0F + macro * 150.0F + roll * 78.0F - swales * 92.0F + prairie_detail;
}

float shadertoy_gorge_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00020F) + (seed_value.x * 0.091F) - 0.16F,
        .y = (world_z * 0.00020F) + (seed_value.y * 0.083F) + 0.08F,
    };
    const GorgeSourceField field = gorge_source_field(p, seed_value);
    const float terrace =
        (triangle_wave((field.q.y * 0.14F) + field.plateau * 0.62F) - 0.5F) *
        smoothstep(0.16F, 0.86F, field.wall + field.main_corridor * 0.35F) * 34.0F;
    const float rough_detail =
        (ridged_fbm({.x = p.x * 2.35F, .y = p.y * 2.35F},
                    {.x = seed_value.x - 397.0F, .y = seed_value.y + 401.0F}, 4) -
         0.42F) *
        (38.0F + field.plateau * 46.0F) * (1.0F - field.floor * 0.62F);
    const float base_height = 210.0F + field.plateau * 1320.0F + field.plateau_source * 260.0F;
    const float incision =
        field.main_corridor * (960.0F + field.plateau * 690.0F) +
        field.floor * (260.0F + field.plateau * 300.0F) +
        field.tributaries * (320.0F + field.plateau * 310.0F);
    const float wall_lift = field.wall * field.plateau * (170.0F + field.main_corridor * 70.0F);
    const float floor_fill = field.floor * (54.0F + (1.0F - field.plateau) * 40.0F);
    const float raw_height =
        base_height - incision + wall_lift + floor_fill + terrace + rough_detail;
    const float dry_floor =
        (32.0F + field.plateau_source * 54.0F + field.corridor_width * 35.0F) *
        (0.35F + field.floor * 0.65F);
    return std::max(raw_height, dry_floor);
}

float shadertoy_glacial_highland_reference_height(float world_x, float world_z,
                                                  std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    const Vec2 p{
        .x = (world_x * 0.00017F) + (seed_value.x * 0.113F) - 1.0F,
        .y = (world_z * 0.00017F) + (seed_value.y * 0.097F) + 3.0F,
    };
    const GlacialSourceField field = glacial_source_field(p, seed_value);
    const float rough =
        (ridged_fbm({.x = p.x * 2.65F, .y = p.y * 2.65F},
                    {.x = seed_value.x - 503.0F, .y = seed_value.y + 509.0F}, 4) -
         0.45F) *
        80.0F * field.uplift * (1.0F - field.trunk_floor * 0.55F) *
        (1.0F - field.ice_field * 0.35F);
    const float broad_height = 240.0F + std::pow(field.uplift, 1.12F) * 2140.0F +
                               std::pow(field.shoulder, 1.65F) * 480.0F;
    const float rib_height =
        std::pow(std::max(field.rock_ribs, 0.0F), 1.15F) * (520.0F + field.trunk_wall * 380.0F);
    const float valley_cut = field.trunk_valley * (500.0F + field.uplift * 620.0F) +
                             field.trunk_floor * (180.0F + field.uplift * 240.0F) +
                             field.side_valleys * (220.0F + field.uplift * 340.0F) +
                             field.cirques * (160.0F + field.uplift * 320.0F);
    const float ice_smoothing =
        field.ice_field * (field.trunk_floor * 170.0F + field.side_valleys * 80.0F +
                           field.cirques * 90.0F);
    const float floor_fill = field.trunk_floor * (100.0F + (1.0F - field.uplift) * 40.0F);
    return std::max(broad_height + rib_height - valley_cut - ice_smoothing + floor_fill + rough,
                    0.0F);
}

float shadertoy_crater_field_reference_height(float world_x, float world_z, std::uint64_t seed) {
    const Vec2 seed_value = seed_components(seed);
    Vec2 p{
        .x = (world_x * 0.00022F) + (seed_value.x * 0.077F),
        .y = (world_z * 0.00022F) + (seed_value.y * 0.089F),
    };
    const float broad =
        fbm({.x = p.x * 0.32F, .y = p.y * 0.32F},
            {.x = seed_value.x + 443.0F, .y = seed_value.y - 449.0F}, 5);
    const float density_mask =
        fbm({.x = p.x * 0.18F, .y = p.y * 0.18F},
            {.x = seed_value.x - 451.0F, .y = seed_value.y + 457.0F}, 4);
    CraterAccumulation craters{};
    accumulate_crater_population(craters, p, seed_value, 0.58F, 0.23F + density_mask * 0.18F,
                                 0.22F, 0.38F, 1.12F, 0.86F, 0.24F,
                                 0.37F, 0.58F,
                                 {.x = 463.0F, .y = -467.0F});
    accumulate_crater_population(craters, p, seed_value, 0.91F, 0.07F + density_mask * 0.10F,
                                 0.30F, 0.46F, 0.92F, 0.48F, 0.16F,
                                 -1.08F, 0.72F,
                                 {.x = 541.0F, .y = -547.0F});
    accumulate_crater_population(craters, p, seed_value, 1.22F, 0.32F + density_mask * 0.22F,
                                 0.16F, 0.32F, 0.78F, 0.58F, 0.18F,
                                 -0.71F, 0.42F,
                                 {.x = -479.0F, .y = 487.0F});
    accumulate_crater_population(craters, p, seed_value, 2.65F, 0.18F + density_mask * 0.24F,
                                 0.09F, 0.16F, 0.42F, 0.30F, 0.10F,
                                 1.19F, 0.26F,
                                 {.x = 491.0F, .y = -499.0F});
    const float rough =
        (ridged_fbm({.x = p.x * 4.1F, .y = p.y * 4.1F},
                    {.x = seed_value.x - 503.0F, .y = seed_value.y + 509.0F}, 4) -
         0.45F) *
        74.0F;
    return std::max(240.0F + broad * 270.0F + craters.rim * 230.0F +
                        craters.ejecta * 105.0F - craters.depression * 330.0F + rough,
                    0.0F);
}

} // namespace cubey::projects::terrain_ref
