#include "planet_celestial.h"

#include <cubey/render/pass.h>
#include <cubey/render/primitive_mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace cubey::projects::planet {
namespace {

enum class PlanetCelestialBinding : std::uint32_t {
    FrameUniforms = 0,
};

[[nodiscard]] constexpr std::uint32_t binding(PlanetCelestialBinding binding) noexcept {
    return static_cast<std::uint32_t>(binding);
}

[[nodiscard]] cubey::math::Vec3 normalized_or_up(cubey::math::Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(direction);
}

[[nodiscard]] bool finite_positive(float value) {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] float wrap_unit(float value) {
    float wrapped = std::fmod(value, 1.0F);
    if (wrapped < 0.0F) {
        wrapped += 1.0F;
    }
    return wrapped;
}

[[nodiscard]] cubey::math::Vec3 rotate_y(cubey::math::Vec3 value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {
        (value.x * c) + (value.z * s),
        value.y,
        (-value.x * s) + (value.z * c),
    };
}

[[nodiscard]] cubey::math::Vec3 rotate_x(cubey::math::Vec3 value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {
        value.x,
        (value.y * c) - (value.z * s),
        (value.y * s) + (value.z * c),
    };
}

[[nodiscard]] float angular_radius(float radius_m, float distance_m) {
    return std::asin(std::clamp(radius_m / std::max(distance_m, 1.0F), -1.0F, 1.0F));
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] float moon_atmosphere_visibility_alpha(
    const PlanetCelestialBody& body, const PlanetCelestialLighting& lighting,
    const PlanetCelestialBodyAtmosphereInputs& atmosphere) {
    if (!finite_positive(atmosphere.planet_radius_m) ||
        atmosphere.atmosphere_outer_radius_m <= atmosphere.planet_radius_m ||
        glm::dot(atmosphere.camera_position_m, atmosphere.camera_position_m) <= 0.0F) {
        return 1.0F;
    }

    const cubey::math::Vec3 camera_up = normalized_or_up(atmosphere.camera_position_m);
    const float atmosphere_height =
        std::max(atmosphere.atmosphere_outer_radius_m - atmosphere.planet_radius_m, 1.0F);
    const float camera_altitude =
        std::max(glm::length(atmosphere.camera_position_m) - atmosphere.planet_radius_m, 0.0F);
    const float inside_atmosphere =
        1.0F - smoothstep(0.75F, 1.20F, camera_altitude / atmosphere_height);
    if (inside_atmosphere <= 0.0F) {
        return 1.0F;
    }

    const cubey::math::Vec3 sun_direction = normalized_or_up(lighting.primary_light_direction);
    const cubey::math::Vec3 moon_direction = normalized_or_up(body.direction);
    const float sun_elevation = glm::dot(sun_direction, camera_up);
    const float moon_elevation = glm::dot(moon_direction, camera_up);
    const float daylight = smoothstep(-0.06F, 0.25F, sun_elevation);
    const float above_horizon = smoothstep(-0.03F, 0.10F, moon_elevation);
    const float separation = std::acos(std::clamp(glm::dot(sun_direction, moon_direction),
                                                  -1.0F, 1.0F));
    const float near_sun = 1.0F - smoothstep(0.18F, 1.05F, separation);
    const float daytime_washout = inside_atmosphere * daylight * above_horizon;
    const float daytime_alpha = std::clamp(0.28F - near_sun * 0.22F, 0.04F, 0.28F);
    return std::clamp(1.0F + (daytime_alpha - 1.0F) * daytime_washout, 0.0F, 1.0F);
}

[[nodiscard]] float moon_eclipse_shadow_fraction(const PlanetCelestialBody& body,
                                                 const PlanetCelestialLighting& lighting,
                                                 float planet_radius_m) {
    if (body.type != PlanetCelestialBodyType::Moon || !finite_positive(planet_radius_m) ||
        !finite_positive(body.distance_m)) {
        return 0.0F;
    }

    const cubey::math::Vec3 moon_direction = normalized_or_up(body.direction);
    const cubey::math::Vec3 anti_sun_direction =
        -normalized_or_up(lighting.primary_light_direction);
    const float separation = std::acos(
        std::clamp(glm::dot(moon_direction, anti_sun_direction), -1.0F, 1.0F));
    const float planet_angular_radius =
        angular_radius(planet_radius_m, std::max(body.distance_m, planet_radius_m + 1.0F));
    const float sun_angular_radius = std::max(lighting.primary_light_angular_radius_rad, 0.0001F);
    const float moon_angular_radius = std::max(body.angular_radius_rad, 0.0001F);
    const float full_shadow_radius = std::max(planet_angular_radius - sun_angular_radius, 0.0F);
    const float partial_shadow_radius =
        std::max(full_shadow_radius + 0.0001F,
                 planet_angular_radius + sun_angular_radius + moon_angular_radius);
    const float centered_shadow =
        1.0F - smoothstep(full_shadow_radius, partial_shadow_radius, separation);
    const float area_cap = std::clamp((planet_angular_radius * planet_angular_radius) /
                                          (sun_angular_radius * sun_angular_radius),
                                      0.0F, 1.0F);
    const float max_shadow = 0.15F + area_cap * 0.78F;
    return std::clamp(centered_shadow * max_shadow, 0.0F, 0.93F);
}

} // namespace

float planet_solar_time_simulation_day(const PlanetSolarTime& time) {
    return std::max(time.day_of_year - 1.0F, 0.0F) +
           std::clamp(time.time_hours, 0.0F, 24.0F) / 24.0F;
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

void planet_solar_time_advance(PlanetSolarTime& time, double delta_seconds) {
    if (time.hours_per_second == 0.0F || delta_seconds <= 0.0) {
        return;
    }
    const double advanced_hours = static_cast<double>(time.time_hours) +
                                  delta_seconds * static_cast<double>(time.hours_per_second);
    const double day_offset = std::floor(advanced_hours / 24.0);
    double wrapped_hours = std::fmod(advanced_hours, 24.0);
    if (wrapped_hours < 0.0) {
        wrapped_hours += 24.0;
    }
    time.time_hours = static_cast<float>(wrapped_hours);
    time.day_of_year += static_cast<float>(day_offset);
    while (time.day_of_year > kPlanetMeanTropicalYearDays) {
        time.day_of_year -= kPlanetMeanTropicalYearDays;
    }
    while (time.day_of_year < 1.0F) {
        time.day_of_year += kPlanetMeanTropicalYearDays;
    }
}

float planet_celestial_synodic_month_days(const PlanetSolarSystemConfig& solar) {
    const float lunar_rate = 1.0F / std::max(solar.moon_orbit_period_days, 0.0001F);
    const float earth_orbit_rate = 1.0F / std::max(solar.planet_orbit_period_days, 0.0001F);
    const float synodic_rate = std::max(lunar_rate - earth_orbit_rate, 0.0001F);
    return 1.0F / synodic_rate;
}

PlanetCelestialSystem
planet_celestial_system_from_solar_time(const PlanetSolarTime& time,
                                        const PlanetSolarSystemConfig& solar) {
    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0F;
    const float simulation_day = planet_solar_time_simulation_day(time);
    const float rotation_angle =
        kTwoPi * simulation_day / std::max(solar.planet_rotation_period_days, 0.0001F);
    const float orbit_angle = kTwoPi * (simulation_day - solar.equinox_day) /
                              std::max(solar.planet_orbit_period_days, 0.0001F);
    const float moon_orbit_angle =
        kTwoPi * (simulation_day / std::max(solar.moon_orbit_period_days, 0.0001F) +
                  solar.moon_orbit_phase_offset_cycles);

    const cubey::math::Vec3 sun_in_orbital_frame =
        normalized_or_up({std::cos(orbit_angle), 0.0F, -std::sin(orbit_angle)});
    const cubey::math::Vec3 sun_in_tilted_frame =
        normalized_or_up(rotate_x(sun_in_orbital_frame, solar.axial_tilt_rad));
    const cubey::math::Vec3 sun_in_planet_frame =
        normalized_or_up(rotate_y(sun_in_tilted_frame, -rotation_angle));

    const float moon_inclination = std::clamp(solar.moon_orbit_inclination_rad, -1.4F, 1.4F);
    const cubey::math::Vec3 moon_orbit_frame = normalized_or_up({
        std::cos(moon_orbit_angle),
        std::sin(moon_orbit_angle) * std::sin(moon_inclination),
        -std::sin(moon_orbit_angle) * std::cos(moon_inclination),
    });
    const cubey::math::Vec3 moon_in_tilted_frame =
        normalized_or_up(rotate_x(moon_orbit_frame, solar.axial_tilt_rad));
    const cubey::math::Vec3 moon_in_planet_frame =
        normalized_or_up(rotate_y(moon_in_tilted_frame, -rotation_angle));
    const float phase_fraction = wrap_unit((moon_orbit_angle - orbit_angle) / kTwoPi);

    return {
        .sun =
            {
                .visible = true,
                .direction = sun_in_planet_frame,
                .color = {1.0F, 0.94F, 0.82F},
                .intensity = 2.25F,
                .angular_radius_rad = angular_radius(solar.sun_radius_m, solar.sun_distance_m),
                .distance_m = solar.sun_distance_m,
                .radius_m = solar.sun_radius_m,
            },
        .moon =
            {
                .visible = true,
                .direction = moon_in_planet_frame,
                .color = {0.58F, 0.62F, 0.74F},
                .intensity = 0.0F,
                .angular_radius_rad = angular_radius(solar.moon_radius_m, solar.moon_distance_m),
                .distance_m = solar.moon_distance_m,
                .radius_m = solar.moon_radius_m,
                .phase_fraction = phase_fraction,
            },
        .simulation_day = simulation_day,
        .planet_rotation_angle_rad = rotation_angle,
        .planet_orbit_angle_rad = orbit_angle,
        .moon_orbit_angle_rad = moon_orbit_angle,
    };
}

PlanetCelestialDiagnostics planet_celestial_diagnostics(const PlanetSolarTime& time,
                                                        const PlanetSolarSystemConfig& solar) {
    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0F;
    const float simulation_day = planet_solar_time_simulation_day(time);
    const float rotation_angle =
        kTwoPi * simulation_day / std::max(solar.planet_rotation_period_days, 0.0001F);
    const float moon_inclination = std::clamp(solar.moon_orbit_inclination_rad, -1.4F, 1.4F);
    const cubey::math::Vec3 ecliptic_normal_orbital_frame{0.0F, 1.0F, 0.0F};
    const cubey::math::Vec3 moon_plane_normal_orbital_frame =
        normalized_or_up({0.0F, std::cos(moon_inclination), std::sin(moon_inclination)});
    const cubey::math::Vec3 ecliptic_normal_tilted =
        normalized_or_up(rotate_x(ecliptic_normal_orbital_frame, solar.axial_tilt_rad));
    const cubey::math::Vec3 moon_plane_normal_tilted =
        normalized_or_up(rotate_x(moon_plane_normal_orbital_frame, solar.axial_tilt_rad));
    const PlanetCelestialSystem celestial = planet_celestial_system_from_solar_time(time, solar);

    return {
        .mean_solar_day_hours = kPlanetMeanSolarDayHours,
        .sidereal_rotation_hours = solar.planet_rotation_period_days * kPlanetMeanSolarDayHours,
        .tropical_year_days = solar.planet_orbit_period_days,
        .lunar_sidereal_month_days = solar.moon_orbit_period_days,
        .lunar_synodic_month_days = planet_celestial_synodic_month_days(solar),
        .axial_tilt_rad = solar.axial_tilt_rad,
        .lunar_orbit_inclination_rad = moon_inclination,
        .moon_phase_fraction = celestial.moon.phase_fraction,
        .equator_plane_normal = {0.0F, 1.0F, 0.0F},
        .ecliptic_plane_normal =
            normalized_or_up(rotate_y(ecliptic_normal_tilted, -rotation_angle)),
        .moon_orbit_plane_normal =
            normalized_or_up(rotate_y(moon_plane_normal_tilted, -rotation_angle)),
        .sun_direction = celestial.sun.direction,
        .moon_direction = celestial.moon.direction,
    };
}

PlanetCelestialBody planet_celestial_sun_body(const PlanetCelestialSystem& celestial) {
    return {
        .type = PlanetCelestialBodyType::Sun,
        .visible = celestial.sun.visible,
        .direction = normalized_or_up(celestial.sun.direction),
        .color = celestial.sun.color,
        .intensity = celestial.sun.intensity,
        .angular_radius_rad = celestial.sun.angular_radius_rad,
        .distance_m = celestial.sun.distance_m,
        .radius_m = celestial.sun.radius_m,
        .phase_fraction = 1.0F,
    };
}

PlanetCelestialBody planet_celestial_moon_body(const PlanetCelestialSystem& celestial) {
    return {
        .type = PlanetCelestialBodyType::Moon,
        .visible = celestial.moon.visible,
        .direction = normalized_or_up(celestial.moon.direction),
        .color = celestial.moon.color,
        .intensity = celestial.moon.intensity,
        .angular_radius_rad = celestial.moon.angular_radius_rad,
        .distance_m = celestial.moon.distance_m,
        .radius_m = celestial.moon.radius_m,
        .phase_fraction = celestial.moon.phase_fraction,
    };
}

PlanetCelestialBodyRenderPlacement
planet_celestial_body_render_placement(const PlanetCelestialBody& body,
                                       const PlanetCelestialBodyRenderPlacementInputs& inputs) {
    const float near_plane = std::max(inputs.near_plane_m, 0.001F);
    const float far_plane = std::max(inputs.far_plane_m, near_plane + 1.0F);
    const float shell_distance =
        std::clamp(far_plane * std::clamp(inputs.shell_distance_fraction, 0.10F, 0.90F),
                   near_plane * 4.0F, far_plane * 0.90F);
    const float scaled_angular_radius = std::clamp(
        body.angular_radius_rad * std::max(inputs.angular_radius_scale, 0.0F), 0.00001F, 0.35F);
    const float render_radius = std::tan(scaled_angular_radius) * shell_distance;
    const bool visible =
        body.visible && finite_positive(body.angular_radius_rad) && finite_positive(render_radius);
    const cubey::math::Vec3 body_direction = normalized_or_up(body.direction);
    const cubey::math::DVec3 body_center_world =
        inputs.planet_center_world_position_m +
        cubey::math::DVec3{static_cast<double>(body_direction.x),
                           static_cast<double>(body_direction.y),
                           static_cast<double>(body_direction.z)} *
            static_cast<double>(std::max(body.distance_m, 1.0F));
    const cubey::math::DVec3 camera_to_body = body_center_world - inputs.camera_world_position_m;
    cubey::math::Vec3 render_direction = body_direction;
    if (glm::length(camera_to_body) > 0.000001) {
        render_direction = normalized_or_up({static_cast<float>(camera_to_body.x),
                                             static_cast<float>(camera_to_body.y),
                                             static_cast<float>(camera_to_body.z)});
    }

    return {
        .visible = visible,
        .center_render_m = inputs.camera_render_position_m + render_direction * shell_distance,
        .radius_render_m = visible ? render_radius : 0.0F,
        .shell_distance_m = shell_distance,
        .angular_radius_rad = scaled_angular_radius,
    };
}

PlanetCelestialLighting planet_celestial_lighting(const PlanetCelestialSystem& celestial) {
    return {
        .primary_light_direction = normalized_or_up(celestial.sun.direction),
        .primary_light_color = celestial.sun.color,
        .primary_light_intensity = 0.88F,
        .primary_light_angular_radius_rad = celestial.sun.angular_radius_rad,
        .ambient_color = {0.040F, 0.050F, 0.070F},
        .ambient_intensity = 0.12F,
        .haze_color = {0.085F, 0.125F, 0.185F},
    };
}

PlanetAtmosphereInputs planet_atmosphere_inputs(const PlanetCelestialSystem& celestial,
                                                const PlanetCelestialLighting& lighting,
                                                cubey::math::DVec3 camera_world_position_m,
                                                float planet_radius_m,
                                                float atmosphere_outer_radius_m) {
    const float camera_radius = static_cast<float>(glm::length(camera_world_position_m));
    return {
        .camera_position_m =
            {
                static_cast<float>(camera_world_position_m.x),
                static_cast<float>(camera_world_position_m.y),
                static_cast<float>(camera_world_position_m.z),
            },
        .camera_radius_m = camera_radius,
        .camera_altitude_m = std::max(camera_radius - planet_radius_m, 0.0F),
        .planet_radius_m = planet_radius_m,
        .atmosphere_outer_radius_m = std::max(atmosphere_outer_radius_m, planet_radius_m),
        .sun_direction = normalized_or_up(celestial.sun.direction),
        .sun_color = lighting.primary_light_color,
        .sun_intensity = lighting.primary_light_intensity,
        .sun_angular_radius_rad = celestial.sun.angular_radius_rad,
        .moon_direction = normalized_or_up(celestial.moon.direction),
        .moon_phase_fraction = celestial.moon.phase_fraction,
        .moon_angular_radius_rad = celestial.moon.angular_radius_rad,
    };
}

cubey::render::AtmosphereEnvironmentConfig
planet_atmosphere_environment_config(const PlanetAtmosphereInputs& inputs) {
    constexpr float kMetersToKm = 0.001F;
    constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
    const cubey::math::Vec3 sun_direction = normalized_or_up(inputs.sun_direction);
    const float elevation_degrees =
        std::asin(std::clamp(sun_direction.y, -1.0F, 1.0F)) * kRadiansToDegrees;
    const float azimuth_degrees =
        std::atan2(sun_direction.x, -sun_direction.z) * kRadiansToDegrees;

    cubey::render::AtmosphereEnvironmentConfig config{};
    config.bottom_radius_km = std::max(inputs.planet_radius_m * kMetersToKm, 0.001F);
    config.top_radius_km =
        std::max(inputs.atmosphere_outer_radius_m * kMetersToKm, config.bottom_radius_km);
    config.camera_altitude_km = std::max(inputs.camera_altitude_m * kMetersToKm, 0.0F);
    config.sun_elevation_degrees = elevation_degrees;
    config.sun_azimuth_degrees =
        cubey::render::atmosphere_environment_wrap_signed_degrees(azimuth_degrees);
    config.sun_angular_radius = inputs.sun_angular_radius_rad;
    config.render_celestial_content = false;
    config.reference_geometry_enabled = false;
    config.moon.enabled = false;
    return config;
}

PlanetSkyFrameUniforms planet_sky_frame_uniforms(const PlanetCelestialSystem& celestial,
                                                 const PlanetSkyFrameUniformInputs& inputs) {
    const cubey::math::Vec3 sun_direction = normalized_or_up(celestial.sun.direction);
    return {
        .camera_right_aspect = inputs.view_rays.right_aspect,
        .camera_up_tan_half_fovy = inputs.view_rays.up_tan_half_fovy,
        .camera_forward_enabled =
            {
                inputs.view_rays.forward.x,
                inputs.view_rays.forward.y,
                inputs.view_rays.forward.z,
                celestial.sun.visible ? 1.0F : 0.0F,
            },
        .sun_direction_radius =
            {
                sun_direction.x,
                sun_direction.y,
                sun_direction.z,
                celestial.sun.angular_radius_rad,
            },
        .sun_color_intensity =
            {
                celestial.sun.color.r,
                celestial.sun.color.g,
                celestial.sun.color.b,
                celestial.sun.intensity,
            },
        .sun_disk_glow =
            {
                18.0F,
                2.8F,
                0.22F,
                0.035F,
            },
        .camera_position_radius =
            {
                inputs.camera_position_m.x,
                inputs.camera_position_m.y,
                inputs.camera_position_m.z,
                inputs.planet_radius_m,
            },
        .background_space_limb =
            {
                0.012F,
                0.022F,
                0.040F,
                std::max(inputs.atmosphere_outer_radius_m, inputs.planet_radius_m),
            },
        .atmosphere_mode_options =
            {
                static_cast<float>(static_cast<std::uint32_t>(inputs.atmosphere_mode)),
                0.0F,
                0.0F,
                0.0F,
            },
    };
}

cubey::render::MaterialPassInfo planet_sky_pass_info() {
    return {
        .label = "planet.sky",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(PlanetCelestialBinding::FrameUniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .blend_enable = false,
    };
}

PlanetCelestialBodyFrameUniforms planet_celestial_body_frame_uniforms(
    const PlanetCelestialBody& body, const PlanetCelestialBodyRenderPlacement& placement,
    const PlanetCelestialLighting& lighting, const cubey::math::Mat4& view_projection,
    const PlanetCelestialBodyFrameInputs& inputs) {
    const cubey::math::Vec3 light_direction = normalized_or_up(lighting.primary_light_direction);
    const float visibility_alpha =
        moon_atmosphere_visibility_alpha(body, lighting, inputs.atmosphere);
    const float eclipse_shadow =
        moon_eclipse_shadow_fraction(body, lighting, inputs.atmosphere.planet_radius_m);
    return {
        .view_projection = view_projection,
        .center_radius =
            {
                placement.center_render_m.x,
                placement.center_render_m.y,
                placement.center_render_m.z,
                placement.visible ? placement.radius_render_m : 0.0F,
            },
        .camera_position_options =
            {
                inputs.camera_render_position_m.x,
                inputs.camera_render_position_m.y,
                inputs.camera_render_position_m.z,
                0.42F,
            },
        .light_direction_intensity =
            {
                light_direction.x,
                light_direction.y,
                light_direction.z,
                lighting.primary_light_intensity,
            },
        .color_phase =
            {
                body.color.r,
                body.color.g,
                body.color.b,
                body.phase_fraction,
            },
        .visibility_atmosphere =
            {
                visibility_alpha,
                eclipse_shadow,
                0.32F,
                0.0F,
            },
    };
}

cubey::render::MaterialPassInfo planet_celestial_body_pass_info() {
    return {
        .label = "planet.celestial_body",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(PlanetCelestialBinding::FrameUniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = true,
        .depth_write = false,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
}

void PlanetSkyFrame::create_materials(const cubey::vulkan::Device& device,
                                      const PlanetSkyFrameMaterialConfig& config) {
    material_.emplace(device, cubey::render::FrameUniformMaterialInstanceConfig{
                                  .material_pass = planet_sky_pass_info(),
                                  .descriptor_set = 0,
                                  .frame_slot_count = config.frame_slot_count,
                                  .uniform_binding = binding(PlanetCelestialBinding::FrameUniforms),
                              });
}

void PlanetSkyFrame::create_pipeline(const cubey::vulkan::Device& device,
                                     const PlanetSkyFramePipelineConfig& config) {
    const std::array descriptor_set_layouts{material().layout()};
    pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                  .extent = config.extent,
                                  .color_format = config.color_format,
                                  .shader_stage_files = config.shader_stage_files,
                                  .descriptor_set_layouts = descriptor_set_layouts,
                                  .material_pass = planet_sky_pass_info(),
                              });
}

void PlanetSkyFrame::destroy_pipeline() {
    pipeline_.reset();
}

void PlanetSkyFrame::destroy() {
    destroy_pipeline();
    material_.reset();
}

void PlanetSkyFrame::upload(cubey::render::FrameSlot frame_slot,
                            const PlanetSkyFrameUniforms& uniforms) const {
    material().upload(frame_slot, uniforms);
}

void PlanetSkyFrame::record_pass(const cubey::vulkan::CommandRecorder& recorder,
                                 cubey::render::ColorTargetView target,
                                 cubey::render::FrameSlot frame_slot) const {
    const cubey::render::RenderTargetRenderingInfo rendering(
        cubey::render::render_target_view(target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        cubey::render::RenderTargetAttachmentOps{
            .color = cubey::vulkan::clear_store_attachment_ops(),
        });
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(target.extent);
    cubey::render::record_fullscreen_pipeline_draw(recorder,
                                                   {
                                                       .pipeline = &pipeline(),
                                                       .descriptor_set = material().set(frame_slot),
                                                   });
    recorder.end_rendering();
}

bool PlanetSkyFrame::materials_created() const noexcept {
    return material_.has_value();
}

const cubey::render::FrameUniformMaterialInstance<PlanetSkyFrameUniforms>&
PlanetSkyFrame::material() const {
    if (!material_.has_value()) {
        throw std::runtime_error("planet celestial material is not initialized");
    }
    return material_.value();
}

const cubey::render::GraphicsPipelineResource& PlanetSkyFrame::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("planet celestial pipeline is not initialized");
    }
    return pipeline_.value();
}

void PlanetCelestialBodyFrame::create_materials(
    const cubey::vulkan::Device& device, const PlanetCelestialBodyFrameMaterialConfig& config) {
    material_.emplace(device, cubey::render::FrameUniformMaterialInstanceConfig{
                                  .material_pass = planet_celestial_body_pass_info(),
                                  .descriptor_set = 0,
                                  .frame_slot_count = config.frame_slot_count,
                                  .uniform_binding = binding(PlanetCelestialBinding::FrameUniforms),
                              });
}

void PlanetCelestialBodyFrame::create_pipeline(
    const cubey::vulkan::Device& device, const PlanetCelestialBodyFramePipelineConfig& config) {
    const cubey::render::VertexInputLayout vertex_input =
        cubey::render::vertex_position_color_normal_uv_input_layout();
    const std::array descriptor_set_layouts{material().layout()};
    pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                  .extent = config.extent,
                                  .color_format = config.color_format,
                                  .depth_format = config.depth_format,
                                  .shader_stage_files = config.shader_stage_files,
                                  .vertex_bindings = vertex_input.bindings(),
                                  .vertex_attributes = vertex_input.attribute_descriptions(),
                                  .descriptor_set_layouts = descriptor_set_layouts,
                                  .material_pass = planet_celestial_body_pass_info(),
                              });
}

void PlanetCelestialBodyFrame::destroy_pipeline() {
    pipeline_.reset();
}

void PlanetCelestialBodyFrame::destroy() {
    destroy_pipeline();
    material_.reset();
}

void PlanetCelestialBodyFrame::upload(cubey::render::FrameSlot frame_slot,
                                      const PlanetCelestialBodyFrameUniforms& uniforms) const {
    material().upload(frame_slot, uniforms);
}

void PlanetCelestialBodyFrame::record_pass(const cubey::vulkan::CommandRecorder& recorder,
                                           const cubey::render::RenderTargetView& target,
                                           cubey::render::FrameSlot frame_slot,
                                           const cubey::render::Mesh& mesh) const {
    const cubey::render::RenderTargetRenderingInfo rendering(
        target, cubey::render::RenderClearValues{},
        cubey::render::RenderTargetAttachmentOps{
            .color = cubey::vulkan::load_store_attachment_ops(),
            .depth = cubey::vulkan::load_store_attachment_ops(),
        });
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(target.color.extent);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().layout(), 0,
                                 material().set(frame_slot));
    cubey::render::record_draw_item(recorder.handle(), {
                                                           .mesh = &mesh,
                                                       });
    recorder.end_rendering();
}

bool PlanetCelestialBodyFrame::materials_created() const noexcept {
    return material_.has_value();
}

const cubey::render::FrameUniformMaterialInstance<PlanetCelestialBodyFrameUniforms>&
PlanetCelestialBodyFrame::material() const {
    if (!material_.has_value()) {
        throw std::runtime_error("planet celestial body material is not initialized");
    }
    return material_.value();
}

const cubey::render::GraphicsPipelineResource& PlanetCelestialBodyFrame::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("planet celestial body pipeline is not initialized");
    }
    return pipeline_.value();
}

} // namespace cubey::projects::planet
