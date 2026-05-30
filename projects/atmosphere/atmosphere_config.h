#pragma once

#include <cubey/core/math.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::atmosphere {

enum class AtmospherePreset : std::uint32_t {
    Noon = 0,
    LowSun = 1,
    Sunset = 2,
    Hazy = 3,
    ThinAir = 4,
    HighAltitude = 5,
};

inline constexpr std::array<AtmospherePreset, 6> kAtmospherePresets{
    AtmospherePreset::Noon, AtmospherePreset::LowSun,  AtmospherePreset::Sunset,
    AtmospherePreset::Hazy, AtmospherePreset::ThinAir, AtmospherePreset::HighAltitude,
};

enum class AtmosphereRenderView : std::uint32_t {
    Final = 0,
    Rayleigh = 1,
    Mie = 2,
    Transmittance = 3,
    OpticalDepth = 4,
    SunDisk = 5,
    AerialPerspective = 6,
};

inline constexpr std::array<AtmosphereRenderView, 7> kAtmosphereRenderViews{
    AtmosphereRenderView::Final,
    AtmosphereRenderView::Rayleigh,
    AtmosphereRenderView::Mie,
    AtmosphereRenderView::Transmittance,
    AtmosphereRenderView::OpticalDepth,
    AtmosphereRenderView::SunDisk,
    AtmosphereRenderView::AerialPerspective,
};

struct AtmosphereConfig {
    AtmospherePreset preset = AtmospherePreset::Noon;
    AtmosphereRenderView render_view = AtmosphereRenderView::Final;

    float bottom_radius_km = 6371.0F;
    float top_radius_km = 6471.0F;
    cubey::math::Vec3 rayleigh_scattering{0.005802F, 0.013558F, 0.033100F};
    float rayleigh_scale_height_km = 8.0F;
    float rayleigh_density_scale = 1.0F;

    float mie_scattering = 0.003996F;
    float mie_extinction = 0.004400F;
    float mie_scale_height_km = 1.2F;
    float mie_anisotropy = 0.80F;
    float mie_density_scale = 1.0F;

    cubey::math::Vec3 ozone_absorption{0.000650F, 0.001881F, 0.000085F};
    float ozone_center_altitude_km = 25.0F;
    float ozone_half_width_km = 15.0F;

    float ground_albedo = 0.10F;
    float sun_angular_radius = 0.004675F;
    float sun_elevation_degrees = 60.0F;
    float sun_azimuth_degrees = 0.0F;
    float camera_altitude_km = 0.15F;
    float exposure = 0.0F;
    bool reference_geometry_enabled = true;
    float reference_grid_km = 1.0F;
    float reference_intensity = 0.72F;
};

[[nodiscard]] inline const char* atmosphere_preset_name(AtmospherePreset preset) {
    switch (preset) {
    case AtmospherePreset::Noon:
        return "noon";
    case AtmospherePreset::LowSun:
        return "low-sun";
    case AtmospherePreset::Sunset:
        return "sunset";
    case AtmospherePreset::Hazy:
        return "hazy";
    case AtmospherePreset::ThinAir:
        return "thin-air";
    case AtmospherePreset::HighAltitude:
        return "high-altitude";
    }
    return "noon";
}

[[nodiscard]] inline AtmospherePreset atmosphere_preset_from_name(std::string_view name) {
    if (name.empty()) {
        return AtmospherePreset::Noon;
    }
    for (const AtmospherePreset preset : kAtmospherePresets) {
        if (name == atmosphere_preset_name(preset)) {
            return preset;
        }
    }
    throw std::runtime_error("unknown atmosphere preset: " + std::string(name));
}

[[nodiscard]] inline const char* atmosphere_render_view_name(AtmosphereRenderView view) {
    switch (view) {
    case AtmosphereRenderView::Final:
        return "final";
    case AtmosphereRenderView::Rayleigh:
        return "rayleigh";
    case AtmosphereRenderView::Mie:
        return "mie";
    case AtmosphereRenderView::Transmittance:
        return "transmittance";
    case AtmosphereRenderView::OpticalDepth:
        return "optical-depth";
    case AtmosphereRenderView::SunDisk:
        return "sun-disk";
    case AtmosphereRenderView::AerialPerspective:
        return "aerial-perspective";
    }
    return "final";
}

[[nodiscard]] inline AtmosphereRenderView atmosphere_render_view_from_name(std::string_view name) {
    if (name.empty()) {
        return AtmosphereRenderView::Final;
    }
    for (const AtmosphereRenderView view : kAtmosphereRenderViews) {
        if (name == atmosphere_render_view_name(view)) {
            return view;
        }
    }
    throw std::runtime_error("unknown atmosphere render view: " + std::string(name));
}

[[nodiscard]] inline AtmosphereRenderView next_atmosphere_render_view(AtmosphereRenderView view) {
    for (std::size_t index = 0; index < kAtmosphereRenderViews.size(); ++index) {
        if (kAtmosphereRenderViews[index] == view) {
            return kAtmosphereRenderViews[(index + 1U) % kAtmosphereRenderViews.size()];
        }
    }
    return AtmosphereRenderView::Final;
}

[[nodiscard]] inline AtmosphereConfig atmosphere_config_for_preset(AtmospherePreset preset) {
    AtmosphereConfig config;
    config.preset = preset;
    switch (preset) {
    case AtmospherePreset::Noon:
        config.sun_elevation_degrees = 60.0F;
        config.sun_azimuth_degrees = 0.0F;
        config.camera_altitude_km = 0.15F;
        config.exposure = 0.0F;
        break;
    case AtmospherePreset::LowSun:
        config.sun_elevation_degrees = 12.0F;
        config.sun_azimuth_degrees = -32.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 1.15F;
        break;
    case AtmospherePreset::Sunset:
        config.sun_elevation_degrees = 2.0F;
        config.sun_azimuth_degrees = -48.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 1.45F;
        break;
    case AtmospherePreset::Hazy:
        config.sun_elevation_degrees = 18.0F;
        config.sun_azimuth_degrees = -24.0F;
        config.camera_altitude_km = 0.15F;
        config.mie_density_scale = 3.50F;
        break;
    case AtmospherePreset::ThinAir:
        config.sun_elevation_degrees = 50.0F;
        config.sun_azimuth_degrees = 15.0F;
        config.camera_altitude_km = 0.15F;
        config.rayleigh_density_scale = 0.45F;
        config.mie_density_scale = 0.25F;
        break;
    case AtmospherePreset::HighAltitude:
        config.sun_elevation_degrees = 10.0F;
        config.sun_azimuth_degrees = -58.0F;
        config.camera_altitude_km = 25.0F;
        config.mie_density_scale = 0.35F;
        break;
    }
    return config;
}

[[nodiscard]] inline bool atmosphere_vec3_nonnegative(cubey::math::Vec3 value) {
    return value.x >= 0.0F && value.y >= 0.0F && value.z >= 0.0F;
}

inline void validate_atmosphere_config(const AtmosphereConfig& config) {
    const auto require_finite = [](float value, const char* name) {
        if (!std::isfinite(value)) {
            throw std::runtime_error(std::string(name) + " must be finite");
        }
    };
    require_finite(config.bottom_radius_km, "atmosphere bottom radius");
    require_finite(config.top_radius_km, "atmosphere top radius");
    require_finite(config.rayleigh_scale_height_km, "atmosphere Rayleigh scale height");
    require_finite(config.mie_scale_height_km, "atmosphere Mie scale height");
    require_finite(config.mie_anisotropy, "atmosphere Mie anisotropy");
    require_finite(config.camera_altitude_km, "atmosphere camera altitude");
    require_finite(config.reference_grid_km, "atmosphere reference grid scale");
    require_finite(config.reference_intensity, "atmosphere reference intensity");

    if (config.bottom_radius_km <= 0.0F || config.top_radius_km <= config.bottom_radius_km) {
        throw std::runtime_error("atmosphere radii must be positive and ordered bottom < top");
    }
    if (!atmosphere_vec3_nonnegative(config.rayleigh_scattering) ||
        !atmosphere_vec3_nonnegative(config.ozone_absorption)) {
        throw std::runtime_error("atmosphere coefficients must be nonnegative");
    }
    if (config.rayleigh_scale_height_km <= 0.0F || config.mie_scale_height_km <= 0.0F ||
        config.ozone_half_width_km <= 0.0F) {
        throw std::runtime_error("atmosphere scale heights must be positive");
    }
    if (config.rayleigh_density_scale < 0.0F || config.mie_density_scale < 0.0F ||
        config.mie_scattering < 0.0F || config.mie_extinction < 0.0F) {
        throw std::runtime_error("atmosphere density and Mie coefficients must be nonnegative");
    }
    if (config.mie_anisotropy < 0.0F || config.mie_anisotropy >= 1.0F) {
        throw std::runtime_error("atmosphere Mie anisotropy must be in [0, 1)");
    }
    if (config.ground_albedo < 0.0F || config.ground_albedo > 1.0F) {
        throw std::runtime_error("atmosphere ground albedo must be in [0, 1]");
    }
    if (config.sun_angular_radius <= 0.0F || config.camera_altitude_km < 0.0F) {
        throw std::runtime_error("atmosphere sun radius must be positive and altitude nonnegative");
    }
    if (config.reference_grid_km <= 0.0F || config.reference_intensity < 0.0F) {
        throw std::runtime_error("atmosphere reference grid scale must be positive");
    }
}

[[nodiscard]] inline AtmosphereConfig atmosphere_config_from_run_config(const RunConfig& run) {
    AtmosphereConfig config =
        atmosphere_config_for_preset(atmosphere_preset_from_name(run.atmosphere.preset));
    config.render_view = atmosphere_render_view_from_name(run.debug_view);
    config.exposure = run.pbr.exposure;
    if (run_config_float_is_set(run.atmosphere.sun_elevation_degrees)) {
        config.sun_elevation_degrees = run.atmosphere.sun_elevation_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.sun_azimuth_degrees)) {
        config.sun_azimuth_degrees = run.atmosphere.sun_azimuth_degrees;
    }
    if (run_config_float_is_set(run.atmosphere.camera_altitude_km)) {
        config.camera_altitude_km = run.atmosphere.camera_altitude_km;
    }
    if (run_config_float_is_set(run.atmosphere.mie_scale)) {
        config.mie_density_scale = run.atmosphere.mie_scale;
    }
    validate_atmosphere_config(config);
    return config;
}

} // namespace cubey::projects::atmosphere
