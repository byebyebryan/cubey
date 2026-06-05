#include "planet_celestial.h"

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
    const float synodic_days = synodic_month_days(solar);

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

void test_celestial_lighting_uses_celestial_direction() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{-0.20F, -0.65F, -0.73F});
    celestial.sun.intensity = 2.25F;

    const cubey::projects::planet::PlanetCelestialLighting lighting =
        cubey::projects::planet::planet_celestial_lighting(celestial);

    require_vec_near(lighting.primary_light_direction, celestial.sun.direction,
                     "celestial lighting should use modeled sun direction");
    require(lighting.primary_light_intensity > 0.0F,
            "planet sun intensity should not depend on a global atmosphere horizon");
    require(lighting.ambient_intensity > 0.0F, "planet lighting should include local ambient");
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
            celestial, {
                           .view_rays = view_rays,
                           .camera_position_m = {0.0F, 0.0F, 10.0F},
                           .planet_radius_m = 4.0F,
                           .atmosphere_outer_radius_m = 5.0F,
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
    require_near(uniforms.sun_color_intensity.w, celestial.sun.intensity, 0.000001F,
                 "celestial frame uniforms should pack sun intensity");
    require_near(uniforms.camera_position_radius.z, 10.0F, 0.000001F,
                 "celestial frame uniforms should pack camera position for occlusion");
    require_near(uniforms.camera_position_radius.w, 4.0F, 0.000001F,
                 "celestial frame uniforms should pack planet radius for occlusion");
    require_near(uniforms.background_space_limb.w, 5.0F, 0.000001F,
                 "celestial frame uniforms should pack atmosphere limb radius");
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
        cubey::projects::planet::planet_celestial_body_frame_uniforms(moon, placement, lighting,
                                                                      cubey::math::Mat4{1.0F});

    require_near(uniforms.center_radius.x, placement.center_render_m.x, 0.000001F,
                 "body frame uniforms should pack render center x");
    require_near(uniforms.center_radius.y, placement.center_render_m.y, 0.000001F,
                 "body frame uniforms should pack render center y");
    require_near(uniforms.center_radius.z, placement.center_render_m.z, 0.000001F,
                 "body frame uniforms should pack render center z");
    require_near(uniforms.center_radius.w, placement.radius_render_m, 0.000001F,
                 "body frame uniforms should pack render radius");
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
}

void test_celestial_body_pass_uses_depth_test_without_depth_write() {
    const cubey::render::MaterialPassInfo pass =
        cubey::projects::planet::planet_celestial_body_pass_info();

    require(pass.cull_mode == VK_CULL_MODE_BACK_BIT, "body pass should cull back faces");
    require(pass.depth_test, "body pass should depth-test against planet geometry");
    require(!pass.depth_write, "body pass should not overwrite scene depth");
    require(!pass.blend_enable, "body pass should remain opaque");
}

} // namespace

int main() {
    try {
        test_default_solar_system_uses_earth_like_reference_periods();
        test_solar_time_drives_planet_rotation_and_moon_orbit();
        test_sidereal_spin_produces_24_hour_solar_day();
        test_solar_time_advance_wraps_hours_and_days();
        test_moon_phase_uses_synodic_month();
        test_celestial_lighting_uses_celestial_direction();
        test_celestial_body_conversion_preserves_moon_state();
        test_celestial_body_render_placement_preserves_apparent_size();
        test_sky_frame_uniforms_pack_sun_state();
        test_sky_pass_writes_opaque_sky();
        test_celestial_body_frame_uniforms_pack_render_placement();
        test_celestial_body_pass_uses_depth_test_without_depth_write();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_celestial_tests: %s\n", error.what());
        return 1;
    }
}
