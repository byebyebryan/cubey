#include "planet_celestial.h"

namespace cubey::projects::planet {

float planet_solar_time_simulation_day(const PlanetSolarTime& time) {
    return cubey::render::celestial_solar_time_simulation_day(time);
}

PlanetSolarTime planet_solar_time_from_run_config(const RunConfig& config) {
    PlanetSolarTime time{};
    if (run_config_float_is_set(config.planet.day_of_year)) {
        time.day_of_year = config.planet.day_of_year;
    }
    if (run_config_float_is_set(config.planet.time_hours)) {
        time.time_hours = config.planet.time_hours;
    }
    if (run_config_float_is_set(config.planet.time_speed_hours_per_second)) {
        time.hours_per_second = config.planet.time_speed_hours_per_second;
    }
    if (config.planet.time_paused >= 0 && config.planet.time_paused != 0) {
        time.hours_per_second = 0.0F;
    }
    return time;
}

PlanetExposureConfig planet_exposure_config_from_run_config(const RunConfig& config) {
    PlanetExposureConfig exposure{};
    exposure.manual_exposure = config.pbr.exposure;
    exposure.auto_exposure_enabled =
        config.atmosphere.auto_exposure < 0 || config.atmosphere.auto_exposure == 1;
    if (config.pbr.exposure_explicit) {
        exposure.auto_exposure_enabled = false;
    }

    const float bias = run_config_float_is_set(config.atmosphere.exposure_bias)
                           ? config.atmosphere.exposure_bias
                           : 0.0F;
    exposure.daylight_exposure += bias;
    exposure.twilight_exposure += bias;
    exposure.night_exposure += bias;
    return exposure;
}

void planet_solar_time_advance(PlanetSolarTime& time, double delta_seconds) {
    cubey::render::celestial_solar_time_advance(time, delta_seconds);
}

float planet_celestial_synodic_month_days(const PlanetSolarSystemConfig& solar) {
    return cubey::render::celestial_synodic_month_days(solar);
}

PlanetCelestialSystem
planet_celestial_system_from_solar_time(const PlanetSolarTime& time,
                                        const PlanetSolarSystemConfig& solar) {
    return cubey::render::celestial_system_from_solar_time(time, solar);
}

PlanetCelestialDiagnostics planet_celestial_diagnostics(const PlanetSolarTime& time,
                                                        const PlanetSolarSystemConfig& solar) {
    return cubey::render::celestial_diagnostics(time, solar);
}

PlanetCelestialBody planet_celestial_sun_body(const PlanetCelestialSystem& celestial) {
    return cubey::render::celestial_sun_body(celestial);
}

PlanetCelestialBody planet_celestial_moon_body(const PlanetCelestialSystem& celestial) {
    return cubey::render::celestial_moon_body(celestial);
}

PlanetCelestialBodyRenderPlacement
planet_celestial_body_render_placement(const PlanetCelestialBody& body,
                                       const PlanetCelestialBodyRenderPlacementInputs& inputs) {
    return cubey::render::celestial_body_render_placement(body, inputs);
}

PlanetCelestialLighting planet_celestial_lighting(const PlanetCelestialSystem& celestial) {
    return cubey::render::celestial_lighting(celestial);
}

float planet_celestial_sun_elevation_degrees(const PlanetCelestialSystem& celestial,
                                             cubey::math::DVec3 camera_world_position_m) {
    return cubey::render::celestial_sun_elevation_degrees(celestial, camera_world_position_m);
}

float planet_celestial_visible_disk_light_fraction(const PlanetCelestialSystem& celestial,
                                                   cubey::math::DVec3 camera_world_position_m) {
    return cubey::render::celestial_visible_disk_light_fraction(celestial, camera_world_position_m);
}

float planet_celestial_view_light_fraction(const PlanetCelestialSystem& celestial,
                                           cubey::math::DVec3 camera_world_position_m,
                                           const PlanetExposureView& view) {
    return cubey::render::celestial_view_light_fraction(celestial, camera_world_position_m, view);
}

float planet_celestial_display_exposure(const PlanetCelestialSystem& celestial,
                                        cubey::math::DVec3 camera_world_position_m,
                                        const PlanetExposureConfig& exposure) {
    return cubey::render::celestial_display_exposure(celestial, camera_world_position_m, exposure);
}

float planet_celestial_display_exposure(const PlanetCelestialSystem& celestial,
                                        cubey::math::DVec3 camera_world_position_m,
                                        const PlanetExposureConfig& exposure,
                                        float surface_reference_weight) {
    return cubey::render::celestial_display_exposure(celestial, camera_world_position_m, exposure,
                                                     surface_reference_weight);
}

float planet_celestial_display_exposure(const PlanetCelestialSystem& celestial,
                                        cubey::math::DVec3 camera_world_position_m,
                                        const PlanetExposureConfig& exposure,
                                        float surface_reference_weight,
                                        const PlanetExposureView& view) {
    return cubey::render::celestial_display_exposure(celestial, camera_world_position_m, exposure,
                                                     surface_reference_weight, view);
}

float planet_celestial_auto_exposure(float sun_elevation_degrees,
                                     const PlanetExposureConfig& exposure) {
    return cubey::render::celestial_auto_exposure(sun_elevation_degrees, exposure);
}

float planet_celestial_orbit_auto_exposure(float visible_disk_light_fraction,
                                           const PlanetExposureConfig& exposure) {
    return cubey::render::celestial_orbit_auto_exposure(visible_disk_light_fraction, exposure);
}

PlanetAtmosphereInputs planet_atmosphere_inputs(const PlanetCelestialSystem& celestial,
                                                const PlanetCelestialLighting& lighting,
                                                cubey::math::DVec3 camera_world_position_m,
                                                float planet_radius_m,
                                                float atmosphere_outer_radius_m) {
    return cubey::render::celestial_atmosphere_inputs(
        celestial, lighting, camera_world_position_m, planet_radius_m, atmosphere_outer_radius_m);
}

PlanetCelestialBodyFrameUniforms planet_celestial_body_frame_uniforms(
    const PlanetCelestialBody& body, const PlanetCelestialBodyRenderPlacement& placement,
    const PlanetCelestialLighting& lighting, const cubey::math::Mat4& view_projection,
    const PlanetCelestialBodyFrameInputs& inputs) {
    return cubey::render::celestial_body_frame_uniforms(body, placement, lighting, view_projection,
                                                       inputs);
}

cubey::render::MaterialPassInfo planet_celestial_body_pass_info() {
    return cubey::render::celestial_body_pass_info();
}

} // namespace cubey::projects::planet
