#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/atmosphere_reflection_probe.h>

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
    require(sizeof(cubey::render::AtmosphereEnvironmentFrameUniforms) == sizeof(float) * 64U,
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
            config, {
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
    require(uniforms.render_options.x == 0.0F,
            "atmosphere environment should default to ground rendering");

    config.ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnly;
    const cubey::render::AtmosphereEnvironmentFrameUniforms sky_only_uniforms =
        cubey::render::atmosphere_environment_frame_uniforms(
            config, {
                        .view_rays = view_rays,
                        .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
                    });
    require(sky_only_uniforms.render_options.x == 1.0F,
            "atmosphere environment should pack sky-only ground policy");
}

void test_atmosphere_environment_resolves_celestial_time_math() {
    require_near(
        cubey::render::atmosphere_environment_radians_to_degrees(std::numbers::pi_v<float>), 180.0F,
        0.0001F, "atmosphere environment should convert radians to degrees");
    require_near(cubey::render::atmosphere_environment_wrap_time_hours(25.5F), 1.5F, 0.0001F,
                 "atmosphere environment should wrap time after midnight");
    require_near(cubey::render::atmosphere_environment_wrap_time_hours(-1.0F), 23.0F, 0.0001F,
                 "atmosphere environment should wrap negative time before midnight");
    require_near(cubey::render::atmosphere_environment_wrap_signed_degrees(190.0F), -170.0F,
                 0.0001F, "atmosphere environment should wrap azimuth into signed degrees");
    require_near(cubey::render::atmosphere_environment_advance_day_of_year(365.0F, 2), 1.0F,
                 0.0001F, "atmosphere environment should wrap positive day-of-year advancement");
    require_near(cubey::render::atmosphere_environment_wrap_unit(-0.25F), 0.75F, 0.0001F,
                 "atmosphere environment should wrap unit intervals");

    cubey::render::AtmosphereEnvironmentTimeOfDay solar_noon;
    solar_noon.time_hours = 12.0F;
    solar_noon.day_of_year = 80.0F;
    solar_noon.latitude_degrees = 30.0F;
    const cubey::render::AtmosphereEnvironmentSolarPosition position =
        cubey::render::atmosphere_environment_solar_position(solar_noon);
    require_near(position.elevation_degrees, 60.0F, 0.2F,
                 "atmosphere environment should resolve equinox noon solar elevation");
    require_near(position.azimuth_degrees, 0.0F, 0.2F,
                 "atmosphere environment should resolve equinox noon solar azimuth");

    const cubey::math::Vec3 east =
        cubey::render::atmosphere_environment_direction_from_alt_az(0.0F, 90.0F);
    require_near(east.x, 1.0F, 0.0001F,
                 "atmosphere environment should convert azimuth to direction X");
    require(std::abs(east.y) < 0.0001F && std::abs(east.z) < 0.0001F,
            "atmosphere environment should keep horizon east direction level");

    const cubey::render::AtmosphereEnvironmentMoon full_moon{
        .enabled = true,
        .disk_intensity = 1.0F,
        .moonlight_intensity = 1.0F,
        .phase_offset_days = 14.765294F,
        .angular_radius_scale = 2.0F,
    };
    cubey::render::AtmosphereEnvironmentTimeOfDay lunar_time = solar_noon;
    lunar_time.time_hours = 0.0F;
    const cubey::render::AtmosphereEnvironmentLunarState lunar =
        cubey::render::atmosphere_environment_lunar_state(lunar_time, full_moon);
    require_near(lunar.phase_fraction, 0.5F, 0.001F,
                 "atmosphere environment should resolve full moon phase fraction");
    require_near(lunar.illumination, 1.0F, 0.001F,
                 "atmosphere environment should resolve full moon illumination");
    require_near(lunar.angular_radius, 0.00452F * 2.0F, 0.000001F,
                 "atmosphere environment should scale lunar angular radius");

    require(cubey::render::atmosphere_environment_auto_exposure(2.0F, 0.0F) >
                cubey::render::atmosphere_environment_auto_exposure(60.0F, 0.0F),
            "atmosphere environment auto exposure should brighten low sun");
    require(cubey::render::atmosphere_environment_auto_exposure(-20.0F, 4.0F) <= 4.0F,
            "atmosphere environment auto exposure should stay clamped");
}

void test_atmosphere_environment_lighting_projects_diffuse_sh() {
    cubey::render::AtmosphereEnvironmentConfig day_config;
    day_config.sun_elevation_degrees = 55.0F;
    day_config.sun_azimuth_degrees = 20.0F;
    const cubey::render::AtmosphereEnvironmentLighting day_lighting =
        cubey::render::atmosphere_environment_lighting(day_config);

    cubey::render::AtmosphereEnvironmentConfig night = day_config;
    night.sun_elevation_degrees = -24.0F;
    const cubey::render::AtmosphereEnvironmentLighting night_lighting =
        cubey::render::atmosphere_environment_lighting(night);

    require(day_lighting.diffuse_irradiance_sh.size() == 9U,
            "atmosphere lighting should project L2 SH coefficients");
    for (const cubey::math::Vec3& coefficient : day_lighting.diffuse_irradiance_sh) {
        require(std::isfinite(coefficient.x) && std::isfinite(coefficient.y) &&
                    std::isfinite(coefficient.z),
                "atmosphere lighting SH coefficients should be finite");
    }

    const cubey::math::Vec3 day_up = cubey::render::atmosphere_environment_evaluate_sh(
        day_lighting.diffuse_irradiance_sh, {0.0F, 1.0F, 0.0F});
    const cubey::math::Vec3 day_down = cubey::render::atmosphere_environment_evaluate_sh(
        day_lighting.diffuse_irradiance_sh, {0.0F, -1.0F, 0.0F});
    const cubey::math::Vec3 night_up = cubey::render::atmosphere_environment_evaluate_sh(
        night_lighting.diffuse_irradiance_sh, {0.0F, 1.0F, 0.0F});

    require(day_lighting.sun_intensity > night_lighting.sun_intensity,
            "daylight atmosphere lighting should produce stronger sun intensity than night");
    require(day_up.y > night_up.y * 8.0F,
            "daylight atmosphere SH should be much brighter than night SH");
    require(day_up.y > day_down.y,
            "daylight atmosphere SH should light upward-facing normals more than ground-facing");
    require(day_lighting.primary_light_intensity == day_lighting.sun_intensity,
            "daylight primary atmosphere light should be the sun");
    require(day_lighting.ambient_color.y == day_up.y,
            "atmosphere ambient fallback should come from upward diffuse SH");
}

void test_atmosphere_background_pass_declares_frame_and_atlas_bindings() {
    const cubey::render::MaterialPassInfo pass = cubey::render::atmosphere_background_pass_info();
    require(pass.label == "atmosphere.fullscreen",
            "atmosphere background pass should keep the fullscreen label");
    require(pass.descriptor_sets.size() == 1U,
            "atmosphere background pass should use one descriptor set");
    require(pass.descriptor_sets[0].set == 0U,
            "atmosphere background pass should use descriptor set zero");
    require(pass.descriptor_sets[0].bindings.size() == 3U,
            "atmosphere background pass should bind frame uniforms and two atlases");
    require(
        pass.descriptor_sets[0].bindings[0].binding ==
            static_cast<std::uint32_t>(cubey::render::AtmosphereBackgroundBinding::FrameUniforms),
        "atmosphere background frame uniforms should use binding zero");
    require(pass.descriptor_sets[0].bindings[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "atmosphere background frame uniforms should use a uniform buffer");
    require(pass.descriptor_sets[0].bindings[1].binding ==
                static_cast<std::uint32_t>(cubey::render::AtmosphereBackgroundBinding::MoonAtlas),
            "atmosphere background moon atlas should use binding one");
    require(pass.descriptor_sets[0].bindings[1].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "atmosphere background moon atlas should be sampled");
    require(
        pass.descriptor_sets[0].bindings[2].binding ==
            static_cast<std::uint32_t>(cubey::render::AtmosphereBackgroundBinding::NightSkyAtlas),
        "atmosphere background night sky atlas should use binding two");
    require(pass.descriptor_sets[0].bindings[2].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "atmosphere background night sky atlas should be sampled");
    require(pass.push_constants.empty(),
            "atmosphere background pass should not use push constants");
}

void test_atmosphere_reflection_probe_declares_prefilter_and_irradiance_passes() {
    const cubey::render::MaterialPassInfo prefilter =
        cubey::render::atmosphere_reflection_prefilter_pass_info();
    require(prefilter.label == "atmosphere.reflection.prefilter",
            "atmosphere reflection prefilter pass should keep its label");
    require(prefilter.descriptor_sets.size() == 1U,
            "atmosphere reflection prefilter pass should declare one descriptor set");
    require(prefilter.descriptor_sets[0].bindings.size() == 2U,
            "atmosphere reflection prefilter pass should bind uniforms and sky radiance");

    const cubey::render::MaterialPassInfo irradiance =
        cubey::render::atmosphere_reflection_irradiance_pass_info();
    require(irradiance.label == "atmosphere.reflection.irradiance",
            "atmosphere reflection irradiance pass should keep its label");
    require(irradiance.descriptor_sets.size() == 1U,
            "atmosphere reflection irradiance pass should declare one descriptor set");
    require(irradiance.descriptor_sets[0].bindings.size() == 2U,
            "atmosphere reflection irradiance pass should bind uniforms and sky radiance");
}
