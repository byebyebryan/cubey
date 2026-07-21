#include "terrain_surface_model.h"

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

} // namespace

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
    float snow = mountain_factor * smoothstep(0.25F, 0.39F, height) *
                 smoothstep(0.30F, 0.82F, normal_y);
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
    if (!std::isfinite(climate.temperature_mean_c) ||
        !std::isfinite(climate.temperature_stddev_c) ||
        !std::isfinite(climate.precipitation_annual_mm) ||
        !std::isfinite(climate.precipitation_cv) || climate.temperature_stddev_c < 0.0F ||
        climate.precipitation_annual_mm < 0.0F || climate.precipitation_cv < 0.0F ||
        climate.precipitation_cv > 1.0F) {
        throw std::runtime_error("terrain climate sample violates its physical contract");
    }

    const float effective_temperature =
        std::max(climate.temperature_mean_c + 0.5F * climate.temperature_stddev_c, 0.0F);
    const float potential_evapotranspiration =
        std::max(250.0F + 25.0F * effective_temperature +
                     0.7F * effective_temperature * effective_temperature,
                 250.0F);
    const float aridity = climate.precipitation_annual_mm / potential_evapotranspiration;
    const float effective_moisture =
        aridity * (1.0F - 0.35F * std::clamp(climate.precipitation_cv, 0.0F, 1.0F));
    const float moisture = smoothstep(0.03F, 0.50F, effective_moisture);
    const float aridity_cover = smoothstep(0.02F, 0.28F, effective_moisture);
    const float growth = smoothstep(
        60.0F, 150.0F,
        growing_season_days(climate.temperature_mean_c, climate.temperature_stddev_c));

    const float cold = 1.0F - smoothstep(-1.0F, 3.0F, climate.temperature_mean_c);
    const float wet_snow = smoothstep(150.0F, 400.0F, climate.precipitation_annual_mm);
    snow = std::clamp(mountain_factor * smoothstep(0.18F, 0.45F, height) * cold * wet_snow *
                          smoothstep(0.30F, 0.82F, normal_y),
                      0.0F, 1.0F);
    rock = std::clamp(std::max(exposed_rock, alpine_rock) * (1.0F - snow), 0.0F, 1.0F);
    const float climate_ground = std::max(0.0F, 1.0F - rock - snow);
    const float vegetation =
        std::min(landform_capacity * aridity_cover * growth, climate_ground);
    return {rock, snow, ambient_visibility, vegetation, moisture};
}

} // namespace cubey::projects::terrain
