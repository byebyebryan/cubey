#include "terrain_engine_reference.h"

#include <algorithm>
#include <cmath>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] float fract_positive(float value) {
    return value - std::floor(value);
}

[[nodiscard]] float terrain_engine_reference_seed_component(std::uint64_t seed, int shift,
                                                            float scale) {
    const std::uint64_t bits = (seed >> shift) & 0xffffULL;
    return (static_cast<float>(bits) / 65535.0F) * scale;
}

[[nodiscard]] float terrain_engine_reference_random2d(float x, float y, float seed_x,
                                                      float seed_y) {
    const float dot_value = (x * (12.9898F + seed_x)) + (y * (78.233F + seed_y));
    return fract_positive(std::sin(dot_value) * 43758.5453123F);
}

[[nodiscard]] float terrain_engine_reference_noise(float x, float y, std::uint64_t seed) {
    const TerrainEngineReferenceSeedComponents seed_components =
        terrain_engine_reference_seed_components(seed);
    const float integer_x = std::floor(x);
    const float integer_y = std::floor(y);
    const float fractional_x = x - integer_x;
    const float fractional_y = y - integer_y;
    const float a = terrain_engine_reference_random2d(integer_x, integer_y, seed_components.x,
                                                      seed_components.y);
    const float b = terrain_engine_reference_random2d(integer_x + 1.0F, integer_y,
                                                      seed_components.x, seed_components.y);
    const float c = terrain_engine_reference_random2d(integer_x, integer_y + 1.0F,
                                                      seed_components.x, seed_components.y);
    const float d = terrain_engine_reference_random2d(integer_x + 1.0F, integer_y + 1.0F,
                                                      seed_components.x, seed_components.y);
    const float wx = fractional_x * fractional_x * fractional_x *
                     (10.0F + fractional_x * (-15.0F + 6.0F * fractional_x));
    const float wy = fractional_y * fractional_y * fractional_y *
                     (10.0F + fractional_y * (-15.0F + 6.0F * fractional_y));
    const float k0 = a;
    const float k1 = b - a;
    const float k2 = c - a;
    const float k3 = d - c - b + a;
    return k0 + (k1 * wx) + (k2 * wy) + (k3 * wx * wy);
}

} // namespace

bool is_terrain_engine_reference_recipe(std::string_view recipe_id) {
    return recipe_id == kTerrainRecipeTerrainEngineRef;
}

TerrainEngineReferenceSeedComponents terrain_engine_reference_seed_components(
    std::uint64_t seed) {
    return {
        .x = terrain_engine_reference_seed_component(seed, 0, 17.0F),
        .y = terrain_engine_reference_seed_component(seed, 16, 31.0F),
    };
}

float terrain_engine_reference_height(float world_x, float world_y, std::uint64_t seed) {
    constexpr int kOctaves = 13;
    constexpr float kInputFrequency = 0.01F;
    constexpr float kDisplacementFactor = 20.0F;
    constexpr float kPersistence = 0.5F;
    constexpr float kPower = 3.0F;
    float frequency = 0.005F * kInputFrequency;
    float amplitude = kDisplacementFactor;
    float total = 0.0F;
    for (int octave = 0; octave < kOctaves; ++octave) {
        frequency *= 2.0F;
        amplitude *= kPersistence;
        const float sample_x = frequency * ((0.8F * world_x) + (0.6F * world_y));
        const float sample_y = frequency * ((-0.6F * world_x) + (0.8F * world_y));
        total += terrain_engine_reference_noise(sample_x, sample_y, seed) * amplitude;
    }
    return std::pow(std::max(total, 0.0F), kPower);
}

cubey::procedural::ScalarField2D terrain_engine_reference_height_field(
    cubey::procedural::Grid2DDesc desc, std::uint64_t seed) {
    cubey::procedural::ScalarField2D result(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float world_x = desc.origin_x + (static_cast<float>(x) * desc.cell_size);
            const float world_y = desc.origin_y + (static_cast<float>(y) * desc.cell_size);
            result.at(x, y) = terrain_engine_reference_height(world_x, world_y, seed);
        }
    }
    return result;
}

float terrain_engine_reference_normal_cos_v(float world_x, float world_y, std::uint64_t seed) {
    constexpr float kStepM = 1.0F;
    const float dhdu =
        (terrain_engine_reference_height(world_x + kStepM, world_y, seed) -
         terrain_engine_reference_height(world_x - kStepM, world_y, seed)) /
        (2.0F * kStepM);
    const float dhdv =
        (terrain_engine_reference_height(world_x, world_y + kStepM, seed) -
         terrain_engine_reference_height(world_x, world_y - kStepM, seed)) /
        (2.0F * kStepM);
    return 1.0F / std::sqrt(1.0F + (dhdu * dhdu) + (dhdv * dhdv));
}

} // namespace cubey::projects::terrain
