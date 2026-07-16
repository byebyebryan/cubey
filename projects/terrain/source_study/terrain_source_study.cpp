#include "terrain_source_study.h"

#include <cubey/procedural/noise.h>
#include <cubey/procedural/seed.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cubey::projects::terrain {
namespace {

constexpr float kBasePeriodM = 14'000.0F;
constexpr float kCalibrationExtentM = 16'384.0F;
constexpr std::uint32_t kCalibrationSamplesPerAxis = 257U;
constexpr float kCalibratedReliefM = 3'500.0F;
constexpr std::array<std::uint64_t, 3> kCalibrationSeeds{0U, 9012U, 12345U};

constexpr std::array<TerrainSourceStudyRecipeInfo, 8> kRecipes{{
    {TerrainSourceStudyRecipe::ControlV2_1, "control-v2-1", "independent fBm bands",
     "Cubey terrain source v2.1"},
    {TerrainSourceStudyRecipe::TerrainEngineFbm, "terrain-engine-fbm",
     "value fBm with nonlinear uplift", "TerrainEngine-OpenGL"},
    {TerrainSourceStudyRecipe::ElevatedDerivative, "elevated-derivative",
     "accumulated derivative damping", "Elevated source archive"},
    {TerrainSourceStudyRecipe::SwissDerivative, "swiss-derivative",
     "derivative-damped folded relief", "ShaderToy Swiss Alps"},
    {TerrainSourceStudyRecipe::MountainsSigned, "mountains-signed",
     "alternating signed octaves with broad uplift", "ShaderToy Mountains"},
    {TerrainSourceStudyRecipe::MountainsHierarchyV2, "mountains-hierarchy-v2",
     "scale-coupled envelope, signed structure, and sparse uplift",
     "ShaderToy Mountains morphology study"},
    {TerrainSourceStudyRecipe::RainforestCliff, "rainforest-cliff",
     "rotated fBm with bounded cliff remap", "ShaderToy Rainforest"},
    {TerrainSourceStudyRecipe::MountainPeakWarp, "mountain-peak-warp",
     "stationary derivative-warped multifractal", "ShaderToy Mountain Peak"},
}};

struct NoiseSample {
    float value = 0.0F;
    cubey::math::Vec2 derivative{0.0F, 0.0F};
};

struct RawEvaluator {
    TerrainSourceStudyRecipe recipe = TerrainSourceStudyRecipe::ControlV2_1;
    std::uint64_t seed = 0U;
    TerrainSourceParameters control{};
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

[[nodiscard]] std::uint64_t octave_seed(std::uint64_t seed, std::string_view domain,
                                        std::uint32_t octave) {
    return cubey::procedural::derive_seed(seed, domain, octave);
}

[[nodiscard]] float raw_terrain_engine(const RawEvaluator& evaluator, const TerrainQuery& query) {
    cubey::math::Vec2 position = query.world_xz / kBasePeriodM;
    float amplitude = 0.5F;
    float wavelength_m = kBasePeriodM;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0U; octave < 13U; ++octave) {
        const float visibility = octave_visibility(wavelength_m, query.footprint_m);
        if (visibility > 0.0F) {
            const float noise =
                value_noise_derivative(
                    position, octave_seed(evaluator.seed, "terrain.source-study.primary", octave))
                        .value *
                    0.5F +
                0.5F;
            sum += amplitude * visibility * noise;
            weight += amplitude * visibility;
        }
        position = rotated(position) * 2.0F;
        wavelength_m *= 0.5F;
        amplitude *= 0.5F;
    }
    const float unit = weight > 0.0F ? sum / weight : 0.0F;
    return std::pow(std::clamp(unit, 0.0F, 1.0F), 3.0F);
}

[[nodiscard]] float derivative_damped_fbm(const RawEvaluator& evaluator, const TerrainQuery& query,
                                          std::uint32_t octaves, float lacunarity, float gain,
                                          float damping) {
    cubey::math::Vec2 position = query.world_xz / kBasePeriodM;
    cubey::math::Vec2 derivative{0.0F, 0.0F};
    float amplitude = 0.5F;
    float wavelength_m = kBasePeriodM;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0U; octave < octaves; ++octave) {
        const float visibility = octave_visibility(wavelength_m, query.footprint_m);
        if (visibility > 0.0F) {
            const NoiseSample noise = value_noise_derivative(
                position, octave_seed(evaluator.seed, "terrain.source-study.primary", octave));
            // The accumulated derivative is the shaping signal, not another
            // amplitude-weighted height octave. Keeping its full octave
            // response makes this family materially different from plain fBm.
            derivative += noise.derivative * visibility;
            const float derivative_energy =
                derivative.x * derivative.x + derivative.y * derivative.y;
            sum += amplitude * visibility * noise.value / (1.0F + damping * derivative_energy);
            weight += amplitude * visibility;
        }
        position = rotated(position) * lacunarity;
        wavelength_m /= lacunarity;
        amplitude *= gain;
    }
    return weight > 0.0F ? sum / weight : 0.0F;
}

[[nodiscard]] float raw_elevated(const RawEvaluator& evaluator, const TerrainQuery& query) {
    const float field = derivative_damped_fbm(evaluator, query, 9U, 2.0F, 0.52F, 1.15F);
    const float support = smoothstep(-0.36F, 0.58F, field);
    return std::pow(support, 1.45F);
}

[[nodiscard]] float raw_swiss(const RawEvaluator& evaluator, const TerrainQuery& query) {
    const float field = derivative_damped_fbm(evaluator, query, 9U, 2.0F, 0.5F, 1.6F);
    const float folded = std::abs(field) * 2.0F - 1.0F;
    const float activation = smoothstep(-0.82F, 0.38F, folded);
    return activation * std::max(folded + 0.82F, 0.0F);
}

[[nodiscard]] float raw_mountains(const RawEvaluator& evaluator, const TerrainQuery& query) {
    cubey::math::Vec2 position = query.world_xz / kBasePeriodM;
    const float broad_driver =
        value_noise_derivative(
            position * 0.25F,
            cubey::procedural::derive_seed(evaluator.seed, "terrain.source-study.secondary"))
                .value *
            0.75F +
        0.15F;
    const float uplift = 0.06F + broad_driver * broad_driver;
    float amplitude = 1.0F;
    float wavelength_m = kBasePeriodM;
    float signed_sum = 0.0F;
    float weight = 0.0F;
    const std::uint64_t structure_seed =
        cubey::procedural::derive_seed(evaluator.seed, "terrain.source-study.primary");
    for (std::uint32_t octave = 0U; octave < 8U; ++octave) {
        const float visibility = octave_visibility(wavelength_m, query.footprint_m);
        const float noise = value_noise_derivative(position, structure_seed).value * 0.5F + 0.5F;
        signed_sum += amplitude * visibility * noise;
        weight += std::abs(amplitude) * visibility;
        amplitude *= -0.4F;
        position = rotated(position) * 2.05F;
        wavelength_m /= 2.05F;
    }
    const float structure = weight > 0.0F ? signed_sum / weight : 0.0F;
    const float remote_mass = std::pow(
        std::abs(value_noise_derivative(query.world_xz / (kBasePeriodM * 12.0F),
                                        cubey::procedural::derive_seed(
                                            evaluator.seed, "terrain.source-study.remote-mass"))
                     .value),
        5.0F);
    return uplift * (0.35F + 1.65F * structure) + 0.18F * remote_mass;
}

[[nodiscard]] float raw_mountains_hierarchy_v2(const RawEvaluator& evaluator,
                                               const TerrainQuery& query) {
    constexpr float kStructurePeriodM = 3'000.0F;
    constexpr float kEnvelopePeriodM = 7'000.0F;
    constexpr float kUpliftPeriodM = 14'000.0F;
    constexpr float kLacunarity = 2.08F;
    constexpr float kSignedGain = -0.32F;
    constexpr std::uint32_t kStructureOctaves = 6U;

    const std::uint64_t envelope_seed = cubey::procedural::derive_seed(
        evaluator.seed, "terrain.source-study.mountains-v2.envelope");
    const std::uint64_t structure_seed = cubey::procedural::derive_seed(
        evaluator.seed, "terrain.source-study.mountains-v2.structure");
    const std::uint64_t uplift_seed =
        cubey::procedural::derive_seed(evaluator.seed, "terrain.source-study.mountains-v2.uplift");

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

[[nodiscard]] float raw_rainforest(const RawEvaluator& evaluator, const TerrainQuery& query) {
    cubey::math::Vec2 position = query.world_xz / kBasePeriodM;
    float amplitude = 0.5F;
    float wavelength_m = kBasePeriodM;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0U; octave < 9U; ++octave) {
        const float visibility = octave_visibility(wavelength_m, query.footprint_m);
        sum += amplitude * visibility *
               value_noise_derivative(
                   position, octave_seed(evaluator.seed, "terrain.source-study.primary", octave))
                   .value;
        weight += amplitude * visibility;
        amplitude *= 0.55F;
        position = rotated(position) * 1.9F;
        wavelength_m /= 1.9F;
    }
    const float unit = std::clamp(0.5F + 0.5F * (weight > 0.0F ? sum / weight : 0.0F), 0.0F, 1.0F);
    return unit + 0.15F * smoothstep(0.46F, 0.56F, unit);
}

[[nodiscard]] float raw_mountain_peak(const RawEvaluator& evaluator, const TerrainQuery& query) {
    cubey::math::Vec2 position = query.world_xz / kBasePeriodM;
    cubey::math::Vec2 derivative_sum{0.0F, 0.0F};
    float amplitude = 1.0F;
    float wavelength_m = kBasePeriodM;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0U; octave < 8U; ++octave) {
        const float visibility = octave_visibility(wavelength_m, query.footprint_m);
        const cubey::math::Vec2 warped = position - derivative_sum * 0.34F;
        const NoiseSample noise = value_noise_derivative(
            warped, octave_seed(evaluator.seed, "terrain.source-study.primary", octave));
        const float unit = std::clamp(noise.value * 0.5F + 0.5F, 0.0F, 1.0F);
        const float choppy = std::pow(unit, 1.75F);
        sum += choppy * amplitude * visibility;
        weight += amplitude * visibility;
        derivative_sum += noise.derivative * ((unit * 2.0F - 1.0F) * amplitude * visibility);
        amplitude *= 0.58F * std::lerp(0.72F, 1.0F, std::pow(unit, 0.32F));
        position = rotated(position) * 2.35F;
        wavelength_m /= 2.35F;
    }
    return weight > 0.0F ? sum / weight : 0.0F;
}

[[nodiscard]] RawEvaluator raw_evaluator(TerrainSourceStudyRecipe recipe, std::uint64_t seed) {
    return {
        .recipe = recipe,
        .seed = seed,
        .control = resolve_terrain_source_parameters({
            .seed = seed,
            .preset = TerrainPreset::Mountain,
            .version = TerrainSourceVersion::V2_1,
            .weathering = TerrainWeatheringMode::Off,
        }),
    };
}

[[nodiscard]] float sample_raw(const RawEvaluator& evaluator, const TerrainQuery& query) {
    switch (evaluator.recipe) {
    case TerrainSourceStudyRecipe::ControlV2_1:
        return sample_terrain_height(evaluator.control, query);
    case TerrainSourceStudyRecipe::TerrainEngineFbm:
        return raw_terrain_engine(evaluator, query);
    case TerrainSourceStudyRecipe::ElevatedDerivative:
        return raw_elevated(evaluator, query);
    case TerrainSourceStudyRecipe::SwissDerivative:
        return raw_swiss(evaluator, query);
    case TerrainSourceStudyRecipe::MountainsSigned:
        return raw_mountains(evaluator, query);
    case TerrainSourceStudyRecipe::MountainsHierarchyV2:
        return raw_mountains_hierarchy_v2(evaluator, query);
    case TerrainSourceStudyRecipe::RainforestCliff:
        return raw_rainforest(evaluator, query);
    case TerrainSourceStudyRecipe::MountainPeakWarp:
        return raw_mountain_peak(evaluator, query);
    }
    return 0.0F;
}

[[nodiscard]] float quantile(const std::vector<float>& sorted, float q) {
    const float index = q * static_cast<float>(sorted.size() - 1U);
    const std::size_t lower = static_cast<std::size_t>(std::floor(index));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(index));
    return std::lerp(sorted[lower], sorted[upper], index - static_cast<float>(lower));
}

} // namespace

std::span<const TerrainSourceStudyRecipeInfo> terrain_source_study_recipes() noexcept {
    return kRecipes;
}

std::string_view terrain_source_study_recipe_name(TerrainSourceStudyRecipe recipe) noexcept {
    const auto found = std::find_if(kRecipes.begin(), kRecipes.end(),
                                    [recipe](const auto& info) { return info.recipe == recipe; });
    return found != kRecipes.end() ? found->id : "control-v2-1";
}

TerrainSourceStudyRecipe terrain_source_study_recipe_from_name(std::string_view name) {
    if (name.empty()) {
        return TerrainSourceStudyRecipe::ControlV2_1;
    }
    const auto found = std::find_if(kRecipes.begin(), kRecipes.end(),
                                    [name](const auto& info) { return info.id == name; });
    if (found == kRecipes.end()) {
        throw std::runtime_error("unknown terrain source study recipe: " + std::string(name));
    }
    return found->recipe;
}

TerrainSourceStudyCalibration terrain_source_study_calibration(TerrainSourceStudyRecipe recipe) {
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(kCalibrationSeeds.size()) *
                    kCalibrationSamplesPerAxis * kCalibrationSamplesPerAxis);
    const float denominator = static_cast<float>(kCalibrationSamplesPerAxis - 1U);
    const float footprint_m = 2.0F * kCalibrationExtentM / denominator;
    for (const std::uint64_t seed : kCalibrationSeeds) {
        const RawEvaluator evaluator = raw_evaluator(recipe, seed);
        for (std::uint32_t z = 0U; z < kCalibrationSamplesPerAxis; ++z) {
            for (std::uint32_t x = 0U; x < kCalibrationSamplesPerAxis; ++x) {
                const cubey::math::Vec2 world_xz{
                    std::lerp(-kCalibrationExtentM, kCalibrationExtentM,
                              static_cast<float>(x) / denominator),
                    std::lerp(-kCalibrationExtentM, kCalibrationExtentM,
                              static_cast<float>(z) / denominator),
                };
                const float value = sample_raw(evaluator, {
                                                              .world_xz = world_xz,
                                                              .footprint_m = footprint_m,
                                                          });
                if (!std::isfinite(value)) {
                    throw std::runtime_error(
                        "terrain source study calibration sampled non-finite height");
                }
                samples.push_back(value);
            }
        }
    }
    std::sort(samples.begin(), samples.end());
    const float p05 = quantile(samples, 0.05F);
    const float p95 = quantile(samples, 0.95F);
    if (!(p95 > p05)) {
        throw std::runtime_error("terrain source study calibration has no relief");
    }
    return {
        .raw_p05 = p05,
        .raw_p95 = p95,
        .scale_m = kCalibratedReliefM / (p95 - p05),
        .sample_count = samples.size(),
    };
}

TerrainSourceStudySource::TerrainSourceStudySource(TerrainSourceStudyRecipe recipe,
                                                   std::uint64_t seed)
    : TerrainSourceStudySource(recipe, seed, terrain_source_study_calibration(recipe)) {}

TerrainSourceStudySource::TerrainSourceStudySource(TerrainSourceStudyRecipe recipe,
                                                   std::uint64_t seed,
                                                   TerrainSourceStudyCalibration calibration)
    : recipe_(recipe), seed_(seed), calibration_(calibration),
      control_parameters_(raw_evaluator(recipe, seed).control) {
    if (!std::isfinite(calibration_.raw_p05) || !std::isfinite(calibration_.raw_p95) ||
        !std::isfinite(calibration_.scale_m) || calibration_.raw_p95 <= calibration_.raw_p05 ||
        calibration_.scale_m <= 0.0F || calibration_.sample_count == 0U) {
        throw std::runtime_error("invalid terrain source study calibration");
    }
    validate_terrain_height_source_metadata(metadata());
}

TerrainHeightSourceMetadata TerrainSourceStudySource::metadata() const noexcept {
    return {
        .id = terrain_source_study_recipe_name(recipe_),
        .seed = seed_,
        .base_height_m = 0.0F,
        .relief_scale_m = kCalibratedReliefM,
        .gradient_step_m = 8.0F,
    };
}

float TerrainSourceStudySource::sample_height(const TerrainQuery& query) const {
    const float raw = sample_raw_height(query);
    return std::max((raw - calibration_.raw_p05) * calibration_.scale_m, 0.0F);
}

TerrainSourceStudyRecipe TerrainSourceStudySource::recipe() const noexcept {
    return recipe_;
}

TerrainSourceStudyCalibration TerrainSourceStudySource::calibration() const noexcept {
    return calibration_;
}

float TerrainSourceStudySource::sample_raw_height(const TerrainQuery& query) const {
    if (!std::isfinite(query.world_xz.x) || !std::isfinite(query.world_xz.y) ||
        !std::isfinite(query.footprint_m) || query.footprint_m < 0.0F) {
        throw std::runtime_error("invalid terrain source study query");
    }
    return sample_raw({.recipe = recipe_, .seed = seed_, .control = control_parameters_}, query);
}

} // namespace cubey::projects::terrain
