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

void test_celestial_sun_derives_from_atmosphere() {
    cubey::render::AtmosphereEnvironmentConfig atmosphere{};
    atmosphere.sun_elevation_degrees = 22.0F;
    atmosphere.sun_azimuth_degrees = -35.0F;
    atmosphere.sun_angular_radius = 0.006F;

    const cubey::projects::planet::PlanetCelestialSystem celestial =
        cubey::projects::planet::planet_celestial_system_from_atmosphere(atmosphere);

    require(celestial.sun.visible, "derived sun should be visible by default");
    require_vec_near(celestial.sun.direction,
                     cubey::render::atmosphere_environment_sun_direction(atmosphere),
                     "derived sun should preserve atmosphere direction");
    require_near(celestial.sun.angular_radius_rad, atmosphere.sun_angular_radius, 0.000001F,
                 "derived sun should preserve atmosphere angular radius");
    require(celestial.sun.intensity > 0.0F, "derived sun should carry direct light intensity");
}

void test_atmosphere_inputs_round_trip_celestial_direction() {
    cubey::render::AtmosphereEnvironmentConfig atmosphere{};
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{0.30F, 0.40F, -0.85F});
    celestial.sun.angular_radius_rad = 0.010F;

    const cubey::render::AtmosphereEnvironmentConfig resolved =
        cubey::projects::planet::planet_atmosphere_inputs_from_celestial(atmosphere, celestial);

    require_vec_near(cubey::render::atmosphere_environment_sun_direction(resolved),
                     celestial.sun.direction,
                     "celestial sun direction should round-trip through atmosphere inputs");
    require_near(resolved.sun_angular_radius, celestial.sun.angular_radius_rad, 0.000001F,
                 "celestial sun radius should feed atmosphere inputs");
}

void test_celestial_lighting_uses_celestial_direction() {
    cubey::render::AtmosphereEnvironmentConfig atmosphere{};
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{-0.20F, 0.65F, -0.73F});

    const cubey::render::AtmosphereEnvironmentLighting lighting =
        cubey::projects::planet::planet_celestial_lighting(atmosphere, celestial);

    require_vec_near(lighting.sun_direction, celestial.sun.direction,
                     "celestial lighting should use modeled sun direction");
    require_vec_near(lighting.primary_light_direction, celestial.sun.direction,
                     "primary light should follow modeled sun direction");
}

void test_celestial_frame_uniforms_pack_sun_state() {
    cubey::projects::planet::PlanetCelestialSystem celestial{};
    celestial.sun.direction = glm::normalize(cubey::math::Vec3{0.15F, 0.85F, -0.50F});
    celestial.sun.color = {1.0F, 0.75F, 0.45F};
    celestial.sun.intensity = 1.8F;
    celestial.sun.angular_radius_rad = 0.012F;

    const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
        cubey::math::identity_quat(), 1.5F, 1.0F);
    const cubey::projects::planet::PlanetCelestialFrameUniforms uniforms =
        cubey::projects::planet::planet_celestial_frame_uniforms(
            celestial, {
                           .view_rays = view_rays,
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
}

void test_celestial_pass_uses_additive_blend() {
    const cubey::render::MaterialPassInfo pass =
        cubey::projects::planet::planet_celestial_pass_info();
    require(pass.blend_enable, "celestial pass should blend over atmosphere");
    require(pass.src_color_blend_factor == VK_BLEND_FACTOR_ONE &&
                pass.dst_color_blend_factor == VK_BLEND_FACTOR_ONE,
            "celestial pass should add HDR sun radiance");
    require(!pass.depth_test && !pass.depth_write,
            "celestial pass should not own planet depth occlusion");
}

} // namespace

int main() {
    try {
        test_celestial_sun_derives_from_atmosphere();
        test_atmosphere_inputs_round_trip_celestial_direction();
        test_celestial_lighting_uses_celestial_direction();
        test_celestial_frame_uniforms_pack_sun_state();
        test_celestial_pass_uses_additive_blend();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet_celestial_tests: %s\n", error.what());
        return 1;
    }
}
