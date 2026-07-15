#include "ocean_sea_state.h"

#include "ocean_config.h"

#include <stdexcept>
#include <string>

namespace cubey::projects::ocean {
namespace {

[[nodiscard]] OceanSeaStateSettings sea_state_owned_settings(const OceanConfig& config) {
    OceanSeaStateSettings settings{
        .roughness = config.roughness,
        .normal_strength = config.normal_strength,
        .surface_foam_strength = config.surface_foam_strength,
        .foam_history_strength = config.foam_history_strength,
        .foam_density = config.foam_density,
        .foam_sharpness = config.foam_sharpness,
        .self_shadow_strength = config.self_shadow_strength,
        .far_roughness_strength = config.far_roughness_strength,
        .far_glint_strength = config.far_glint_strength,
        .far_reflection_variation_strength = config.far_reflection_variation_strength,
        .sun_glitter_width = config.sun_glitter_width,
    };
    for (std::size_t index = 0; index < settings.cascades.size(); ++index) {
        const OceanCascadeConfig& cascade = config.cascades[index];
        settings.cascades[index] = {
            .displacement_scale = cascade.displacement_scale,
            .normal_scale = cascade.normal_scale,
            .wind_speed = cascade.wind_speed,
            .fetch_length_km = cascade.fetch_length_km,
            .swell = cascade.swell,
            .spread = cascade.spread,
            .detail = cascade.detail,
            .whitecap = cascade.whitecap,
            .foam_amount = cascade.foam_amount,
        };
    }
    return settings;
}

} // namespace

const char* ocean_sea_state_name(OceanSeaState state) {
    switch (state) {
    case OceanSeaState::Calm:
        return "calm";
    case OceanSeaState::Windy:
        return "windy";
    case OceanSeaState::Stormy:
        return "stormy";
    case OceanSeaState::Custom:
        return "custom";
    }
    return "custom";
}

OceanSeaState ocean_sea_state_from_name(std::string_view name) {
    if (name.empty() || name == "windy") {
        return OceanSeaState::Windy;
    }
    if (name == "calm") {
        return OceanSeaState::Calm;
    }
    if (name == "stormy") {
        return OceanSeaState::Stormy;
    }
    throw std::runtime_error("unknown ocean sea state: " + std::string(name));
}

OceanSeaStateSettings ocean_sea_state_settings(OceanSeaState state) {
    switch (state) {
    case OceanSeaState::Calm:
        return {
            .cascades =
                {
                    OceanSeaStateCascadeSettings{.displacement_scale = 0.60F,
                                                 .normal_scale = 0.58F,
                                                 .wind_speed = 5.0F,
                                                 .fetch_length_km = 35.0F,
                                                 .swell = 0.55F,
                                                 .spread = 0.38F,
                                                 .detail = 0.90F,
                                                 .whitecap = 0.18F,
                                                 .foam_amount = 1.0F},
                    OceanSeaStateCascadeSettings{.displacement_scale = 0.36F,
                                                 .normal_scale = 0.46F,
                                                 .wind_speed = 4.0F,
                                                 .fetch_length_km = 25.0F,
                                                 .swell = 0.45F,
                                                 .spread = 0.50F,
                                                 .detail = 0.84F,
                                                 .whitecap = 0.14F,
                                                 .foam_amount = 0.65F},
                },
            .roughness = 0.22F,
            .normal_strength = 0.68F,
            .surface_foam_strength = 0.24F,
            .foam_history_strength = 0.30F,
            .foam_density = 1.0F,
            .foam_sharpness = 0.62F,
            .self_shadow_strength = 0.15F,
            .far_roughness_strength = 0.035F,
            .far_glint_strength = 0.38F,
            .far_reflection_variation_strength = 0.018F,
            .sun_glitter_width = 0.065F,
        };
    case OceanSeaState::Windy:
        return {
            .cascades =
                {
                    OceanSeaStateCascadeSettings{.displacement_scale = 1.04F,
                                                 .normal_scale = 0.94F,
                                                 .wind_speed = 11.0F,
                                                 .fetch_length_km = 150.0F,
                                                 .swell = 0.84F,
                                                 .spread = 0.20F,
                                                 .detail = 0.96F,
                                                 .whitecap = 0.70F,
                                                 .foam_amount = 3.70F},
                    OceanSeaStateCascadeSettings{.displacement_scale = 0.72F,
                                                 .normal_scale = 0.78F,
                                                 .wind_speed = 9.0F,
                                                 .fetch_length_km = 110.0F,
                                                 .swell = 0.74F,
                                                 .spread = 0.30F,
                                                 .detail = 0.90F,
                                                 .whitecap = 0.66F,
                                                 .foam_amount = 2.70F},
                },
            .roughness = 0.34F,
            .normal_strength = 0.82F,
            .surface_foam_strength = 0.92F,
            .foam_history_strength = 0.80F,
            .foam_density = 2.65F,
            .foam_sharpness = 0.65F,
            .self_shadow_strength = 0.30F,
            .far_roughness_strength = 0.14F,
            .far_glint_strength = 0.32F,
            .far_reflection_variation_strength = 0.085F,
            .sun_glitter_width = 0.095F,
        };
    case OceanSeaState::Stormy:
        return {
            .cascades =
                {
                    OceanSeaStateCascadeSettings{.displacement_scale = 1.42F,
                                                 .normal_scale = 1.14F,
                                                 .wind_speed = 18.0F,
                                                 .fetch_length_km = 350.0F,
                                                 .swell = 1.0F,
                                                 .spread = 0.12F,
                                                 .detail = 0.98F,
                                                 .whitecap = 0.50F,
                                                 .foam_amount = 5.60F},
                    OceanSeaStateCascadeSettings{.displacement_scale = 1.10F,
                                                 .normal_scale = 1.02F,
                                                 .wind_speed = 16.0F,
                                                 .fetch_length_km = 330.0F,
                                                 .swell = 0.95F,
                                                 .spread = 0.22F,
                                                 .detail = 0.95F,
                                                 .whitecap = 0.48F,
                                                 .foam_amount = 4.50F},
                },
            .roughness = 0.42F,
            .normal_strength = 0.92F,
            .surface_foam_strength = 1.08F,
            .foam_history_strength = 0.96F,
            .foam_density = 3.0F,
            .foam_sharpness = 0.67F,
            .self_shadow_strength = 0.45F,
            .far_roughness_strength = 0.22F,
            .far_glint_strength = 0.27F,
            .far_reflection_variation_strength = 0.14F,
            .sun_glitter_width = 0.12F,
        };
    case OceanSeaState::Custom:
        break;
    }
    throw std::runtime_error("custom ocean sea state has no preset settings");
}

void apply_ocean_sea_state(OceanConfig& config, OceanSeaState state) {
    const OceanSeaStateSettings settings = ocean_sea_state_settings(state);
    config.sea_state = state;
    config.cascade_enabled = {true, true, false, false, false};
    for (std::size_t index = 0; index < settings.cascades.size(); ++index) {
        OceanCascadeConfig& cascade = config.cascades[index];
        const OceanSeaStateCascadeSettings& cascade_settings = settings.cascades[index];
        cascade.displacement_scale = cascade_settings.displacement_scale;
        cascade.normal_scale = cascade_settings.normal_scale;
        cascade.wind_speed = cascade_settings.wind_speed;
        cascade.fetch_length_km = cascade_settings.fetch_length_km;
        cascade.swell = cascade_settings.swell;
        cascade.spread = cascade_settings.spread;
        cascade.detail = cascade_settings.detail;
        cascade.whitecap = cascade_settings.whitecap;
        cascade.foam_amount = cascade_settings.foam_amount;
    }
    config.roughness = settings.roughness;
    config.normal_strength = settings.normal_strength;
    config.surface_foam_strength = settings.surface_foam_strength;
    config.foam_history_strength = settings.foam_history_strength;
    config.foam_density = settings.foam_density;
    config.foam_sharpness = settings.foam_sharpness;
    config.self_shadow_strength = settings.self_shadow_strength;
    config.far_roughness_strength = settings.far_roughness_strength;
    config.far_glint_strength = settings.far_glint_strength;
    config.far_reflection_variation_strength = settings.far_reflection_variation_strength;
    config.sun_glitter_width = settings.sun_glitter_width;
}

bool ocean_config_matches_sea_state(const OceanConfig& config, OceanSeaState state) {
    return state != OceanSeaState::Custom &&
           config.cascade_enabled ==
               std::array<bool, kOceanCascadeCount>{true, true, false, false, false} &&
           sea_state_owned_settings(config) == ocean_sea_state_settings(state);
}

OceanSeaState ocean_infer_sea_state(const OceanConfig& config) {
    for (const OceanSeaState state : kOceanSeaStatePresets) {
        if (ocean_config_matches_sea_state(config, state)) {
            return state;
        }
    }
    return OceanSeaState::Custom;
}

} // namespace cubey::projects::ocean
