#include "planet_atmosphere_adapter.h"
#include "planet_celestial.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
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

void require_vec_near(cubey::math::Vec3 actual, cubey::math::Vec3 expected, const char* message) {
    require_near(actual.x, expected.x, 0.0001F, message);
    require_near(actual.y, expected.y, 0.0001F, message);
    require_near(actual.z, expected.z, 0.0001F, message);
}

float wrap_unit_delta(float actual, float expected) {
    float delta = std::fmod(actual - expected, 1.0F);
    if (delta < -0.5F) {
        delta += 1.0F;
    }
    if (delta > 0.5F) {
        delta -= 1.0F;
    }
    return delta;
}

float synodic_month_days(const cubey::projects::planet::PlanetSolarSystemConfig& solar) {
    return 1.0F / ((1.0F / solar.moon_orbit_period_days) - (1.0F / solar.planet_orbit_period_days));
}

float angle_between(cubey::math::Vec3 a, cubey::math::Vec3 b) {
    return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0F, 1.0F));
}

cubey::projects::planet::PlanetExposureView exposure_view(cubey::math::Quat rotation) {
    return {
        .view_rays =
            cubey::render::view_ray_basis_3d(rotation, 1.0F, std::numbers::pi_v<float> / 3.0F),
        .planet_radius_m = 1.0F,
    };
}

void test_default_solar_system_uses_earth_like_reference_periods() {
    const cubey::projects::planet::PlanetSolarSystemConfig solar{};

    require_near(solar.planet_rotation_period_days *
                     cubey::projects::planet::kPlanetMeanSolarDayHours,
                 23.9345F, 0.0001F, "default Earth spin should use a sidereal rotation period");
    require_near(solar.planet_orbit_period_days, 365.2422F, 0.0001F,
                 "default Earth orbit should use the tropical year for seasons");
    require_near(solar.moon_orbit_period_days, 27.321661F, 0.00001F,
                 "default moon orbit should use the sidereal lunar month");
    require_near(solar.moon_orbit_inclination_rad * 180.0F / std::numbers::pi_v<float>, 5.145F,
                 0.001F, "default moon orbit should include lunar inclination");
    require_near(solar.moon_orbit_phase_offset_cycles, 0.5980231F, 0.000001F,
                 "default moon phase epoch should keep the demo from starting near new moon");
}

void test_solar_time_applies_run_config() {
    cubey::RunConfig run_config{};
    run_config.planet.day_of_year = 81.0F;
    run_config.planet.time_hours = 15.25F;
    run_config.planet.time_speed_hours_per_second = 0.75F;

    cubey::projects::planet::PlanetSolarTime time =
        cubey::projects::planet::planet_solar_time_from_run_config(run_config);

    require_near(time.day_of_year, 81.0F, 0.0001F, "planet solar time should apply run config day");
    require_near(time.time_hours, 15.25F, 0.0001F,
                 "planet solar time should apply run config hour");
    require_near(time.hours_per_second, 0.75F, 0.0001F,
                 "planet solar time should apply run config speed");

    run_config.planet.time_paused = 1;
    time = cubey::projects::planet::planet_solar_time_from_run_config(run_config);
    require_near(time.hours_per_second, 0.0F, 0.0001F,
                 "planet pause flag should override configured time speed");
}

void test_solar_time_drives_planet_rotation_and_moon_orbit() {
    cubey::projects::planet::PlanetSolarTime morning{};
    morning.day_of_year = 80.0F;
    morning.time_hours = 6.0F;
    cubey::projects::planet::PlanetSolarTime evening = morning;
    evening.time_hours = 18.0F;

    const cubey::projects::planet::PlanetCelestialSystem morning_system =
        cubey::projects::planet::planet_celestial_system_from_solar_time(morning);
    const cubey::projects::planet::PlanetCelestialSystem evening_system =
        cubey::projects::planet::planet_celestial_system_from_solar_time(evening);

    require(glm::dot(morning_system.sun.direction, evening_system.sun.direction) < -0.95F,
            "planet self rotation should move the sun across the planet-fixed sky");
    require(morning_system.moon.angular_radius_rad > 0.0F,
            "solar-system model should include moon angular size");
    require(morning_system.moon.direction != evening_system.moon.direction,
            "moon orbit should advance with simulation time");
}

void test_sidereal_spin_produces_24_hour_solar_day() {
    cubey::projects::planet::PlanetSolarTime today{};
    today.day_of_year = 80.0F;
    today.time_hours = 12.0F;
    cubey::projects::planet::PlanetSolarTime tomorrow = today;
    tomorrow.day_of_year += 1.0F;

    const cubey::projects::planet::PlanetCelestialSystem today_system =
        cubey::projects::planet::planet_celestial_system_from_solar_time(today);
    const cubey::projects::planet::PlanetCelestialSystem tomorrow_system =
        cubey::projects::planet::planet_celestial_system_from_solar_time(tomorrow);

    require(glm::dot(today_system.sun.direction, tomorrow_system.sun.direction) > 0.99995F,
            "same mean solar time on adjacent days should keep the sun at nearly the same hour "
            "angle");

    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0F;
    const float today_solar_angle =
        (today_system.planet_rotation_angle_rad - today_system.planet_orbit_angle_rad) / kTwoPi;
    const float tomorrow_solar_angle =
        (tomorrow_system.planet_rotation_angle_rad - tomorrow_system.planet_orbit_angle_rad) /
        kTwoPi;
    require_near(wrap_unit_delta(tomorrow_solar_angle, today_solar_angle), 0.0F, 0.0002F,
                 "sidereal spin minus orbital motion should advance by one solar day");
}

void test_solar_time_advance_wraps_hours_and_days() {
    cubey::projects::planet::PlanetSolarTime time{};
    time.day_of_year = 365.0F;
    time.time_hours = 23.5F;
    time.hours_per_second = 2.0F;

    cubey::projects::planet::planet_solar_time_advance(time, 1800.0);

    require(time.time_hours >= 0.0F && time.time_hours < 24.0F,
            "solar clock should wrap local hours");
    require(time.day_of_year >= 1.0F && time.day_of_year <= 365.2422F,
            "solar clock should wrap day of year");
}

void test_moon_phase_uses_synodic_month() {
    const cubey::projects::planet::PlanetSolarSystemConfig solar{};
    const float synodic_days = cubey::projects::planet::planet_celestial_synodic_month_days(solar);

    require_near(synodic_days, synodic_month_days(solar), 0.0001F,
                 "public synodic month helper should match orbit-rate difference");

    cubey::projects::planet::PlanetSolarTime start{};
    start.day_of_year = 80.0F;
    start.time_hours = 12.0F;

    cubey::projects::planet::PlanetSolarTime quarter = start;
    quarter.day_of_year += synodic_days * 0.25F;
    cubey::projects::planet::PlanetSolarTime half = start;
    half.day_of_year += synodic_days * 0.5F;
    cubey::projects::planet::PlanetSolarTime three_quarter = start;
    three_quarter.day_of_year += synodic_days * 0.75F;
    cubey::projects::planet::PlanetSolarTime full_cycle = start;
    full_cycle.day_of_year += synodic_days;

    const float start_phase =
        cubey::projects::planet::planet_celestial_system_from_solar_time(start).moon.phase_fraction;
    const float quarter_phase =
        cubey::projects::planet::planet_celestial_system_from_solar_time(quarter)
            .moon.phase_fraction;
    const float half_phase =
        cubey::projects::planet::planet_celestial_system_from_solar_time(half).moon.phase_fraction;
    const float three_quarter_phase =
        cubey::projects::planet::planet_celestial_system_from_solar_time(three_quarter)
            .moon.phase_fraction;
    const float full_cycle_phase =
        cubey::projects::planet::planet_celestial_system_from_solar_time(full_cycle)
            .moon.phase_fraction;

    require_near(wrap_unit_delta(quarter_phase, start_phase + 0.25F), 0.0F, 0.0002F,
                 "moon phase should advance by a quarter over a quarter synodic month");
    require_near(wrap_unit_delta(half_phase, start_phase + 0.5F), 0.0F, 0.0002F,
                 "moon phase should advance by a half over a half synodic month");
    require_near(wrap_unit_delta(three_quarter_phase, start_phase + 0.75F), 0.0F, 0.0002F,
                 "moon phase should keep signed direction through the waning half");
    require_near(wrap_unit_delta(full_cycle_phase, start_phase), 0.0F, 0.0002F,
                 "moon phase should repeat over the synodic month");
}

void test_default_lunar_epoch_starts_away_from_sun() {
    cubey::projects::planet::PlanetSolarTime time{};
    time.day_of_year = 80.0F;
    time.time_hours = 5.5F;

    const cubey::projects::planet::PlanetCelestialSystem celestial =
        cubey::projects::planet::planet_celestial_system_from_solar_time(time);
    const float separation_degrees =
        angle_between(celestial.sun.direction, celestial.moon.direction) * 180.0F /
        std::numbers::pi_v<float>;

    require(separation_degrees > 150.0F,
            "default lunar epoch should not place the moon near the sun in the demo view");
    require_near(celestial.moon.phase_fraction, 0.5F, 0.0002F,
                 "default lunar epoch should start near full moon at the spring dawn preset");
}

void test_moon_phase_matches_sun_moon_separation_for_coplanar_orbits() {
    cubey::projects::planet::PlanetSolarSystemConfig solar{};
    solar.axial_tilt_rad = 0.0F;
    solar.moon_orbit_inclination_rad = 0.0F;
    solar.moon_orbit_phase_offset_cycles = 0.0F;
    solar.equinox_day = 0.0F;
    const float synodic_days = cubey::projects::planet::planet_celestial_synodic_month_days(solar);

    cubey::projects::planet::PlanetSolarTime new_time{};
    new_time.day_of_year = 1.0F;
    new_time.time_hours = 0.0F;
    const cubey::projects::planet::PlanetCelestialSystem new_moon =
        cubey::projects::planet::planet_celestial_system_from_solar_time(new_time, solar);

    cubey::projects::planet::PlanetSolarTime quarter_time = new_time;
    quarter_time.day_of_year += synodic_days * 0.25F;
    const cubey::projects::planet::PlanetCelestialSystem quarter_moon =
        cubey::projects::planet::planet_celestial_system_from_solar_time(quarter_time, solar);

    cubey::projects::planet::PlanetSolarTime full_time = new_time;
    full_time.day_of_year += synodic_days * 0.5F;
    const cubey::projects::planet::PlanetCelestialSystem full_moon =
        cubey::projects::planet::planet_celestial_system_from_solar_time(full_time, solar);

    require_near(angle_between(new_moon.sun.direction, new_moon.moon.direction), 0.0F, 0.0002F,
                 "new moon should align with the sun in the coplanar mean model");
    require_near(angle_between(quarter_moon.sun.direction, quarter_moon.moon.direction),
                 std::numbers::pi_v<float> * 0.5F, 0.0005F,
                 "quarter moon should sit about ninety degrees from the sun");
    require_near(angle_between(full_moon.sun.direction, full_moon.moon.direction),
                 std::numbers::pi_v<float>, 0.0005F, "full moon should sit opposite the sun");
}

void test_celestial_diagnostics_report_plane_relationships() {
    cubey::projects::planet::PlanetSolarTime time{};
    time.day_of_year = 160.0F;
    time.time_hours = 9.5F;
    const cubey::projects::planet::PlanetSolarSystemConfig solar{};

    const cubey::projects::planet::PlanetCelestialDiagnostics diagnostics =
        cubey::projects::planet::planet_celestial_diagnostics(time, solar);

    require_near(diagnostics.sidereal_rotation_hours, 23.9345F, 0.0001F,
                 "diagnostics should report sidereal spin in hours");
    require_near(diagnostics.lunar_synodic_month_days, 29.53068F, 0.0005F,
                 "diagnostics should report derived synodic month");
    require_near(angle_between(diagnostics.equator_plane_normal, diagnostics.ecliptic_plane_normal),
                 solar.axial_tilt_rad, 0.0001F,
                 "ecliptic normal should be tilted from the equator by axial tilt");
    require_near(
        angle_between(diagnostics.ecliptic_plane_normal, diagnostics.moon_orbit_plane_normal),
        solar.moon_orbit_inclination_rad, 0.0001F,
        "moon orbit normal should be tilted from the ecliptic by lunar inclination");
    require_vec_near(
        diagnostics.sun_direction,
        cubey::projects::planet::planet_celestial_system_from_solar_time(time, solar).sun.direction,
        "diagnostics should expose modeled sun direction");
}

void test_celestial_lighting_uses_celestial_direction() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{-0.20F, -0.65F, -0.73F});
    celestial.sun.intensity = 2.25F;
    celestial.moon.direction = glm::normalize(cubey::math::Vec3{0.12F, 0.34F, 0.93F});
    celestial.moon.phase_fraction = 0.5F;

    const cubey::projects::planet::PlanetCelestialLighting lighting =
        cubey::projects::planet::planet_celestial_lighting(celestial);

    require_vec_near(lighting.primary_light_direction, celestial.sun.direction,
                     "celestial lighting should use modeled sun direction");
    require(lighting.primary_light_intensity > 0.0F,
            "planet sun intensity should not depend on a global atmosphere horizon");
    require_vec_near(lighting.moon_light_direction, celestial.moon.direction,
                     "celestial lighting should expose modeled moonlight direction");
    require(lighting.moon_light_intensity > 0.0F,
            "full moon should contribute a small secondary light");
    require(lighting.ambient_intensity > 0.0F, "planet lighting should include local ambient");
}

void test_celestial_lighting_scales_moonlight_by_phase() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.moon.direction = glm::normalize(cubey::math::Vec3{0.10F, 0.30F, 0.95F});
    celestial.moon.phase_fraction = 0.0F;
    const cubey::projects::planet::PlanetCelestialLighting new_moon =
        cubey::projects::planet::planet_celestial_lighting(celestial);

    celestial.moon.phase_fraction = 0.25F;
    const cubey::projects::planet::PlanetCelestialLighting quarter_moon =
        cubey::projects::planet::planet_celestial_lighting(celestial);

    celestial.moon.phase_fraction = 0.5F;
    const cubey::projects::planet::PlanetCelestialLighting full_moon =
        cubey::projects::planet::planet_celestial_lighting(celestial);

    require_near(new_moon.moon_light_intensity, 0.0F, 0.000001F,
                 "new moon should not add secondary moonlight");
    require(quarter_moon.moon_light_intensity > new_moon.moon_light_intensity,
            "quarter moon should be brighter than new moon");
    require(full_moon.moon_light_intensity > quarter_moon.moon_light_intensity,
            "full moon should be brighter than quarter moon");
    require_near(quarter_moon.moon_light_intensity, full_moon.moon_light_intensity * 0.5F, 0.0001F,
                 "quarter moon should be roughly half the full-moon light");
}

void test_celestial_display_exposure_uses_local_sun_elevation() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = {0.0F, 1.0F, 0.0F};
    const cubey::projects::planet::PlanetExposureConfig exposure{};
    const cubey::math::DVec3 camera_position{0.0, 1.0, 0.0};

    const float daylight_exposure = cubey::projects::planet::planet_celestial_display_exposure(
        celestial, camera_position, exposure);

    celestial.sun.direction = {0.0F, -1.0F, 0.0F};
    const float night_exposure = cubey::projects::planet::planet_celestial_display_exposure(
        celestial, camera_position, exposure);

    require(daylight_exposure < -2.0F,
            "planet auto exposure should darken bright daylight relative to fixed exposure");
    require(night_exposure > 2.0F,
            "planet auto exposure should brighten full night enough for inspection");
    require(night_exposure > daylight_exposure,
            "planet auto exposure should brighten night relative to daylight");
}

void test_celestial_auto_exposure_interpolates_day_twilight_and_night() {
    cubey::projects::planet::PlanetExposureConfig exposure{};
    exposure.daylight_exposure = -3.0F;
    exposure.twilight_exposure = -0.5F;
    exposure.night_exposure = 2.5F;

    require_near(cubey::projects::planet::planet_celestial_auto_exposure(80.0F, exposure), -3.0F,
                 0.000001F, "high sun should use daylight exposure");
    const float horizon_exposure =
        cubey::projects::planet::planet_celestial_auto_exposure(0.0F, exposure);
    require(horizon_exposure < exposure.twilight_exposure,
            "horizon twilight should already lean toward daylight instead of over-brightening");
    require(horizon_exposure > exposure.daylight_exposure,
            "horizon twilight should remain between daylight and twilight exposure");
    require_near(cubey::projects::planet::planet_celestial_auto_exposure(-60.0F, exposure), 2.5F,
                 0.000001F, "deep night should use night exposure");
}

void test_celestial_orbit_exposure_uses_visible_disk_light_fraction() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = {0.0F, 1.0F, 0.0F};

    require_near(cubey::projects::planet::planet_celestial_visible_disk_light_fraction(
                     celestial, {0.0, 1.0, 0.0}),
                 1.0F, 0.000001F, "sun-facing orbit view should see a fully lit disk");
    require_near(cubey::projects::planet::planet_celestial_visible_disk_light_fraction(
                     celestial, {1.0, 0.0, 0.0}),
                 0.5F, 0.000001F, "side-on orbit view should see a half-lit disk");
    require_near(cubey::projects::planet::planet_celestial_visible_disk_light_fraction(
                     celestial, {0.0, -1.0, 0.0}),
                 0.0F, 0.000001F, "anti-sun orbit view should see a dark disk");
}

void test_celestial_view_light_fraction_samples_visible_planet() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    const cubey::math::DVec3 camera_position{0.0, 0.0, 2.0};
    const cubey::projects::planet::PlanetExposureView view =
        exposure_view(cubey::math::identity_quat());

    celestial.sun.direction = {0.0F, 0.0F, 1.0F};
    const float lit = cubey::projects::planet::planet_celestial_view_light_fraction(
        celestial, camera_position, view);

    celestial.sun.direction = {1.0F, 0.0F, 0.0F};
    const float side = cubey::projects::planet::planet_celestial_view_light_fraction(
        celestial, camera_position, view);

    celestial.sun.direction = {0.0F, 0.0F, -1.0F};
    const float dark = cubey::projects::planet::planet_celestial_view_light_fraction(
        celestial, camera_position, view);

    require(lit > 0.75F, "centered lit orbit view should estimate a bright planet disk");
    require(side > 0.10F && side < lit,
            "side-lit orbit view should estimate less light than a lit disk");
    require(dark < 0.05F, "backlit orbit view should estimate a dark planet disk");
}

void test_celestial_view_light_fraction_uses_camera_view_direction() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = {0.0F, 0.0F, 1.0F};
    const cubey::math::DVec3 camera_position{0.0, 0.0, 2.0};
    const cubey::projects::planet::PlanetExposureView centered =
        exposure_view(cubey::math::identity_quat());
    const cubey::projects::planet::PlanetExposureView off_center =
        exposure_view(cubey::math::angle_axis_quat(0.95F, {0.0F, 1.0F, 0.0F}));

    const float centered_light = cubey::projects::planet::planet_celestial_view_light_fraction(
        celestial, camera_position, centered);
    const float off_center_light = cubey::projects::planet::planet_celestial_view_light_fraction(
        celestial, camera_position, off_center);

    require(off_center_light < centered_light,
            "orbit light fraction should account for where the camera is looking");
}

void test_celestial_orbit_auto_exposure_compresses_surface_day_night_range() {
    cubey::projects::planet::PlanetExposureConfig exposure{};
    exposure.daylight_exposure = -3.0F;
    exposure.twilight_exposure = -1.0F;
    exposure.night_exposure = 3.0F;

    const float orbit_day =
        cubey::projects::planet::planet_celestial_orbit_auto_exposure(1.0F, exposure);
    const float orbit_half =
        cubey::projects::planet::planet_celestial_orbit_auto_exposure(0.5F, exposure);
    const float orbit_night =
        cubey::projects::planet::planet_celestial_orbit_auto_exposure(0.0F, exposure);
    const float surface_day =
        cubey::projects::planet::planet_celestial_auto_exposure(80.0F, exposure);
    const float surface_night =
        cubey::projects::planet::planet_celestial_auto_exposure(-80.0F, exposure);

    require(orbit_day < orbit_half, "lit orbit disk should use darker exposure than half phase");
    require(orbit_half < orbit_night, "half-lit orbit disk should be darker than dark phase");
    require((orbit_night - orbit_day) < (surface_night - surface_day),
            "orbit exposure should avoid the full surface day-night exposure swing");
    require(orbit_night < surface_night,
            "dark orbit view should not brighten as aggressively as surface night");
}

void test_celestial_display_exposure_blends_orbit_and_surface_references() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = {0.0F, -1.0F, 0.0F};
    const cubey::projects::planet::PlanetExposureConfig exposure{};
    const cubey::math::DVec3 camera_position{0.0, 1.0, 0.0};

    const float orbit_exposure = cubey::projects::planet::planet_celestial_display_exposure(
        celestial, camera_position, exposure, 0.0F);
    const float surface_exposure = cubey::projects::planet::planet_celestial_display_exposure(
        celestial, camera_position, exposure, 1.0F);
    const float blended_exposure = cubey::projects::planet::planet_celestial_display_exposure(
        celestial, camera_position, exposure, 0.5F);

    require(orbit_exposure < surface_exposure,
            "orbit exposure should be less aggressive than surface night exposure");
    require(blended_exposure > orbit_exposure && blended_exposure < surface_exposure,
            "transition camera exposure should blend orbit and surface references");
}

void test_celestial_display_exposure_respects_overrides() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = {0.0F, 1.0F, 0.0F};
    const cubey::math::DVec3 camera_position{0.0, 1.0, 0.0};

    cubey::RunConfig explicit_exposure{};
    explicit_exposure.pbr.exposure = -0.25F;
    explicit_exposure.pbr.exposure_explicit = true;
    const cubey::projects::planet::PlanetExposureConfig explicit_config =
        cubey::projects::planet::planet_exposure_config_from_run_config(explicit_exposure);
    require_near(cubey::projects::planet::planet_celestial_display_exposure(
                     celestial, camera_position, explicit_config),
                 -0.25F, 0.000001F, "explicit PBR exposure should override planet auto exposure");

    cubey::RunConfig disabled_auto{};
    disabled_auto.pbr.exposure = 0.35F;
    disabled_auto.atmosphere.auto_exposure = 0;
    const cubey::projects::planet::PlanetExposureConfig disabled_config =
        cubey::projects::planet::planet_exposure_config_from_run_config(disabled_auto);
    require_near(cubey::projects::planet::planet_celestial_display_exposure(
                     celestial, camera_position, disabled_config),
                 0.35F, 0.000001F, "--no-auto-exposure should keep fixed PBR exposure");

    cubey::RunConfig biased{};
    biased.atmosphere.exposure_bias = 0.5F;
    const cubey::projects::planet::PlanetExposureConfig default_config =
        cubey::projects::planet::planet_exposure_config_from_run_config(cubey::RunConfig{});
    const cubey::projects::planet::PlanetExposureConfig biased_config =
        cubey::projects::planet::planet_exposure_config_from_run_config(biased);
    const float default_exposure = cubey::projects::planet::planet_celestial_display_exposure(
        celestial, camera_position, default_config);
    const float biased_exposure = cubey::projects::planet::planet_celestial_display_exposure(
        celestial, camera_position, biased_config);
    require_near(biased_exposure, default_exposure + 0.5F, 0.000001F,
                 "planet exposure bias should offset automatic exposure");
}

void test_planet_atmosphere_inputs_follow_celestial_state() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{0.35F, 0.55F, -0.76F});
    celestial.sun.angular_radius_rad = 0.006F;
    celestial.moon.direction = glm::normalize(cubey::math::Vec3{-0.20F, 0.30F, 0.93F});
    celestial.moon.phase_fraction = 0.25F;
    const cubey::projects::planet::PlanetCelestialLighting lighting =
        cubey::projects::planet::planet_celestial_lighting(celestial);

    const cubey::projects::planet::PlanetAtmosphereInputs inputs =
        cubey::projects::planet::planet_atmosphere_inputs(
            celestial, lighting, cubey::math::DVec3{0.0, 610000.0, 0.0}, 600000.0F, 670000.0F);

    require_vec_near(inputs.sun_direction, celestial.sun.direction,
                     "planet atmosphere inputs should expose modeled sun direction");
    require_vec_near(inputs.moon_direction, celestial.moon.direction,
                     "planet atmosphere inputs should expose modeled moon direction");
    require_near(inputs.camera_altitude_m, 10000.0F, 0.01F,
                 "planet atmosphere inputs should derive camera altitude");
    require_near(inputs.sun_angular_radius_rad, celestial.sun.angular_radius_rad, 0.000001F,
                 "planet atmosphere inputs should preserve sun angular radius");
    require_near(inputs.moon_phase_fraction, 0.25F, 0.000001F,
                 "planet atmosphere inputs should preserve moon phase");
}

void test_planet_atmosphere_environment_config_round_trips_sun_direction() {
    cubey::projects::planet::PlanetAtmosphereInputs inputs{};
    inputs.planet_radius_m = 600000.0F;
    inputs.atmosphere_outer_radius_m = 670000.0F;
    inputs.camera_altitude_m = 1200.0F;
    inputs.sun_direction = glm::normalize(cubey::math::Vec3{0.42F, 0.35F, -0.84F});
    inputs.sun_angular_radius_rad = 0.006F;
    cubey::render::AtmosphereEnvironmentConfig look_config;
    look_config.rayleigh_density_scale = 1.15F;
    look_config.mie_density_scale = 0.75F;
    look_config.ozone_density_scale = 1.25F;
    look_config.night_sky.twilight_strength = 0.7F;

    const cubey::render::AtmosphereEnvironmentConfig config =
        cubey::projects::planet::planet_atmosphere_environment_config(inputs, look_config);
    const cubey::math::Vec3 shared_direction =
        cubey::render::atmosphere_environment_sun_direction(config);

    require_near(config.bottom_radius_km, 600.0F, 0.0001F,
                 "planet atmosphere adapter should convert planet radius to kilometers");
    require_near(config.top_radius_km, 670.0F, 0.0001F,
                 "planet atmosphere adapter should convert atmosphere radius to kilometers");
    require_near(config.camera_altitude_km, 1.2F, 0.0001F,
                 "planet atmosphere adapter should convert camera altitude to kilometers");
    require(glm::dot(shared_direction, inputs.sun_direction) > 0.9999F,
            "planet atmosphere adapter should preserve sun direction through shared config");
    require(config.render_celestial_content,
            "planet atmosphere adapter should let unified atmosphere own sky celestial rendering");
    require(config.render_sun_disk, "planet atmosphere adapter should let unified atmosphere draw sun");
    require(config.render_night_sky,
            "planet atmosphere adapter should let unified atmosphere draw night sky");
    require(!config.render_moon_disk,
            "planet atmosphere adapter should leave moon disk rendering to planet geometry");
    require_near(config.rayleigh_density_scale, 1.15F, 0.0001F,
                 "planet atmosphere adapter should preserve shared Rayleigh look scale");
    require_near(config.mie_density_scale, 0.75F, 0.0001F,
                 "planet atmosphere adapter should preserve shared Mie look scale");
    require_near(config.ozone_density_scale, 1.25F, 0.0001F,
                 "planet atmosphere adapter should preserve shared ozone look scale");
    require_near(config.night_sky.twilight_strength, 0.7F, 0.0001F,
                 "planet atmosphere adapter should preserve shared twilight look scale");
}

void test_planet_unified_atmosphere_frame_uses_local_tangent_up() {
    cubey::projects::planet::PlanetAtmosphereInputs inputs{};
    inputs.planet_radius_m = 600000.0F;
    inputs.atmosphere_outer_radius_m = 670000.0F;
    inputs.camera_position_m = {610000.0F, 0.0F, 0.0F};
    inputs.camera_altitude_m = 10000.0F;
    inputs.sun_direction = glm::normalize(cubey::math::Vec3{0.50F, 0.40F, -0.76F});
    inputs.sun_angular_radius_rad = 0.006F;
    inputs.moon_direction = glm::normalize(cubey::math::Vec3{0.10F, 0.30F, 0.95F});
    inputs.moon_angular_radius_rad = 0.004F;
    inputs.moon_phase_fraction = 0.33F;
    const cubey::render::ViewRayBasis3D view_rays{
        .right_aspect = {0.0F, 0.0F, 1.0F, 1.5F},
        .up_tan_half_fovy = {1.0F, 0.0F, 0.0F, 0.6F},
        .forward = {0.0F, -1.0F, 0.0F, 0.0F},
    };
    cubey::render::AtmosphereEnvironmentConfig look_config;
    look_config.rayleigh_density_scale = 1.2F;
    look_config.ozone_density_scale = 1.3F;

    const cubey::render::AtmosphereEnvironmentFrameUniforms uniforms =
        cubey::projects::planet::planet_unified_atmosphere_frame_uniforms(
            inputs, {.view_rays = view_rays, .look_config = look_config});

    require_vec_near(cubey::math::Vec3{uniforms.camera_up_tan_half_fovy}, {0.0F, 1.0F, 0.0F},
                     "unified atmosphere view up should become local planet up");
    require_vec_near(cubey::math::Vec3{uniforms.camera_right_aspect}, {1.0F, 0.0F, 0.0F},
                     "unified atmosphere view right should stay tangent to local horizon");
    require_vec_near(cubey::math::Vec3{uniforms.camera_forward_debug_view}, {0.0F, 0.0F, -1.0F},
                     "unified atmosphere view forward should preserve camera orientation");
    require_near(uniforms.camera_right_aspect.w, 1.5F, 0.000001F,
                 "unified atmosphere adapter should preserve aspect");
    require_near(uniforms.camera_up_tan_half_fovy.w, 0.6F, 0.000001F,
                 "unified atmosphere adapter should preserve tan half fovy");
    require_near(uniforms.sun_direction_radius.y, inputs.sun_direction.x, 0.0001F,
                 "unified atmosphere sun elevation should be relative to local planet up");
    require_near(uniforms.rayleigh.x,
                 look_config.rayleigh_scattering.x * look_config.rayleigh_density_scale, 0.0001F,
                 "unified atmosphere frame should pack shared Rayleigh look scale");
    require_near(uniforms.ozone.y,
                 look_config.ozone_absorption.y * look_config.ozone_density_scale, 0.0001F,
                 "unified atmosphere frame should pack shared ozone look scale");
}

void test_planet_unified_atmosphere_frame_splits_sky_and_moon_ownership() {
    cubey::projects::planet::PlanetAtmosphereInputs inputs{};
    inputs.planet_radius_m = 600000.0F;
    inputs.atmosphere_outer_radius_m = 670000.0F;
    inputs.camera_position_m = {0.0F, 610000.0F, 0.0F};
    inputs.camera_altitude_m = 10000.0F;
    inputs.sun_direction = glm::normalize(cubey::math::Vec3{0.30F, 0.60F, -0.74F});
    inputs.moon_direction = glm::normalize(cubey::math::Vec3{-0.20F, 0.40F, 0.89F});
    inputs.moon_angular_radius_rad = 0.0045F;
    inputs.moon_phase_fraction = 0.5F;

    const cubey::render::AtmosphereEnvironmentFrameUniforms uniforms =
        cubey::projects::planet::planet_unified_atmosphere_frame_uniforms(
            inputs, {
                        .view_rays = cubey::render::view_ray_basis_3d(
                            cubey::math::identity_quat(), 1.0F, std::numbers::pi_v<float> / 3.0F),
                        .render_view = cubey::render::AtmosphereEnvironmentRenderView::Mie,
                    });

    require_near(uniforms.radii_ground.x, 600.0F, 0.0001F,
                 "unified atmosphere adapter should convert planet radius to kilometers");
    require_near(uniforms.radii_ground.y, 670.0F, 0.0001F,
                 "unified atmosphere adapter should convert atmosphere radius to kilometers");
    require_near(uniforms.radii_ground.z, 10.0F, 0.0001F,
                 "unified atmosphere adapter should convert camera altitude to kilometers");
    require_near(uniforms.render_options.x, 2.0F, 0.000001F,
                 "unified atmosphere adapter should render sky-only without smooth ground occlusion");
    require_near(uniforms.render_options.z, 0.0F, 0.000001F,
                 "unified atmosphere adapter should not lower the sky occluder by default");
    require_near(uniforms.render_options.y, 1.0F, 0.000001F,
                 "unified atmosphere adapter should enable unified sky celestial content");
    require_near(uniforms.celestial_render_options.x, 1.0F, 0.000001F,
                 "unified atmosphere adapter should render the unified sky sun disk");
    require_near(uniforms.celestial_render_options.y, 1.0F, 0.000001F,
                 "unified atmosphere adapter should render unified night sky");
    require_near(uniforms.celestial_render_options.z, 0.0F, 0.000001F,
                 "unified atmosphere adapter should suppress inline moon disk rendering");
    require_near(uniforms.atmosphere_options.y, 0.0F, 0.000001F,
                 "unified atmosphere adapter should disable reference geometry");
    require_near(uniforms.moon_options.x, 1.0F, 0.000001F,
                 "unified atmosphere adapter should keep moon data for sky washout");
    require_near(uniforms.moon_options.w, 1.0F, 0.000001F,
                 "unified atmosphere adapter should derive moon illumination from planet phase");
    require_near(uniforms.moon_phase_options.y, 0.0F, 0.000001F,
                 "unified atmosphere adapter should pack moon phase sine");
    require_near(uniforms.camera_forward_debug_view.w, 2.0F, 0.000001F,
                 "unified atmosphere adapter should preserve debug render view");
}

void test_celestial_body_conversion_preserves_moon_state() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.moon.direction = glm::normalize(cubey::math::Vec3{0.20F, 0.30F, 0.93F});
    celestial.moon.color = {0.50F, 0.60F, 0.70F};
    celestial.moon.angular_radius_rad = 0.006F;
    celestial.moon.distance_m = 12.0F;
    celestial.moon.radius_m = 0.25F;
    celestial.moon.phase_fraction = 0.33F;

    const cubey::projects::planet::PlanetCelestialBody moon =
        cubey::projects::planet::planet_celestial_moon_body(celestial);

    require(moon.type == cubey::projects::planet::PlanetCelestialBodyType::Moon,
            "moon body conversion should tag the body type");
    require_vec_near(moon.direction, celestial.moon.direction,
                     "moon body conversion should preserve direction");
    require_near(moon.angular_radius_rad, celestial.moon.angular_radius_rad, 0.000001F,
                 "moon body conversion should preserve angular radius");
    require_near(moon.phase_fraction, celestial.moon.phase_fraction, 0.000001F,
                 "moon body conversion should preserve phase");
}

void test_celestial_body_render_placement_preserves_apparent_size() {
    cubey::projects::planet::PlanetCelestialBody moon{};
    moon.direction = glm::normalize(cubey::math::Vec3{0.0F, 0.25F, -1.0F});
    moon.angular_radius_rad = 0.004F;
    moon.distance_m = 1000.0F;

    const cubey::projects::planet::PlanetCelestialBodyRenderPlacement placement =
        cubey::projects::planet::planet_celestial_body_render_placement(
            moon, {
                      .camera_render_position_m = {10.0F, 20.0F, 30.0F},
                      .near_plane_m = 1.0F,
                      .far_plane_m = 1000.0F,
                      .angular_radius_scale = 4.0F,
                      .shell_distance_fraction = 0.5F,
                  });

    require(placement.visible, "visible celestial body should produce visible placement");
    require_near(placement.shell_distance_m, 500.0F, 0.001F,
                 "render placement should use configured shell fraction");
    require_near(placement.radius_render_m / placement.shell_distance_m,
                 std::tan(moon.angular_radius_rad * 4.0F), 0.000001F,
                 "render placement radius should preserve scaled angular size");
    require_vec_near(
        glm::normalize(placement.center_render_m - cubey::math::Vec3{10.0F, 20.0F, 30.0F}),
        moon.direction, "render placement should keep the body on its celestial ray");
}

void test_celestial_body_render_placement_uses_topocentric_ray() {
    cubey::projects::planet::PlanetCelestialBody moon{};
    moon.direction = {1.0F, 0.0F, 0.0F};
    moon.angular_radius_rad = 0.004F;
    moon.distance_m = 100.0F;

    const cubey::projects::planet::PlanetCelestialBodyRenderPlacement placement =
        cubey::projects::planet::planet_celestial_body_render_placement(
            moon, {
                      .camera_render_position_m = {0.0F, 0.0F, 0.0F},
                      .camera_world_position_m = {0.0, 0.0, 10.0},
                      .planet_center_world_position_m = {0.0, 0.0, 0.0},
                      .near_plane_m = 1.0F,
                      .far_plane_m = 1000.0F,
                      .angular_radius_scale = 1.0F,
                      .shell_distance_fraction = 0.5F,
                  });

    const cubey::math::Vec3 expected_direction =
        glm::normalize(cubey::math::Vec3{100.0F, 0.0F, -10.0F});
    require_vec_near(glm::normalize(placement.center_render_m), expected_direction,
                     "render placement should use the camera-to-physical-body ray");
}

void test_sky_frame_uniforms_pack_sun_state() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{0.15F, 0.85F, -0.50F});
    celestial.sun.color = {1.0F, 0.75F, 0.45F};
    celestial.sun.intensity = 1.8F;
    celestial.sun.angular_radius_rad = 0.012F;
    celestial.moon.direction = glm::normalize(cubey::math::Vec3{-0.50F, 0.10F, 0.86F});
    celestial.moon.angular_radius_rad = 0.008F;

    const cubey::render::ViewRayBasis3D view_rays =
        cubey::render::view_ray_basis_3d(cubey::math::identity_quat(), 1.5F, 1.0F);
    const cubey::projects::planet::PlanetSkyFrameUniforms uniforms =
        cubey::projects::planet::planet_sky_frame_uniforms(
            celestial,
            {
                .view_rays = view_rays,
                .camera_position_m = {0.0F, 0.0F, 10.0F},
                .planet_radius_m = 4.0F,
                .atmosphere_outer_radius_m = 5.0F,
                .atmosphere_mode = cubey::projects::planet::PlanetAtmosphereMode::Physical,
                .moon_angular_radius_scale = 3.0F,
            });

    require(uniforms.camera_right_aspect == view_rays.right_aspect,
            "celestial frame uniforms should pack view right/aspect");
    require(uniforms.camera_forward_enabled.w == 1.0F,
            "celestial frame uniforms should pack sun visibility");
    require_vec_near({uniforms.sun_direction_radius.x, uniforms.sun_direction_radius.y,
                      uniforms.sun_direction_radius.z},
                     celestial.sun.direction, "celestial frame uniforms should pack sun direction");
    require_near(uniforms.sun_direction_radius.w, celestial.sun.angular_radius_rad, 0.000001F,
                 "celestial frame uniforms should pack sun angular radius");
    require_vec_near({uniforms.moon_direction_radius.x, uniforms.moon_direction_radius.y,
                      uniforms.moon_direction_radius.z},
                     celestial.moon.direction,
                     "celestial frame uniforms should pack moon direction for star masking");
    require_near(uniforms.moon_direction_radius.w, celestial.moon.angular_radius_rad * 3.0F,
                 0.000001F, "celestial frame uniforms should pack scaled moon angular radius");
    require_near(uniforms.sun_color_intensity.w, celestial.sun.intensity, 0.000001F,
                 "celestial frame uniforms should pack sun intensity");
    require_near(uniforms.camera_position_radius.z, 10.0F, 0.000001F,
                 "celestial frame uniforms should pack camera position for occlusion");
    require_near(uniforms.camera_position_radius.w, 4.0F, 0.000001F,
                 "celestial frame uniforms should pack planet radius for occlusion");
    require_near(uniforms.background_space_limb.w, 5.0F, 0.000001F,
                 "celestial frame uniforms should pack atmosphere limb radius");
    require_near(uniforms.atmosphere_mode_options.x, 1.0F, 0.000001F,
                 "celestial frame uniforms should pack atmosphere preview mode");
    require(uniforms.night_options.x < 0.60F,
            "celestial frame uniforms should keep night-sky atlas defaults subtle");
    require_near(uniforms.night_options.w, 1.0F, 0.000001F,
                 "celestial frame uniforms should fully wash out night sky in daylight");
    require(uniforms.milky_way_options.x < 0.90F,
            "celestial frame uniforms should keep Milky Way defaults below inspection strength");
}

void test_sky_frame_uniforms_disable_invisible_moon_star_mask() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.moon.visible = false;
    celestial.moon.direction = glm::normalize(cubey::math::Vec3{-0.50F, 0.10F, 0.86F});
    celestial.moon.angular_radius_rad = 0.008F;

    const cubey::projects::planet::PlanetSkyFrameUniforms uniforms =
        cubey::projects::planet::planet_sky_frame_uniforms(
            celestial, {
                           .view_rays = cubey::render::view_ray_basis_3d(
                               cubey::math::identity_quat(), 1.5F, 1.0F),
                           .moon_angular_radius_scale = 3.0F,
                       });

    require_vec_near({uniforms.moon_direction_radius.x, uniforms.moon_direction_radius.y,
                      uniforms.moon_direction_radius.z},
                     celestial.moon.direction,
                     "invisible moon should still pack a valid direction for diagnostics");
    require_near(uniforms.moon_direction_radius.w, 0.0F, 0.000001F,
                 "invisible moon should disable sky star masking");
}

void test_sky_frame_uniforms_use_topocentric_moon_mask_direction() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.moon.direction = {0.0F, 0.0F, 1.0F};
    celestial.moon.distance_m = 10.0F;

    const cubey::projects::planet::PlanetSkyFrameUniforms uniforms =
        cubey::projects::planet::planet_sky_frame_uniforms(
            celestial, {
                           .camera_position_m = {2.0F, 0.0F, 0.0F},
                       });

    const cubey::math::Vec3 expected = glm::normalize(cubey::math::Vec3{-2.0F, 0.0F, 10.0F});
    require_vec_near({uniforms.moon_direction_radius.x, uniforms.moon_direction_radius.y,
                      uniforms.moon_direction_radius.z},
                     expected,
                     "moon star mask should match the topocentric body placement direction");
}

void test_sky_pass_writes_opaque_sky() {
    const cubey::render::MaterialPassInfo pass = cubey::projects::planet::planet_sky_pass_info();
    require(!pass.blend_enable, "sky pass should write the planet-owned sky");
    require(!pass.depth_test && !pass.depth_write,
            "sky pass should use analytic planet occlusion instead of depth");
}

void test_celestial_body_frame_uniforms_pack_render_placement() {
    cubey::projects::planet::PlanetCelestialBody moon{};
    moon.direction = glm::normalize(cubey::math::Vec3{0.0F, 0.0F, 1.0F});
    moon.color = {0.50F, 0.60F, 0.70F};
    moon.phase_fraction = 0.25F;

    cubey::projects::planet::PlanetCelestialBodyRenderPlacement placement{};
    placement.visible = true;
    placement.center_render_m = {10.0F, 20.0F, 30.0F};
    placement.radius_render_m = 4.0F;

    cubey::projects::planet::PlanetCelestialLighting lighting{};
    lighting.primary_light_direction = glm::normalize(cubey::math::Vec3{1.0F, 2.0F, 3.0F});
    lighting.primary_light_intensity = 0.75F;

    const cubey::projects::planet::PlanetCelestialBodyFrameUniforms uniforms =
        cubey::projects::planet::planet_celestial_body_frame_uniforms(
            moon, placement, lighting, cubey::math::Mat4{1.0F},
            {
                .camera_render_position_m = {1.0F, 2.0F, 3.0F},
            });

    require_near(uniforms.center_radius.x, placement.center_render_m.x, 0.000001F,
                 "body frame uniforms should pack render center x");
    require_near(uniforms.center_radius.y, placement.center_render_m.y, 0.000001F,
                 "body frame uniforms should pack render center y");
    require_near(uniforms.center_radius.z, placement.center_render_m.z, 0.000001F,
                 "body frame uniforms should pack render center z");
    require_near(uniforms.center_radius.w, placement.radius_render_m, 0.000001F,
                 "body frame uniforms should pack render radius");
    require_near(uniforms.camera_position_options.x, 1.0F, 0.000001F,
                 "body frame uniforms should pack render camera position");
    require(uniforms.camera_position_options.w > 0.0F,
            "body frame uniforms should enable procedural body detail");
    require_vec_near({uniforms.light_direction_intensity.x, uniforms.light_direction_intensity.y,
                      uniforms.light_direction_intensity.z},
                     lighting.primary_light_direction,
                     "body frame uniforms should pack normalized light direction");
    require_near(uniforms.light_direction_intensity.w, lighting.primary_light_intensity, 0.000001F,
                 "body frame uniforms should pack light intensity");
    require_near(uniforms.color_phase.x, moon.color.x, 0.000001F,
                 "body frame uniforms should pack body color");
    require_near(uniforms.color_phase.w, moon.phase_fraction, 0.000001F,
                 "body frame uniforms should pack body phase");
    require_near(uniforms.visibility_atmosphere.x, 0.0F, 0.000001F,
                 "body frame uniforms should default to no atmospheric sky visibility");
}

void test_celestial_body_frame_washes_out_daytime_moon_in_atmosphere() {
    cubey::projects::planet::PlanetCelestialBody moon{};
    moon.direction = glm::normalize(cubey::math::Vec3{1.0F, 1.0F, 0.0F});
    moon.color = {0.50F, 0.60F, 0.70F};

    cubey::projects::planet::PlanetCelestialBodyRenderPlacement placement{};
    placement.visible = true;
    placement.center_render_m = {10.0F, 20.0F, 30.0F};
    placement.radius_render_m = 4.0F;

    cubey::projects::planet::PlanetCelestialLighting lighting{};
    lighting.primary_light_direction = {0.0F, 1.0F, 0.0F};
    lighting.primary_light_intensity = 0.75F;

    constexpr float planet_radius = 600000.0F;
    constexpr float atmosphere_height = 70000.0F;
    const cubey::projects::planet::PlanetCelestialBodyAtmosphereInputs surface_atmosphere{
        .camera_position_m = {0.0F, planet_radius + 1000.0F, 0.0F},
        .planet_radius_m = planet_radius,
        .atmosphere_outer_radius_m = planet_radius + atmosphere_height,
    };
    const cubey::projects::planet::PlanetCelestialBodyFrameUniforms surface_uniforms =
        cubey::projects::planet::planet_celestial_body_frame_uniforms(
            moon, placement, lighting, cubey::math::Mat4{1.0F},
            {
                .atmosphere = surface_atmosphere,
            });

    require(surface_uniforms.visibility_atmosphere.x > 0.60F,
            "daytime moon should receive strong sky visibility in the lower atmosphere");
    require(surface_uniforms.visibility_atmosphere.x < 0.98F,
            "daytime moon sky visibility should keep some body contrast for shader tuning");

    cubey::projects::planet::PlanetCelestialBodyAtmosphereInputs space_atmosphere =
        surface_atmosphere;
    space_atmosphere.camera_position_m = {0.0F, planet_radius + (atmosphere_height * 2.0F), 0.0F};
    const cubey::projects::planet::PlanetCelestialBodyFrameUniforms space_uniforms =
        cubey::projects::planet::planet_celestial_body_frame_uniforms(
            moon, placement, lighting, cubey::math::Mat4{1.0F},
            {
                .atmosphere = space_atmosphere,
            });

    require_near(space_uniforms.visibility_atmosphere.x, 0.0F, 0.000001F,
                 "moon should keep full contrast above the atmosphere");
}

void test_celestial_body_frame_defers_planet_shadow_eclipse() {
    cubey::projects::planet::PlanetCelestialBody moon{};
    moon.type = cubey::projects::planet::PlanetCelestialBodyType::Moon;
    moon.direction = {0.0F, 0.0F, 1.0F};
    moon.angular_radius_rad = 0.004F;
    moon.distance_m = 1000.0F;

    cubey::projects::planet::PlanetCelestialBodyRenderPlacement placement{};
    placement.visible = true;
    placement.center_render_m = {0.0F, 0.0F, 100.0F};
    placement.radius_render_m = 1.0F;

    cubey::projects::planet::PlanetCelestialLighting lighting{};
    lighting.primary_light_direction = {0.0F, 0.0F, -1.0F};
    lighting.primary_light_angular_radius_rad = 0.01F;

    const cubey::projects::planet::PlanetCelestialBodyFrameUniforms centered_shadow =
        cubey::projects::planet::planet_celestial_body_frame_uniforms(
            moon, placement, lighting, cubey::math::Mat4{1.0F},
            {
                .atmosphere =
                    {
                        .planet_radius_m = 600.0F,
                    },
            });

    require_near(centered_shadow.visibility_atmosphere.y, 0.0F, 0.000001F,
                 "full moon should not imply an eclipse without node-aware shadow geometry");

    moon.direction = {1.0F, 0.0F, 0.0F};
    const cubey::projects::planet::PlanetCelestialBodyFrameUniforms off_shadow =
        cubey::projects::planet::planet_celestial_body_frame_uniforms(
            moon, placement, lighting, cubey::math::Mat4{1.0F},
            {
                .atmosphere =
                    {
                        .planet_radius_m = 600.0F,
                    },
            });

    require_near(off_shadow.visibility_atmosphere.y, 0.0F, 0.000001F,
                 "moon away from the anti-sun line should not receive planet shadow");
}

void test_celestial_body_pass_uses_depth_test_without_depth_write() {
    const cubey::render::MaterialPassInfo pass =
        cubey::projects::planet::planet_celestial_body_pass_info();

    require(pass.cull_mode == VK_CULL_MODE_BACK_BIT, "body pass should cull back faces");
    require(pass.depth_test, "body pass should depth-test against planet geometry");
    require(!pass.depth_write, "body pass should not overwrite scene depth");
    require(pass.blend_enable,
            "body pass should blend the moon so unlit phases disappear into the sky");
    require(pass.src_color_blend_factor == VK_BLEND_FACTOR_ONE,
            "body pass should use premultiplied source color");
    require(pass.dst_color_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "body pass should blend low-alpha moon phases into the smooth sky color");
    require(pass.src_alpha_blend_factor == VK_BLEND_FACTOR_ONE,
            "body pass should use premultiplied source alpha");
    require(pass.dst_alpha_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "body pass should keep destination alpha consistent with source-over blending");
}

} // namespace

int main() {
    try {
        test_default_solar_system_uses_earth_like_reference_periods();
        test_solar_time_applies_run_config();
        test_solar_time_drives_planet_rotation_and_moon_orbit();
        test_sidereal_spin_produces_24_hour_solar_day();
        test_solar_time_advance_wraps_hours_and_days();
        test_moon_phase_uses_synodic_month();
        test_default_lunar_epoch_starts_away_from_sun();
        test_moon_phase_matches_sun_moon_separation_for_coplanar_orbits();
        test_celestial_diagnostics_report_plane_relationships();
        test_celestial_lighting_uses_celestial_direction();
        test_celestial_lighting_scales_moonlight_by_phase();
        test_celestial_display_exposure_uses_local_sun_elevation();
        test_celestial_auto_exposure_interpolates_day_twilight_and_night();
        test_celestial_orbit_exposure_uses_visible_disk_light_fraction();
        test_celestial_view_light_fraction_samples_visible_planet();
        test_celestial_view_light_fraction_uses_camera_view_direction();
        test_celestial_orbit_auto_exposure_compresses_surface_day_night_range();
        test_celestial_display_exposure_blends_orbit_and_surface_references();
        test_celestial_display_exposure_respects_overrides();
        test_planet_atmosphere_inputs_follow_celestial_state();
        test_planet_atmosphere_environment_config_round_trips_sun_direction();
        test_planet_unified_atmosphere_frame_uses_local_tangent_up();
        test_planet_unified_atmosphere_frame_splits_sky_and_moon_ownership();
        test_celestial_body_conversion_preserves_moon_state();
        test_celestial_body_render_placement_preserves_apparent_size();
        test_celestial_body_render_placement_uses_topocentric_ray();
        test_sky_frame_uniforms_pack_sun_state();
        test_sky_frame_uniforms_disable_invisible_moon_star_mask();
        test_sky_frame_uniforms_use_topocentric_moon_mask_direction();
        test_sky_pass_writes_opaque_sky();
        test_celestial_body_frame_uniforms_pack_render_placement();
        test_celestial_body_frame_washes_out_daytime_moon_in_atmosphere();
        test_celestial_body_frame_defers_planet_shadow_eclipse();
        test_celestial_body_pass_uses_depth_test_without_depth_write();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_celestial_tests: %s\n", error.what());
        return 1;
    }
}
