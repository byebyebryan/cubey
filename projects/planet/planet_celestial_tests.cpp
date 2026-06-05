#include "planet_celestial.h"

#include <cmath>
#include <cstdio>
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

void test_celestial_frame_uniforms_pack_sun_state() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{0.15F, 0.85F, -0.50F});
    celestial.sun.color = {1.0F, 0.75F, 0.45F};
    celestial.sun.intensity = 1.8F;
    celestial.sun.angular_radius_rad = 0.012F;
    celestial.moon.direction = glm::normalize(cubey::math::Vec3{-0.50F, 0.10F, 0.86F});
    celestial.moon.angular_radius_rad = 0.008F;

    const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
        cubey::math::identity_quat(), 1.5F, 1.0F);
    const cubey::projects::planet::PlanetCelestialFrameUniforms uniforms =
        cubey::projects::planet::planet_celestial_frame_uniforms(
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
                     celestial.sun.direction,
                     "celestial frame uniforms should pack sun direction");
    require_near(uniforms.sun_direction_radius.w, celestial.sun.angular_radius_rad, 0.000001F,
                 "celestial frame uniforms should pack sun angular radius");
    require_near(uniforms.sun_color_intensity.w, celestial.sun.intensity, 0.000001F,
                 "celestial frame uniforms should pack sun intensity");
    require_near(uniforms.camera_position_radius.z, 10.0F, 0.000001F,
                 "celestial frame uniforms should pack camera position for occlusion");
    require_near(uniforms.camera_position_radius.w, 4.0F, 0.000001F,
                 "celestial frame uniforms should pack planet radius for occlusion");
    require_near(uniforms.moon_direction_radius.w, celestial.moon.angular_radius_rad, 0.000001F,
                 "celestial frame uniforms should pack moon angular radius");
    require_near(uniforms.background_space_limb.w, 5.0F, 0.000001F,
                 "celestial frame uniforms should pack atmosphere limb radius");
}

void test_celestial_pass_writes_opaque_sky() {
    const cubey::render::MaterialPassInfo pass =
        cubey::projects::planet::planet_celestial_pass_info();
    require(!pass.blend_enable, "celestial pass should write the planet-owned sky");
    require(!pass.depth_test && !pass.depth_write,
            "celestial pass should use analytic planet occlusion instead of depth");
}

} // namespace

int main() {
    try {
        test_solar_time_drives_planet_rotation_and_moon_orbit();
        test_solar_time_advance_wraps_hours_and_days();
        test_celestial_lighting_uses_celestial_direction();
        test_celestial_frame_uniforms_pack_sun_state();
        test_celestial_pass_writes_opaque_sky();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_celestial_tests: %s\n", error.what());
        return 1;
    }
}
