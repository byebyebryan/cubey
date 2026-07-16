#include "terrain_mountain_backdrop_source.h"

#include <cubey/procedural/noise.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

struct NoiseSample {
    float value = 0.0F;
    cubey::math::Vec2 derivative{0.0F, 0.0F};
};

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] cubey::math::Vec2 rotated(cubey::math::Vec2 value) {
    constexpr float kCosAngle = 0.7451744F;
    constexpr float kSinAngle = 0.6668696F;
    return {kCosAngle * value.x - kSinAngle * value.y, kSinAngle * value.x + kCosAngle * value.y};
}

[[nodiscard]] float signed_corner(std::int32_t x, std::int32_t y, std::uint64_t seed) {
    constexpr float kScale = 1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(cubey::procedural::hash_u32(x, y, seed)) * kScale * 2.0F - 1.0F;
}

[[nodiscard]] NoiseSample value_noise_derivative(cubey::math::Vec2 position, std::uint64_t seed) {
    const float floor_x = std::floor(position.x);
    const float floor_y = std::floor(position.y);
    const auto x0 = static_cast<std::int32_t>(floor_x);
    const auto y0 = static_cast<std::int32_t>(floor_y);
    const float tx = position.x - floor_x;
    const float ty = position.y - floor_y;
    const float ux = tx * tx * (3.0F - 2.0F * tx);
    const float uy = ty * ty * (3.0F - 2.0F * ty);
    const float dux = 6.0F * tx * (1.0F - tx);
    const float duy = 6.0F * ty * (1.0F - ty);
    const float a = signed_corner(x0, y0, seed);
    const float b = signed_corner(x0 + 1, y0, seed);
    const float c = signed_corner(x0, y0 + 1, seed);
    const float d = signed_corner(x0 + 1, y0 + 1, seed);
    const float lower = std::lerp(a, b, ux);
    const float upper = std::lerp(c, d, ux);
    return {
        .value = std::lerp(lower, upper, uy),
        .derivative =
            {
                dux * std::lerp(b - a, d - c, uy),
                duy * (upper - lower),
            },
    };
}

[[nodiscard]] float octave_visibility(float wavelength_m, float footprint_m) {
    if (footprint_m <= 0.0F) {
        return 1.0F;
    }
    return smoothstep(1.0F, 2.0F, wavelength_m / footprint_m);
}

void validate_query(const TerrainQuery& query) {
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain mountain backdrop query");
    }
}

} // namespace

float sample_terrain_mountain_backdrop_raw(std::uint64_t seed, const TerrainQuery& query) {
    validate_query(query);
    constexpr float kStructurePeriodM = 3'000.0F;
    constexpr float kEnvelopePeriodM = 7'000.0F;
    constexpr float kUpliftPeriodM = 14'000.0F;
    constexpr float kLacunarity = 2.08F;
    constexpr float kSignedGain = -0.32F;
    constexpr std::uint32_t kStructureOctaves = 6U;

    const std::uint64_t envelope_seed =
        cubey::procedural::derive_seed(seed, "terrain.source-study.mountains-v2.envelope");
    const std::uint64_t structure_seed =
        cubey::procedural::derive_seed(seed, "terrain.source-study.mountains-v2.structure");
    const std::uint64_t uplift_seed =
        cubey::procedural::derive_seed(seed, "terrain.source-study.mountains-v2.uplift");

    const float envelope_noise =
        value_noise_derivative(query.world_xz / kEnvelopePeriodM, envelope_seed).value * 0.5F +
        0.5F;
    const float envelope = 0.18F + 0.82F * std::pow(envelope_noise, 1.7F);

    cubey::math::Vec2 position = query.world_xz / kStructurePeriodM;
    float wavelength_m = kStructurePeriodM;
    float amplitude = 1.0F;
    float signed_structure = 0.0F;
    for (std::uint32_t octave = 0U; octave < kStructureOctaves; ++octave) {
        const float visibility = octave_visibility(wavelength_m, query.footprint_m);
        const float noise = value_noise_derivative(position, structure_seed).value * 0.5F + 0.5F;
        const float filtered_noise = std::lerp(0.5F, noise, visibility);
        signed_structure += amplitude * filtered_noise;
        amplitude *= kSignedGain;
        position = rotated(position) * kLacunarity;
        wavelength_m /= kLacunarity;
    }

    const float uplift_noise =
        value_noise_derivative(query.world_xz / kUpliftPeriodM, uplift_seed).value * 0.5F + 0.5F;
    const float sparse_uplift = std::pow(uplift_noise, 4.5F);
    return envelope * (0.22F + signed_structure) + sparse_uplift * 0.70F;
}

TerrainMountainBackdropSource::TerrainMountainBackdropSource(std::uint64_t seed) : seed_(seed) {
    validate_terrain_height_source_metadata(metadata());
}

TerrainHeightSourceMetadata TerrainMountainBackdropSource::metadata() const noexcept {
    return {
        .id = "mountains-hierarchy-v2",
        .seed = seed_,
        .base_height_m = 0.0F,
        .relief_scale_m = 3'500.0F,
        .gradient_step_m = 8.0F,
    };
}

float TerrainMountainBackdropSource::sample_height(const TerrainQuery& query) const {
    const TerrainMountainBackdropCalibration values = calibration();
    return std::max((sample_raw_height(query) - values.raw_p05) * values.scale_m, 0.0F);
}

float TerrainMountainBackdropSource::sample_raw_height(const TerrainQuery& query) const {
    return sample_terrain_mountain_backdrop_raw(seed_, query);
}

TerrainMountainBackdropCalibration TerrainMountainBackdropSource::calibration() const noexcept {
    return terrain_mountain_backdrop_calibration();
}

} // namespace cubey::projects::terrain
