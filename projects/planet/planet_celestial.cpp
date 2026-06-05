#include "planet_celestial.h"

#include <cubey/render/pass.h>

#include <algorithm>
#include <array>
#include <cmath>
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

[[nodiscard]] float direction_elevation_degrees(cubey::math::Vec3 direction) {
    const cubey::math::Vec3 normal = normalized_or_up(direction);
    return cubey::render::atmosphere_environment_radians_to_degrees(
        std::asin(std::clamp(normal.y, -1.0F, 1.0F)));
}

[[nodiscard]] float direction_azimuth_degrees(cubey::math::Vec3 direction) {
    const cubey::math::Vec3 normal = normalized_or_up(direction);
    return cubey::render::atmosphere_environment_wrap_signed_degrees(
        cubey::render::atmosphere_environment_radians_to_degrees(std::atan2(normal.x, -normal.z)));
}

} // namespace

PlanetCelestialSystem planet_celestial_system_from_atmosphere(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere) {
    const cubey::render::AtmosphereEnvironmentLighting lighting =
        cubey::render::atmosphere_environment_lighting(atmosphere);
    return {
        .sun =
            {
                .visible = true,
                .direction = normalized_or_up(lighting.sun_direction),
                .color = lighting.sun_color,
                .intensity = lighting.sun_intensity,
                .angular_radius_rad = atmosphere.sun_angular_radius,
            },
    };
}

cubey::render::AtmosphereEnvironmentConfig planet_atmosphere_inputs_from_celestial(
    cubey::render::AtmosphereEnvironmentConfig atmosphere,
    const PlanetCelestialSystem& celestial) {
    atmosphere.sun_elevation_degrees = direction_elevation_degrees(celestial.sun.direction);
    atmosphere.sun_azimuth_degrees = direction_azimuth_degrees(celestial.sun.direction);
    atmosphere.sun_angular_radius = celestial.sun.angular_radius_rad;
    return atmosphere;
}

cubey::render::AtmosphereEnvironmentLighting planet_celestial_lighting(
    const cubey::render::AtmosphereEnvironmentConfig& atmosphere,
    const PlanetCelestialSystem& celestial) {
    return cubey::render::atmosphere_environment_lighting(
        planet_atmosphere_inputs_from_celestial(atmosphere, celestial));
}

PlanetCelestialFrameUniforms planet_celestial_frame_uniforms(
    const PlanetCelestialSystem& celestial, const PlanetCelestialFrameUniformInputs& inputs) {
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
                normalized_or_up(celestial.sun.direction).x,
                normalized_or_up(celestial.sun.direction).y,
                normalized_or_up(celestial.sun.direction).z,
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
    };
}

cubey::render::MaterialPassInfo planet_celestial_pass_info() {
    return {
        .label = "planet.celestial",
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
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    };
}

void PlanetCelestialFrame::create_materials(const cubey::vulkan::Device& device,
                                            const PlanetCelestialFrameMaterialConfig& config) {
    material_.emplace(device, cubey::render::FrameUniformMaterialInstanceConfig{
                                  .material_pass = planet_celestial_pass_info(),
                                  .descriptor_set = 0,
                                  .frame_slot_count = config.frame_slot_count,
                                  .uniform_binding =
                                      binding(PlanetCelestialBinding::FrameUniforms),
                              });
}

void PlanetCelestialFrame::create_pipeline(const cubey::vulkan::Device& device,
                                           const PlanetCelestialFramePipelineConfig& config) {
    const std::array descriptor_set_layouts{material().layout()};
    pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                  .extent = config.extent,
                                  .color_format = config.color_format,
                                  .shader_stage_files = config.shader_stage_files,
                                  .descriptor_set_layouts = descriptor_set_layouts,
                                  .material_pass = planet_celestial_pass_info(),
                              });
}

void PlanetCelestialFrame::destroy_pipeline() {
    pipeline_.reset();
}

void PlanetCelestialFrame::destroy() {
    destroy_pipeline();
    material_.reset();
}

void PlanetCelestialFrame::upload(cubey::render::FrameSlot frame_slot,
                                  const PlanetCelestialFrameUniforms& uniforms) const {
    material().upload(frame_slot, uniforms);
}

void PlanetCelestialFrame::record_pass(const cubey::vulkan::CommandRecorder& recorder,
                                       cubey::render::ColorTargetView target,
                                       cubey::render::FrameSlot frame_slot) const {
    const cubey::render::RenderTargetRenderingInfo rendering(
        cubey::render::render_target_view(target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        cubey::render::RenderTargetAttachmentOps{
            .color = cubey::vulkan::load_store_attachment_ops(),
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

bool PlanetCelestialFrame::materials_created() const noexcept {
    return material_.has_value();
}

const cubey::render::FrameUniformMaterialInstance<PlanetCelestialFrameUniforms>&
PlanetCelestialFrame::material() const {
    if (!material_.has_value()) {
        throw std::runtime_error("planet celestial material is not initialized");
    }
    return material_.value();
}

const cubey::render::GraphicsPipelineResource& PlanetCelestialFrame::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("planet celestial pipeline is not initialized");
    }
    return pipeline_.value();
}

} // namespace cubey::projects::planet
