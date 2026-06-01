#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_runtime.h>

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_atmosphere_environment_run_config_resolves_manual_and_solar_modes() {
    cubey::AtmosphereEnvironmentRunDefaults defaults{
        .sun_elevation_degrees = 20.0F,
        .sun_azimuth_degrees = -20.0F,
        .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnly,
        .reference_geometry_enabled = false,
    };

    cubey::RunConfig::AtmosphereOptions manual;
    manual.sun_elevation_degrees = 4.0F;
    manual.time_hours = 16.0F;
    cubey::AtmosphereEnvironmentRunState manual_state =
        cubey::atmosphere_environment_run_state_from_config(manual, defaults);
    require(!manual_state.solar_time_enabled,
            "manual sun options should disable implicit solar time");
    require_near(manual_state.environment.sun_elevation_degrees, 4.0F, 0.0001F,
                 "manual sun elevation should override caller defaults");
    require_near(manual_state.environment.sun_azimuth_degrees, -20.0F, 0.0001F,
                 "unset manual sun azimuth should preserve caller default");
    require(manual_state.environment.ground_mode ==
                cubey::render::AtmosphereEnvironmentGroundMode::SkyOnly,
            "shared atmosphere defaults should preserve caller ground mode");
    require(!manual_state.environment.reference_geometry_enabled,
            "shared atmosphere defaults should preserve caller reference geometry policy");

    cubey::RunConfig::AtmosphereOptions solar;
    solar.time_of_day_mode = "solar";
    solar.time_hours = 12.0F;
    solar.day_of_year = 80.0F;
    solar.latitude_degrees = 30.0F;
    cubey::AtmosphereEnvironmentRunState solar_state =
        cubey::atmosphere_environment_run_state_from_config(solar, defaults);
    require(solar_state.solar_time_enabled,
            "explicit solar mode should resolve sun direction from time of day");
    require_near(solar_state.environment.sun_elevation_degrees, 60.0F, 0.2F,
                 "solar mode should resolve equinox noon sun elevation");
    require_near(solar_state.environment.sun_azimuth_degrees, 0.0F, 0.2F,
                 "solar mode should resolve equinox noon sun azimuth");
}

void test_atmosphere_environment_run_config_advances_dynamic_time() {
    cubey::RunConfig::AtmosphereOptions atmosphere;
    atmosphere.time_of_day_mode = "solar";
    atmosphere.time_hours = 23.5F;
    atmosphere.day_of_year = 365.0F;
    atmosphere.latitude_degrees = 30.0F;
    atmosphere.time_speed_hours_per_second = 1.25F;

    cubey::AtmosphereEnvironmentRunState state =
        cubey::atmosphere_environment_run_state_from_config(atmosphere);
    require(state.time_playing, "positive time speed should enable atmosphere time playback");

    const bool advanced = cubey::atmosphere_environment_advance_time(state, 1.0);
    require(advanced, "atmosphere time helper should report playback advancement");
    require_near(state.environment.time_of_day.time_hours, 0.75F, 0.0001F,
                 "atmosphere time helper should wrap hours across midnight");
    require_near(state.environment.time_of_day.day_of_year, 366.0F, 0.0001F,
                 "atmosphere time helper should advance day of year across midnight");

    state.time_playing = false;
    const float previous_time = state.environment.time_of_day.time_hours;
    require(!cubey::atmosphere_environment_advance_time(state, 1.0),
            "paused atmosphere time helper should not advance");
    require_near(state.environment.time_of_day.time_hours, previous_time, 0.0001F,
                 "paused atmosphere time helper should preserve time");
}

void test_atmosphere_environment_runtime_derives_lighting_and_scene_environment() {
    cubey::render::AtmosphereEnvironmentRuntime runtime;
    cubey::render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = 35.0F;
    environment.sun_azimuth_degrees = 90.0F;

    runtime.set_environment(environment);

    const cubey::render::AtmosphereEnvironmentLighting& lighting = runtime.lighting();
    require(lighting.primary_light_intensity > 0.0F,
            "atmosphere runtime should derive primary light intensity");

    const cubey::scene::Environment3D scene_environment = runtime.scene_environment();
    require(scene_environment.diffuse_irradiance_sh_enabled,
            "atmosphere runtime scene environment should enable diffuse SH");
    require(scene_environment.diffuse_irradiance_sh[0] == lighting.diffuse_irradiance_sh[0],
            "atmosphere runtime scene environment should expose derived SH");
}

void test_atmosphere_environment_runtime_requires_resources_before_bindings() {
    cubey::render::AtmosphereEnvironmentRuntime runtime;

    bool threw = false;
    try {
        (void)runtime.reflection_probe();
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "atmosphere runtime should reject resource access before resources exist");
}
