#include <cmath>
#include <cubey/core/math.h>
#include <cubey/engine/terrain_surface_environment.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void close(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < .0001F, message);
}
void clocks_and_presets() {
    cubey::TerrainSurfaceEnvironment environment;
    const auto original = cubey::render::terrain_surface_patch_fixed_daylight();
    require(!environment.atmosphere().time_playing, "environment must start paused");
    close(environment.atmosphere().environment.sun_elevation_degrees,
          original.sun_elevation_degrees, "reference sun changed");
    environment.advance(2.0);
    require(environment.elapsed_seconds() == 2.0, "wall motion clock did not advance");
    close(environment.atmosphere().environment.sun_elevation_degrees,
          original.sun_elevation_degrees, "paused sun moved");
    environment.select_preset("noon");
    close(environment.atmosphere().environment.time_of_day.time_hours, 12.0F, "noon preset");
    environment.atmosphere().time_playing = true;
    environment.atmosphere().time_speed_hours_per_second = .5F;
    environment.advance(2.0);
    close(environment.atmosphere().environment.time_of_day.time_hours, 13.0F,
          "solar clock must use wall delta");
    environment.select_preset("night");
    require(environment.atmosphere().environment.sun_elevation_degrees < 0,
            "night sun above horizon");
    require(!environment.atmosphere().time_playing, "preset must pause cycle");
    environment.select_preset("daylight");
    close(environment.atmosphere().environment.sun_elevation_degrees,
          original.sun_elevation_degrees, "reference restore");
    bool rejected = false;
    try {
        environment.advance(std::numeric_limits<double>::quiet_NaN());
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "non-finite wall delta accepted");
    rejected = false;
    try {
        environment.select_preset("invalid");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "unknown preset accepted");
}
void camera_contract() {
    const auto projection = cubey::math::perspective(1.0F, 16.0F / 9.0F, 1.0F, 100000.0F);
    const cubey::math::Vec3 position{1234, 5678, -9012};
    const auto view = glm::lookAt(position, position + cubey::math::Vec3{.1F, -.4F, -1.0F},
                                  cubey::math::Vec3{0, 1, 0});
    const cubey::render::TerrainSurfacePatchCamera camera{projection * view, position};
    const auto rays = cubey::terrain_surface_environment_view_rays(camera);
    const auto inverse = glm::inverse(camera.view_projection);
    for (const auto uv :
         {cubey::math::Vec2{0, 0}, cubey::math::Vec2{.5F, -.5F}, cubey::math::Vec2{-.8F, .8F}}) {
        const auto p = inverse * cubey::math::Vec4{uv.x, uv.y, .5F, 1};
        const auto expected = glm::normalize(cubey::math::Vec3(p) / p.w - position);
        const auto actual = glm::normalize(cubey::math::Vec3(rays.forward) +
                                           cubey::math::Vec3(rays.right_aspect) * uv.x *
                                               rays.right_aspect.w * rays.up_tan_half_fovy.w -
                                           cubey::math::Vec3(rays.up_tan_half_fovy) * uv.y *
                                               rays.up_tan_half_fovy.w);
        require(glm::dot(actual, expected) > .999F,
                "atmosphere rays must match Vulkan camera including Y sign");
    }
}
} // namespace
int main() {
    try {
        clocks_and_presets();
        camera_contract();
        std::cout << "PASS: shared environment presets, independent clock and camera rays\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
