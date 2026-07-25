#include "terrain_surface_model.h"

#include <cubey/procedural/hash.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] float growing_season_days(float temperature_c, float temperature_stddev_c) {
    const float amplitude = std::max(temperature_stddev_c * std::numbers::sqrt2_v<float>, 0.1F);
    const float threshold_ratio = (5.0F - temperature_c) / amplitude;
    if (threshold_ratio <= -1.0F) {
        return 365.0F;
    }
    if (threshold_ratio >= 1.0F) {
        return 0.0F;
    }
    return 365.0F *
           (0.5F - std::asin(std::clamp(threshold_ratio, -1.0F, 1.0F)) /
                       std::numbers::pi_v<float>);
}

void validate_climate_sample(const TerrainClimateSample& climate) {
    if (!std::isfinite(climate.temperature_mean_c) ||
        !std::isfinite(climate.temperature_stddev_c) ||
        !std::isfinite(climate.precipitation_annual_mm) ||
        !std::isfinite(climate.precipitation_cv) || climate.temperature_stddev_c < 0.0F ||
        climate.precipitation_annual_mm < 0.0F || climate.precipitation_cv < 0.0F ||
        climate.precipitation_cv > 1.0F) {
        throw std::runtime_error("terrain climate sample violates its physical contract");
    }
}

} // namespace

TerrainClimatePotential terrain_climate_potential(const TerrainClimateSample& climate) {
    validate_climate_sample(climate);
    const float growing_days =
        growing_season_days(climate.temperature_mean_c, climate.temperature_stddev_c);
    const float thermal_growth = smoothstep(60.0F, 150.0F, growing_days);
    const float effective_temperature =
        std::max(climate.temperature_mean_c + 0.5F * climate.temperature_stddev_c, 0.0F);
    const float thermal_water_demand_proxy =
        std::max(250.0F + 25.0F * effective_temperature +
                     0.7F * effective_temperature * effective_temperature,
                 250.0F);
    const float climate_moisture_ratio =
        climate.precipitation_annual_mm / thermal_water_demand_proxy;
    const float seasonality_factor =
        1.0F - 0.35F * std::clamp(climate.precipitation_cv, 0.0F, 1.0F);
    const float effective_moisture = climate_moisture_ratio * seasonality_factor;
    return {
        .growing_season_days = growing_days,
        .thermal_growth = thermal_growth,
        .thermal_water_demand_proxy_mm = thermal_water_demand_proxy,
        .climate_moisture_ratio = climate_moisture_ratio,
        .seasonality_factor = seasonality_factor,
        .effective_moisture = effective_moisture,
        .moisture_weight = smoothstep(0.03F, 0.50F, effective_moisture),
        .cover_weight = smoothstep(0.02F, 0.28F, effective_moisture),
        .annual_cold_potential =
            1.0F - smoothstep(-1.0F, 3.0F, climate.temperature_mean_c),
        .wet_snow_potential =
            smoothstep(150.0F, 400.0F, climate.precipitation_annual_mm),
    };
}

TerrainSurfaceWeights terrain_surface_weights(TerrainSurfaceModel model,
                                              const TerrainSurfaceInputs& inputs) {
    if (!std::isfinite(inputs.normalized_height) || !std::isfinite(inputs.slope) ||
        !std::isfinite(inputs.normal_y) || !std::isfinite(inputs.concavity_m) ||
        !std::isfinite(inputs.relief_scale_m) || inputs.relief_scale_m <= 0.0F) {
        throw std::runtime_error("terrain surface inputs must be finite");
    }

    const float height = std::clamp(inputs.normalized_height, 0.0F, 1.0F);
    const float slope = std::clamp(inputs.slope, 0.0F, 1.0F);
    const float normal_y = std::clamp(inputs.normal_y, 0.0F, 1.0F);
    const float mountain_factor = smoothstep(1'300.0F, 2'800.0F, inputs.relief_scale_m);
    const float exposed_rock = smoothstep(0.17F, 0.54F, slope);
    const float alpine_rock = mountain_factor * smoothstep(0.42F, 0.72F, height) *
                              smoothstep(0.035F, 0.30F, slope);
    float snow = mountain_factor * smoothstep(0.58F, 0.80F, height) *
                 smoothstep(0.38F, 0.78F, normal_y);
    snow = std::clamp(snow, 0.0F, 1.0F);
    float rock =
        std::clamp(std::max(exposed_rock, alpine_rock) * (1.0F - snow), 0.0F, 1.0F);
    const float ambient_visibility =
        1.0F - 0.35F * smoothstep(20.0F, 240.0F, inputs.concavity_m);

    if (model == TerrainSurfaceModel::MineralControl) {
        return {rock, snow, ambient_visibility, 0.0F, 0.0F};
    }

    const float flatness = 1.0F - smoothstep(0.055F, 0.22F, slope);
    const float lowland = 1.0F - smoothstep(0.16F, 0.48F, height);
    const float valley = smoothstep(10.0F, 180.0F, inputs.concavity_m);
    const float available_ground = std::max(0.0F, 1.0F - rock - snow);
    const float landform_capacity =
        std::clamp((0.62F * lowland + 0.38F * valley) * flatness * available_ground, 0.0F, 1.0F);

    if (model == TerrainSurfaceModel::LandformTransition) {
        const float moisture =
            std::clamp(0.18F + 0.45F * valley + 0.12F * lowland, 0.0F, 1.0F);
        return {rock, snow, ambient_visibility, 0.58F * landform_capacity, moisture};
    }

    if (!inputs.climate.has_value()) {
        throw std::runtime_error("climate transition requires a climate sample");
    }
    const TerrainClimateSample climate = inputs.climate.value();
    const TerrainClimatePotential potential = terrain_climate_potential(climate);

    snow = std::clamp(mountain_factor * smoothstep(0.18F, 0.45F, height) *
                          potential.annual_cold_potential * potential.wet_snow_potential *
                          smoothstep(0.30F, 0.82F, normal_y),
                      0.0F, 1.0F);
    rock = std::clamp(std::max(exposed_rock, alpine_rock) * (1.0F - snow), 0.0F, 1.0F);
    const float climate_ground = std::max(0.0F, 1.0F - rock - snow);
    const float vegetation =
        std::min(landform_capacity * potential.cover_weight * potential.thermal_growth,
                 climate_ground);
    return {rock, snow, ambient_visibility, vegetation, potential.moisture_weight};
}

std::uint64_t terrain_surface_model_parameter_hash(TerrainSurfaceModel model) {
    switch (model) {
    case TerrainSurfaceModel::MineralControl:
    case TerrainSurfaceModel::LandformTransition:
    case TerrainSurfaceModel::ClimateTransition:
        break;
    default:
        throw std::runtime_error("terrain surface model is invalid");
    }
    cubey::procedural::ProceduralHashBuilder hash;
    hash.append_string(kTerrainSurfaceModelFormulaVersion);
    hash.append_u32(static_cast<std::uint32_t>(model));
    return hash.value();
}

} // namespace cubey::projects::terrain
