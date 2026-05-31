#include <cubey/render/atmosphere_environment.h>

#include <cubey/core/math.h>

#include <cmath>
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

} // namespace

void test_atmosphere_environment_packs_frame_uniforms() {
    require(sizeof(cubey::render::AtmosphereEnvironmentFrameUniforms) == sizeof(float) * 60U,
            "atmosphere environment frame uniforms should keep the shader vec4 layout size");

    cubey::render::AtmosphereEnvironmentConfig config;
    config.sun_elevation_degrees = 30.0F;
    config.sun_azimuth_degrees = 90.0F;
    config.night_sky.camera_visual_mode = true;
    config.moon.enabled = false;
    config.moon.disk_intensity = 0.5F;
    config.time_of_day.latitude_degrees = 42.0F;

    const cubey::math::Vec3 sun = cubey::render::atmosphere_environment_sun_direction(config);
    require_near(sun.x, std::cos(cubey::render::atmosphere_environment_degrees_to_radians(30.0F)),
                 0.0001F, "atmosphere environment sun direction should resolve azimuth around Y");
    require_near(sun.y, 0.5F, 0.0001F,
                 "atmosphere environment sun direction should resolve elevation");
    require(std::abs(sun.z) < 0.0001F,
            "atmosphere environment sun direction should face the requested azimuth");

    const cubey::render::ViewRayBasis3D view_rays{
        .right_aspect = {1.0F, 0.0F, 0.0F, 1.5F},
        .up_tan_half_fovy = {0.0F, 1.0F, 0.0F, 0.75F},
        .forward = {0.0F, 0.0F, -1.0F, 0.0F},
    };
    const cubey::render::AtmosphereEnvironmentFrameUniforms uniforms =
        cubey::render::atmosphere_environment_frame_uniforms(
            config,
            {
                .view_rays = view_rays,
                .render_view = cubey::render::AtmosphereEnvironmentRenderView::Moon,
            });

    require(uniforms.camera_right_aspect == view_rays.right_aspect,
            "atmosphere environment should preserve packed view ray right/aspect");
    require(uniforms.camera_up_tan_half_fovy == view_rays.up_tan_half_fovy,
            "atmosphere environment should preserve packed view ray up/fovy");
    require_near(uniforms.camera_forward_debug_view.z, -1.0F, 0.0001F,
                 "atmosphere environment should preserve packed forward ray");
    require(uniforms.camera_forward_debug_view.w ==
                static_cast<float>(static_cast<std::uint32_t>(
                    cubey::render::AtmosphereEnvironmentRenderView::Moon)),
            "atmosphere environment should pack the debug render view");
    require(uniforms.moon_options.x == 0.0F,
            "atmosphere environment should pack the moon enable flag");
    require(uniforms.milky_way_options.w == 1.0F,
            "atmosphere environment should pack camera visual mode for Milky Way rendering");
    require(uniforms.celestial_options.z ==
                std::sin(cubey::render::atmosphere_environment_degrees_to_radians(
                    config.time_of_day.latitude_degrees)),
            "atmosphere environment should pack the observer latitude sine");
}
