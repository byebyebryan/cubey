#include <cubey/engine/atmosphere_environment_config.h>

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey {
namespace {

[[nodiscard]] bool
atmosphere_options_have_manual_sun(const AtmosphereEnvironmentOptions& atmosphere) {
    return atmosphere.sun_elevation_degrees.has_value() ||
           atmosphere.sun_azimuth_degrees.has_value();
}

[[nodiscard]] float atmosphere_exposure_bias(const AtmosphereEnvironmentOptions& atmosphere) {
    return atmosphere.exposure_bias.value_or(0.0F);
}

[[nodiscard]] bool
atmosphere_auto_exposure_enabled(const AtmosphereEnvironmentOptions& atmosphere) {
    return atmosphere.auto_exposure.value_or(true);
}

[[nodiscard]] render::AtmosphereEnvironmentGroundMode
atmosphere_ground_mode_from_options(std::string_view mode) {
    if (mode.empty() || mode == "ground") {
        return render::AtmosphereEnvironmentGroundMode::Ground;
    }
    if (mode == "sky-only") {
        return render::AtmosphereEnvironmentGroundMode::SkyOnly;
    }
    if (mode == "sky-only-no-ground-occlusion") {
        return render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    }
    throw std::runtime_error("unknown atmosphere ground mode: " + std::string(mode));
}

[[nodiscard]] float
resolved_atmosphere_exposure(const render::AtmosphereEnvironmentConfig& environment,
                             bool auto_exposure_enabled, float exposure_bias) {
    if (!auto_exposure_enabled) {
        return 0.0F;
    }
    return render::atmosphere_environment_auto_exposure(environment.sun_elevation_degrees,
                                                        exposure_bias);
}

void apply_atmosphere_time_options(render::AtmosphereEnvironmentConfig& environment,
                                   const AtmosphereEnvironmentOptions& atmosphere) {
    if (atmosphere.time_hours) {
        environment.time_of_day.time_hours = *atmosphere.time_hours;
    }
    if (atmosphere.day_of_year) {
        environment.time_of_day.day_of_year = *atmosphere.day_of_year;
    }
    if (atmosphere.latitude_degrees) {
        environment.time_of_day.latitude_degrees = *atmosphere.latitude_degrees;
    }
    if (atmosphere.sun_azimuth_offset_degrees) {
        environment.time_of_day.azimuth_offset_degrees = *atmosphere.sun_azimuth_offset_degrees;
    }
}

void apply_atmosphere_render_options(render::AtmosphereEnvironmentConfig& environment,
                                     const AtmosphereEnvironmentOptions& atmosphere) {
    if (atmosphere.camera_altitude_km) {
        environment.camera_altitude_km = *atmosphere.camera_altitude_km;
    }
    apply_atmosphere_environment_look_options(environment, atmosphere);
}

void resolve_solar_sun(render::AtmosphereEnvironmentConfig& environment) {
    const render::AtmosphereEnvironmentSolarPosition solar =
        render::atmosphere_environment_solar_position(environment.time_of_day);
    environment.sun_elevation_degrees = solar.elevation_degrees;
    environment.sun_azimuth_degrees = solar.azimuth_degrees;
}

} // namespace

void validate_atmosphere_environment_options(
    const AtmosphereEnvironmentOptions& atmosphere) {
    if (atmosphere.time_of_day_mode && *atmosphere.time_of_day_mode == "solar" &&
        atmosphere_options_have_manual_sun(atmosphere)) {
        throw std::runtime_error(
            "manual sun elevation/azimuth cannot be combined with solar time");
    }
}

void apply_atmosphere_environment_look_options(
    render::AtmosphereEnvironmentConfig& environment,
    const AtmosphereEnvironmentOptions& atmosphere) {
    if (atmosphere.night_sky_mode) {
        if (*atmosphere.night_sky_mode == "camera") {
            environment.night_sky.camera_visual_mode = true;
        } else if (*atmosphere.night_sky_mode == "human") {
            environment.night_sky.camera_visual_mode = false;
        } else {
            throw std::runtime_error("unknown atmosphere night sky mode: " +
                                     *atmosphere.night_sky_mode);
        }
    }
    if (atmosphere.rayleigh_scale) {
        environment.rayleigh_density_scale = *atmosphere.rayleigh_scale;
    }
    if (atmosphere.mie_scale) {
        environment.mie_density_scale = *atmosphere.mie_scale;
    }
    if (atmosphere.ozone_scale) {
        environment.ozone_density_scale = *atmosphere.ozone_scale;
    }
    if (atmosphere.twilight_strength) {
        environment.night_sky.twilight_strength = *atmosphere.twilight_strength;
    }
    if (atmosphere.twilight_horizon_warmth) {
        environment.night_sky.twilight_horizon_warmth = *atmosphere.twilight_horizon_warmth;
    }
    if (atmosphere.star_intensity) {
        environment.night_sky.star_intensity = *atmosphere.star_intensity;
    }
    if (atmosphere.star_density) {
        environment.night_sky.star_density = *atmosphere.star_density;
    }
    if (atmosphere.milky_way_intensity) {
        environment.night_sky.milky_way_intensity = *atmosphere.milky_way_intensity;
    }
    if (atmosphere.milky_way_contrast) {
        environment.night_sky.milky_way_contrast = *atmosphere.milky_way_contrast;
    }
    if (atmosphere.light_pollution) {
        environment.night_sky.light_pollution = *atmosphere.light_pollution;
    }
    if (atmosphere.moonlight_intensity) {
        environment.moon.moonlight_intensity = *atmosphere.moonlight_intensity;
    }
    if (atmosphere.moon_intensity) {
        environment.moon.disk_intensity = *atmosphere.moon_intensity;
    }
    if (atmosphere.moon_phase_offset_days) {
        environment.moon.phase_offset_days = *atmosphere.moon_phase_offset_days;
    }
    if (atmosphere.moon_size_scale) {
        environment.moon.angular_radius_scale = *atmosphere.moon_size_scale;
    }
    if (atmosphere.moon) {
        environment.moon.enabled = *atmosphere.moon;
    }
}

bool atmosphere_environment_options_use_solar_time(
    const AtmosphereEnvironmentOptions& atmosphere) {
    validate_atmosphere_environment_options(atmosphere);
    if (atmosphere.time_of_day_mode && *atmosphere.time_of_day_mode == "solar") {
        return true;
    }
    if (atmosphere.time_of_day_mode && *atmosphere.time_of_day_mode == "manual") {
        return false;
    }
    if (atmosphere_options_have_manual_sun(atmosphere)) {
        return false;
    }
    return true;
}

float atmosphere_environment_options_time_speed(const AtmosphereEnvironmentOptions& atmosphere) {
    return atmosphere.time_speed_hours_per_second.value_or(
        kAtmosphereEnvironmentDefaultTimeSpeedHoursPerSecond);
}

bool atmosphere_environment_options_time_playing(
    const AtmosphereEnvironmentOptions& atmosphere) {
    return !atmosphere.time_paused.value_or(false) &&
           atmosphere_environment_options_time_speed(atmosphere) > 0.0F;
}

AtmosphereEnvironmentRunState
atmosphere_environment_run_state_from_config(const AtmosphereEnvironmentOptions& atmosphere,
                                             const AtmosphereEnvironmentRunDefaults& defaults) {
    render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = defaults.sun_elevation_degrees;
    environment.sun_azimuth_degrees = defaults.sun_azimuth_degrees;
    environment.ground_mode = atmosphere.ground_mode
                                  ? atmosphere_ground_mode_from_options(*atmosphere.ground_mode)
                                  : defaults.ground_mode;
    environment.reference_geometry_enabled = defaults.reference_geometry_enabled;

    apply_atmosphere_time_options(environment, atmosphere);

    const bool solar_time_enabled = atmosphere_environment_options_use_solar_time(atmosphere);
    if (solar_time_enabled) {
        resolve_solar_sun(environment);
    } else {
        if (atmosphere.sun_elevation_degrees) {
            environment.sun_elevation_degrees = *atmosphere.sun_elevation_degrees;
        }
        if (atmosphere.sun_azimuth_degrees) {
            environment.sun_azimuth_degrees = *atmosphere.sun_azimuth_degrees;
        }
    }

    apply_atmosphere_render_options(environment, atmosphere);
    const bool auto_exposure_enabled = atmosphere_auto_exposure_enabled(atmosphere);
    const float exposure_bias = atmosphere_exposure_bias(atmosphere);

    return {
        .environment = environment,
        .solar_time_enabled = solar_time_enabled,
        .time_playing = atmosphere_environment_options_time_playing(atmosphere),
        .time_speed_hours_per_second = atmosphere_environment_options_time_speed(atmosphere),
        .auto_exposure_enabled = auto_exposure_enabled,
        .exposure_bias = exposure_bias,
        .resolved_exposure =
            resolved_atmosphere_exposure(environment, auto_exposure_enabled, exposure_bias),
    };
}

void atmosphere_environment_resolve_run_state(AtmosphereEnvironmentRunState& state) {
    if (state.solar_time_enabled) {
        resolve_solar_sun(state.environment);
    }
    state.resolved_exposure = resolved_atmosphere_exposure(
        state.environment, state.auto_exposure_enabled, state.exposure_bias);
}

bool atmosphere_environment_advance_time(AtmosphereEnvironmentRunState& state,
                                         double delta_seconds) {
    if (!state.time_playing || state.time_speed_hours_per_second <= 0.0F || delta_seconds <= 0.0) {
        return false;
    }

    const double current_time_hours = static_cast<double>(state.environment.time_of_day.time_hours);
    const double next_time_hours =
        current_time_hours +
        (static_cast<double>(state.time_speed_hours_per_second) * delta_seconds);
    const int day_delta = static_cast<int>(std::floor(next_time_hours / 24.0));
    state.environment.time_of_day.time_hours =
        render::atmosphere_environment_wrap_time_hours(static_cast<float>(next_time_hours));
    if (day_delta != 0) {
        state.environment.time_of_day.day_of_year =
            render::atmosphere_environment_advance_day_of_year(
                state.environment.time_of_day.day_of_year, day_delta);
    }

    atmosphere_environment_resolve_run_state(state);

    return true;
}

} // namespace cubey
