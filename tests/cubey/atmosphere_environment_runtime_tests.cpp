#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_runtime.h>

#include "source_file_test_helpers.h"

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

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

    cubey::RunConfig::AtmosphereOptions default_options;
    cubey::AtmosphereEnvironmentRunState default_state =
        cubey::atmosphere_environment_run_state_from_config(default_options, defaults);
    require(default_state.solar_time_enabled,
            "default atmosphere run state should use the solar clock");
    require(default_state.time_playing, "default atmosphere run state should autoplay time");
    require_near(default_state.time_speed_hours_per_second, 0.5F, 0.0001F,
                 "default atmosphere time speed should be half a simulated hour per second");
    require_near(default_state.environment.time_of_day.time_hours, 5.5F, 0.0001F,
                 "default atmosphere run state should start just before dawn");
    require_near(default_state.environment.time_of_day.azimuth_offset_degrees, -10.0F, 0.0001F,
                 "default atmosphere run state should offset sunrise away from straight-on view");
    require(default_state.auto_exposure_enabled,
            "default atmosphere run state should enable auto exposure");

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
    require(manual_state.auto_exposure_enabled,
            "manual sun options should still default to auto exposure");

    cubey::RunConfig::AtmosphereOptions solar;
    solar.time_of_day_mode = "solar";
    solar.time_hours = 12.0F;
    solar.day_of_year = 80.0F;
    solar.latitude_degrees = 30.0F;
    solar.sun_azimuth_offset_degrees = 0.0F;
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

void test_atmosphere_environment_run_state_resolves_control_mutations() {
    cubey::RunConfig::AtmosphereOptions atmosphere;
    atmosphere.time_of_day_mode = "manual";
    atmosphere.sun_elevation_degrees = 10.0F;
    atmosphere.sun_azimuth_degrees = 15.0F;
    cubey::AtmosphereEnvironmentRunState state =
        cubey::atmosphere_environment_run_state_from_config(atmosphere);

    state.solar_time_enabled = true;
    state.auto_exposure_enabled = true;
    state.exposure_bias = 0.5F;
    state.environment.time_of_day.time_hours = 12.0F;
    state.environment.time_of_day.day_of_year = 80.0F;
    state.environment.time_of_day.latitude_degrees = 30.0F;
    state.environment.time_of_day.azimuth_offset_degrees = 0.0F;
    cubey::atmosphere_environment_resolve_run_state(state);

    require_near(state.environment.sun_elevation_degrees, 60.0F, 0.2F,
                 "shared resolver should update sun elevation after solar UI edits");
    require_near(state.environment.sun_azimuth_degrees, 0.0F, 0.2F,
                 "shared resolver should update sun azimuth after solar UI edits");
    require_near(state.resolved_exposure,
                 cubey::render::atmosphere_environment_auto_exposure(
                     state.environment.sun_elevation_degrees, state.exposure_bias),
                 0.0001F, "shared resolver should update auto exposure after UI edits");

    state.solar_time_enabled = false;
    state.auto_exposure_enabled = false;
    state.environment.sun_elevation_degrees = -8.0F;
    cubey::atmosphere_environment_resolve_run_state(state);

    require_near(state.environment.sun_elevation_degrees, -8.0F, 0.0001F,
                 "shared resolver should preserve manual sun when solar mode is disabled");
    require_near(state.resolved_exposure, 0.0F, 0.0001F,
                 "shared resolver should clear resolved exposure when auto exposure is disabled");
}

void test_atmosphere_environment_runtime_derives_lighting_and_scene_environment() {
    cubey::AtmosphereEnvironmentRuntime runtime;
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

void test_atmosphere_environment_runtime_reports_changed_environment() {
    cubey::AtmosphereEnvironmentRuntime runtime;
    cubey::render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = 25.0F;

    require(runtime.set_environment(environment),
            "atmosphere runtime should report the first environment assignment as changed");
    require(!runtime.set_environment(environment),
            "atmosphere runtime should not dirty unchanged environment assignments");

    environment.sun_elevation_degrees = 30.0F;
    require(runtime.set_environment(environment),
            "atmosphere runtime should report semantic environment edits as changed");
}

void test_atmosphere_environment_runtime_builds_frame_payload() {
    cubey::AtmosphereEnvironmentRuntime runtime;
    cubey::render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = 20.0F;
    environment.sun_azimuth_degrees = 45.0F;
    runtime.set_environment(environment);

    const cubey::AtmosphereEnvironmentRuntimeFrame frame = runtime.frame({
        .view_rays =
            cubey::render::ViewRayBasis3D{
                .right_aspect = {1.0F, 0.0F, 0.0F, 1.75F},
                .up_tan_half_fovy = {0.0F, 1.0F, 0.0F, 0.5F},
                .forward = {0.0F, 0.0F, -1.0F, 0.0F},
            },
        .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
    });

    require_near(frame.background.camera_right_aspect.w, 1.75F, 0.0001F,
                 "atmosphere runtime frame should pack caller view rays");
    require(frame.scene_environment.diffuse_irradiance_sh_enabled,
            "atmosphere runtime frame should carry scene environment lighting");
    require(frame.scene_environment.diffuse_irradiance_sh[0] ==
                frame.lighting.diffuse_irradiance_sh[0],
	            "atmosphere runtime frame should keep lighting and scene environment in sync");
}

void test_atmosphere_environment_runtime_builds_celestial_frame_payload() {
    cubey::AtmosphereEnvironmentRuntime runtime;
    cubey::render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = -20.0F;
    environment.sun_azimuth_degrees = 0.0F;
    runtime.set_environment(environment);

    cubey::render::CelestialSystem celestial;
    celestial.sun.direction = {0.0F, 1.0F, 0.0F};
    celestial.sun.angular_radius_rad = 0.012F;
    celestial.moon.direction = {1.0F, 0.0F, 0.0F};
    celestial.moon.phase_fraction = 0.5F;
    celestial.moon.angular_radius_rad = 0.020F;
    celestial.planet_rotation_angle_rad = 0.75F;

    const cubey::AtmosphereEnvironmentRuntimeFrame frame = runtime.frame_from_celestial({
        .view_rays =
            cubey::render::ViewRayBasis3D{
                .right_aspect = {1.0F, 0.0F, 0.0F, 1.75F},
                .up_tan_half_fovy = {0.0F, 1.0F, 0.0F, 0.5F},
                .forward = {0.0F, 0.0F, -1.0F, 0.0F},
            },
        .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
        .celestial = celestial,
    });

    require_near(frame.background.sun_direction_radius.y, 1.0F, 0.0001F,
                 "celestial runtime frame should override legacy sun direction");
    require_near(frame.background.sun_direction_radius.w, 0.012F, 0.0001F,
                 "celestial runtime frame should override legacy sun angular radius");
    require_near(frame.background.moon_direction_radius.x, 1.0F, 0.0001F,
                 "celestial runtime frame should override legacy moon direction");
    require_near(frame.background.moon_phase_options.x, 0.5F, 0.0001F,
                 "celestial runtime frame should preserve shared moon phase");
    require_near(frame.lighting.sun_direction.y, 1.0F, 0.0001F,
                 "celestial runtime frame lighting should use shared sun direction");
    require(frame.scene_environment.diffuse_irradiance_sh[0] == frame.lighting.diffuse_irradiance_sh[0],
            "celestial runtime frame should keep derived scene environment and lighting in sync");
}

void test_atmosphere_environment_runtime_requires_resources_before_bindings() {
    cubey::AtmosphereEnvironmentRuntime runtime;

    bool threw = false;
    try {
        (void)runtime.reflection_probe();
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "atmosphere runtime should reject resource access before resources exist");
}

void test_atmosphere_environment_runtime_queues_all_faces_after_environment_change() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string header = cubey::tests::read_source_file(
        source_root / "include/cubey/engine/atmosphere_environment_runtime.h");
    const std::string source = cubey::tests::read_source_file(
        source_root / "src/cubey/engine/atmosphere_environment_runtime.cpp");

    cubey::tests::require_contains(
        header, "bool set_environment",
        "atmosphere runtime should expose whether set_environment changed state");
    cubey::tests::require_contains(
        header, "environment_initialized_",
        "atmosphere runtime should track whether an environment has been assigned");
    cubey::tests::require_contains(
        header, "std::uint32_t pending_face_updates_",
        "atmosphere runtime should track queued incremental face updates");
    cubey::tests::require_contains(source, "return false",
                                   "atmosphere runtime should skip unchanged environment updates");
    cubey::tests::require_contains(
        source, "pending_face_updates_ = 6U",
        "atmosphere runtime should refresh every cube face after an environment change");
    cubey::tests::require_contains(
        source, "--pending_face_updates_",
        "atmosphere runtime should drain one queued face update per recording call");
    cubey::tests::require_not_contains(
        source, "time_dirty_",
        "atmosphere runtime should not collapse an environment change into one dirty face");
}
