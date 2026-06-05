#include "planet_celestial.h"

#include <cubey/render/pass.h>

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

} // namespace

float planet_solar_time_simulation_day(const PlanetSolarTime& time) {
    return std::max(time.day_of_year - 1.0F, 0.0F) +
           std::clamp(time.time_hours, 0.0F, 24.0F) / 24.0F;
}

void planet_solar_time_advance(PlanetSolarTime& time, double delta_seconds) {
    if (time.hours_per_second == 0.0F || delta_seconds <= 0.0) {
        return;
    }
    const double advanced_hours =
        static_cast<double>(time.time_hours) +
        delta_seconds * static_cast<double>(time.hours_per_second);
    const double day_offset = std::floor(advanced_hours / 24.0);
    double wrapped_hours = std::fmod(advanced_hours, 24.0);
    if (wrapped_hours < 0.0) {
        wrapped_hours += 24.0;
    }
    time.time_hours = static_cast<float>(wrapped_hours);
    time.day_of_year += static_cast<float>(day_offset);
    while (time.day_of_year > 365.2422F) {
        time.day_of_year -= 365.2422F;
    }
    while (time.day_of_year < 1.0F) {
        time.day_of_year += 365.2422F;
    }
}

PlanetCelestialSystem planet_celestial_system_from_solar_time(
    const PlanetSolarTime& time, const PlanetSolarSystemConfig& solar) {
    constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0F;
    const float simulation_day = planet_solar_time_simulation_day(time);
    const float rotation_angle =
        kTwoPi * simulation_day / std::max(solar.planet_rotation_period_days, 0.0001F);
    const float orbit_angle =
        kTwoPi * (simulation_day - solar.equinox_day) /
        std::max(solar.planet_orbit_period_days, 0.0001F);
    const float moon_orbit_angle =
        kTwoPi * simulation_day / std::max(solar.moon_orbit_period_days, 0.0001F);

    const cubey::math::Vec3 sun_in_orbital_frame =
        normalized_or_up({std::cos(orbit_angle), 0.0F, -std::sin(orbit_angle)});
    const cubey::math::Vec3 sun_in_tilted_frame =
        normalized_or_up(rotate_x(sun_in_orbital_frame, solar.axial_tilt_rad));
    const cubey::math::Vec3 sun_in_planet_frame =
        normalized_or_up(rotate_y(sun_in_tilted_frame, -rotation_angle));

    const cubey::math::Vec3 moon_orbit_frame = normalized_or_up({
        std::cos(moon_orbit_angle),
        std::sin(moon_orbit_angle) * 0.089F,
        std::sin(moon_orbit_angle),
    });
    const cubey::math::Vec3 moon_in_tilted_frame =
        normalized_or_up(rotate_x(moon_orbit_frame, solar.axial_tilt_rad));
    const cubey::math::Vec3 moon_in_planet_frame =
        normalized_or_up(rotate_y(moon_in_tilted_frame, -rotation_angle));
    const float phase_fraction =
        wrap_unit(std::acos(std::clamp(glm::dot(-moon_in_planet_frame, sun_in_planet_frame),
                                       -1.0F, 1.0F)) /
                  kTwoPi);

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
                .angular_radius_rad =
                    angular_radius(solar.moon_radius_m, solar.moon_distance_m),
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

PlanetCelestialBodyRenderPlacement planet_celestial_body_render_placement(
    const PlanetCelestialBody& body, const PlanetCelestialBodyRenderPlacementInputs& inputs) {
    const float near_plane = std::max(inputs.near_plane_m, 0.001F);
    const float far_plane = std::max(inputs.far_plane_m, near_plane + 1.0F);
    const float shell_distance =
        std::clamp(far_plane * std::clamp(inputs.shell_distance_fraction, 0.10F, 0.90F),
                   near_plane * 4.0F, far_plane * 0.90F);
    const float scaled_angular_radius =
        std::clamp(body.angular_radius_rad * std::max(inputs.angular_radius_scale, 0.0F),
                   0.00001F, 0.35F);
    const float render_radius = std::tan(scaled_angular_radius) * shell_distance;
    const bool visible =
        body.visible && finite_positive(body.angular_radius_rad) && finite_positive(render_radius);

    return {
        .visible = visible,
        .center_render_m =
            inputs.camera_render_position_m + normalized_or_up(body.direction) * shell_distance,
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
        .ambient_color = {0.040F, 0.050F, 0.070F},
        .ambient_intensity = 0.12F,
        .haze_color = {0.085F, 0.125F, 0.185F},
    };
}

PlanetSkyFrameUniforms planet_sky_frame_uniforms(
    const PlanetCelestialSystem& celestial, const PlanetSkyFrameUniformInputs& inputs) {
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

void PlanetSkyFrame::create_materials(const cubey::vulkan::Device& device,
                                      const PlanetSkyFrameMaterialConfig& config) {
    material_.emplace(device, cubey::render::FrameUniformMaterialInstanceConfig{
                                  .material_pass = planet_sky_pass_info(),
                                  .descriptor_set = 0,
                                  .frame_slot_count = config.frame_slot_count,
                                  .uniform_binding =
                                      binding(PlanetCelestialBinding::FrameUniforms),
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
    cubey::render::record_fullscreen_pipeline_draw(recorder, {
                                                                 .pipeline = &pipeline(),
                                                                 .descriptor_set =
                                                                     material().set(frame_slot),
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

} // namespace cubey::projects::planet
