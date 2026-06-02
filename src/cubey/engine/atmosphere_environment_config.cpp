#include <cubey/engine/atmosphere_environment_config.h>

#include <cmath>

namespace cubey {
namespace {

constexpr float kDefaultTimeSpeedHoursPerSecond = 1.0F;

[[nodiscard]] bool
atmosphere_options_have_manual_sun(const RunConfig::AtmosphereOptions& atmosphere) {
    return run_config_float_is_set(atmosphere.sun_elevation_degrees) ||
           run_config_float_is_set(atmosphere.sun_azimuth_degrees);
}

[[nodiscard]] float atmosphere_exposure_bias(const RunConfig::AtmosphereOptions& atmosphere) {
    return run_config_float_is_set(atmosphere.exposure_bias) ? atmosphere.exposure_bias : 0.0F;
}

[[nodiscard]] bool
atmosphere_auto_exposure_enabled(const RunConfig::AtmosphereOptions& atmosphere) {
    return atmosphere.auto_exposure < 0 || atmosphere.auto_exposure == 1;
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
                                   const RunConfig::AtmosphereOptions& atmosphere) {
    if (run_config_float_is_set(atmosphere.time_hours)) {
        environment.time_of_day.time_hours = atmosphere.time_hours;
    }
    if (run_config_float_is_set(atmosphere.day_of_year)) {
        environment.time_of_day.day_of_year = atmosphere.day_of_year;
    }
    if (run_config_float_is_set(atmosphere.latitude_degrees)) {
        environment.time_of_day.latitude_degrees = atmosphere.latitude_degrees;
    }
    if (run_config_float_is_set(atmosphere.sun_azimuth_offset_degrees)) {
        environment.time_of_day.azimuth_offset_degrees = atmosphere.sun_azimuth_offset_degrees;
    }
}

void apply_atmosphere_render_options(render::AtmosphereEnvironmentConfig& environment,
                                     const RunConfig::AtmosphereOptions& atmosphere) {
    if (run_config_float_is_set(atmosphere.camera_altitude_km)) {
        environment.camera_altitude_km = atmosphere.camera_altitude_km;
    }
    if (run_config_float_is_set(atmosphere.mie_scale)) {
        environment.mie_density_scale = atmosphere.mie_scale;
    }
    if (run_config_float_is_set(atmosphere.twilight_strength)) {
        environment.night_sky.twilight_strength = atmosphere.twilight_strength;
    }
    if (run_config_float_is_set(atmosphere.twilight_horizon_warmth)) {
        environment.night_sky.twilight_horizon_warmth = atmosphere.twilight_horizon_warmth;
    }
    if (run_config_float_is_set(atmosphere.star_intensity)) {
        environment.night_sky.star_intensity = atmosphere.star_intensity;
    }
    if (run_config_float_is_set(atmosphere.star_density)) {
        environment.night_sky.star_density = atmosphere.star_density;
    }
    if (run_config_float_is_set(atmosphere.milky_way_intensity)) {
        environment.night_sky.milky_way_intensity = atmosphere.milky_way_intensity;
    }
    if (run_config_float_is_set(atmosphere.milky_way_contrast)) {
        environment.night_sky.milky_way_contrast = atmosphere.milky_way_contrast;
    }
    if (run_config_float_is_set(atmosphere.light_pollution)) {
        environment.night_sky.light_pollution = atmosphere.light_pollution;
    }
    if (run_config_float_is_set(atmosphere.moonlight_intensity)) {
        environment.moon.moonlight_intensity = atmosphere.moonlight_intensity;
    }
    if (run_config_float_is_set(atmosphere.moon_intensity)) {
        environment.moon.disk_intensity = atmosphere.moon_intensity;
    }
    if (run_config_float_is_set(atmosphere.moon_phase_offset_days)) {
        environment.moon.phase_offset_days = atmosphere.moon_phase_offset_days;
    }
    if (run_config_float_is_set(atmosphere.moon_size_scale)) {
        environment.moon.angular_radius_scale = atmosphere.moon_size_scale;
    }
    if (atmosphere.moon >= 0) {
        environment.moon.enabled = atmosphere.moon == 1;
    }
}

void resolve_solar_sun(render::AtmosphereEnvironmentConfig& environment) {
    const render::AtmosphereEnvironmentSolarPosition solar =
        render::atmosphere_environment_solar_position(environment.time_of_day);
    environment.sun_elevation_degrees = solar.elevation_degrees;
    environment.sun_azimuth_degrees = solar.azimuth_degrees;
}

} // namespace

bool atmosphere_environment_run_config_uses_solar_time(
    const RunConfig::AtmosphereOptions& atmosphere) {
    if (atmosphere.time_of_day_mode == "solar") {
        return true;
    }
    if (atmosphere.time_of_day_mode == "manual") {
        return false;
    }
    if (atmosphere_options_have_manual_sun(atmosphere)) {
        return false;
    }
    return true;
}

float atmosphere_environment_run_config_time_speed(const RunConfig::AtmosphereOptions& atmosphere) {
    return run_config_float_is_set(atmosphere.time_speed_hours_per_second)
               ? atmosphere.time_speed_hours_per_second
               : kDefaultTimeSpeedHoursPerSecond;
}

bool atmosphere_environment_run_config_time_playing(
    const RunConfig::AtmosphereOptions& atmosphere) {
    return atmosphere.time_paused != 1 &&
           atmosphere_environment_run_config_time_speed(atmosphere) > 0.0F;
}

AtmosphereEnvironmentRunState
atmosphere_environment_run_state_from_config(const RunConfig::AtmosphereOptions& atmosphere,
                                             const AtmosphereEnvironmentRunDefaults& defaults) {
    render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = defaults.sun_elevation_degrees;
    environment.sun_azimuth_degrees = defaults.sun_azimuth_degrees;
    environment.ground_mode = defaults.ground_mode;
    environment.reference_geometry_enabled = defaults.reference_geometry_enabled;

    apply_atmosphere_time_options(environment, atmosphere);

    const bool solar_time_enabled = atmosphere_environment_run_config_uses_solar_time(atmosphere);
    if (solar_time_enabled) {
        resolve_solar_sun(environment);
    } else {
        if (run_config_float_is_set(atmosphere.sun_elevation_degrees)) {
            environment.sun_elevation_degrees = atmosphere.sun_elevation_degrees;
        }
        if (run_config_float_is_set(atmosphere.sun_azimuth_degrees)) {
            environment.sun_azimuth_degrees = atmosphere.sun_azimuth_degrees;
        }
    }

    apply_atmosphere_render_options(environment, atmosphere);
    const bool auto_exposure_enabled = atmosphere_auto_exposure_enabled(atmosphere);
    const float exposure_bias = atmosphere_exposure_bias(atmosphere);

    return {
        .environment = environment,
        .solar_time_enabled = solar_time_enabled,
        .time_playing = atmosphere_environment_run_config_time_playing(atmosphere),
        .time_speed_hours_per_second = atmosphere_environment_run_config_time_speed(atmosphere),
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
