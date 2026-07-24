#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace cubey::render {

struct OceanSurfaceConfig;

enum class OceanSeaState : std::uint32_t {
    Calm = 0,
    Windy = 1,
    Stormy = 2,
    Custom = 3,
};

inline constexpr std::array<OceanSeaState, 3> kOceanSeaStatePresets{
    OceanSeaState::Calm,
    OceanSeaState::Windy,
    OceanSeaState::Stormy,
};

struct OceanSeaStateCascadeSettings {
    float displacement_scale = 1.0F;
    float normal_scale = 1.0F;
    float wind_speed = 10.0F;
    float fetch_length_km = 150.0F;
    float swell = 0.8F;
    float spread = 0.2F;
    float detail = 1.0F;
    float whitecap = 0.5F;
    float foam_amount = 8.0F;

    friend bool operator==(const OceanSeaStateCascadeSettings&,
                           const OceanSeaStateCascadeSettings&) = default;
};

struct OceanSeaStateSettings {
    std::array<OceanSeaStateCascadeSettings, 2> cascades{};
    float roughness = 0.4F;
    float normal_strength = 1.0F;
    float surface_foam_strength = 1.0F;
    float foam_history_strength = 1.0F;
    float foam_density = 3.15F;
    float foam_sharpness = 0.62F;
    float self_shadow_strength = 0.45F;
    float far_roughness_strength = 0.22F;
    float far_glint_strength = 0.27F;
    float far_reflection_variation_strength = 0.14F;
    float sun_glitter_width = 0.12F;

    friend bool operator==(const OceanSeaStateSettings&, const OceanSeaStateSettings&) = default;
};

[[nodiscard]] const char* ocean_sea_state_name(OceanSeaState state);
[[nodiscard]] OceanSeaState ocean_sea_state_from_name(std::string_view name);
[[nodiscard]] OceanSeaStateSettings ocean_sea_state_settings(OceanSeaState state);
void apply_ocean_sea_state(OceanSurfaceConfig& config, OceanSeaState state);
[[nodiscard]] bool ocean_config_matches_sea_state(const OceanSurfaceConfig& config, OceanSeaState state);
[[nodiscard]] OceanSeaState ocean_infer_sea_state(const OceanSurfaceConfig& config);

} // namespace cubey::render
