#include <cubey/render/sky_frame.h>

#include <cubey/render/pass.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace cubey::render {
namespace {

enum class SkyBinding : std::uint32_t {
    FrameUniforms = 0,
    MoonAtlas = 1,
    NightSkyAtlas = 2,
};

[[nodiscard]] constexpr std::uint32_t binding(SkyBinding binding) noexcept {
    return static_cast<std::uint32_t>(binding);
}

[[nodiscard]] cubey::math::Vec3 normalized_or_up(cubey::math::Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(direction);
}

[[nodiscard]] cubey::math::Vec3 to_float(cubey::math::DVec3 value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

} // namespace

SkyFrameUniforms sky_frame_uniforms(const CelestialSystem& celestial,
                                    const SkyFrameUniformInputs& inputs) {
    const cubey::math::Vec3 sun_direction = normalized_or_up(celestial.sun.direction);
    const cubey::math::Vec3 moon_direction = normalized_or_up(celestial.moon.direction);
    const cubey::math::DVec3 moon_world_position_m{
        static_cast<double>(moon_direction.x) *
            static_cast<double>(std::max(celestial.moon.distance_m, 1.0F)),
        static_cast<double>(moon_direction.y) *
            static_cast<double>(std::max(celestial.moon.distance_m, 1.0F)),
        static_cast<double>(moon_direction.z) *
            static_cast<double>(std::max(celestial.moon.distance_m, 1.0F)),
    };
    const cubey::math::DVec3 camera_world_position_m{
        static_cast<double>(inputs.camera_position_m.x),
        static_cast<double>(inputs.camera_position_m.y),
        static_cast<double>(inputs.camera_position_m.z),
    };
    const cubey::math::DVec3 camera_to_moon_m = moon_world_position_m - camera_world_position_m;
    const cubey::math::Vec3 moon_mask_direction = glm::length(camera_to_moon_m) > 0.000001
                                                      ? normalized_or_up(to_float(camera_to_moon_m))
                                                      : moon_direction;
    const float moon_radius = celestial.moon.visible
                                  ? std::max(celestial.moon.angular_radius_rad *
                                                 std::max(inputs.moon_angular_radius_scale, 0.0F),
                                             0.0F)
                                  : 0.0F;
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
        .moon_direction_radius =
            {
                moon_mask_direction.x,
                moon_mask_direction.y,
                moon_mask_direction.z,
                moon_radius,
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
                static_cast<float>(inputs.atmosphere_mode),
                0.0F,
                0.0F,
                0.0F,
            },
        .night_options =
            {
                0.54F,
                0.16F,
                0.62F,
                1.0F,
            },
        .celestial_options =
            {
                std::cos(celestial.planet_rotation_angle_rad),
                std::sin(celestial.planet_rotation_angle_rad),
                0.0F,
                0.0F,
            },
        .moon_options =
            {
                0.32F,
                0.45F,
                0.0F,
                0.0F,
            },
        .moon_phase_options =
            {
                celestial.moon.phase_fraction,
                celestial.moon.intensity,
                0.0F,
                0.0F,
            },
        .milky_way_options =
            {
                0.78F,
                0.60F,
                0.0F,
                0.0F,
            },
        .render_options =
            {
                0.0F,
                0.0F,
                0.0F,
                0.0F,
            },
    };
}

MaterialPassInfo sky_pass_info() {
    return {
        .label = "sky.fullscreen",
        .descriptor_sets =
            {
                MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(SkyBinding::FrameUniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(SkyBinding::MoonAtlas),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(SkyBinding::NightSkyAtlas),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .blend_enable = false,
    };
}

void SkyFrame::create_materials(const cubey::vulkan::Device& device,
                                const SkyFrameMaterialConfig& config) {
    validate_atmosphere_background_texture_bindings(config.textures);
    material_.emplace(device, FrameUniformMaterialInstanceConfig{
                                  .material_pass = sky_pass_info(),
                                  .descriptor_set = 0,
                                  .frame_slot_count = config.frame_slot_count,
                                  .uniform_binding = binding(SkyBinding::FrameUniforms),
                                  .sampled_images =
                                      {
                                          SampledImageMaterialBinding{
                                              .binding = binding(SkyBinding::MoonAtlas),
                                              .sampler = config.textures.lunar_sampler,
                                              .image_view = config.textures.lunar_view,
                                              .layout = config.textures.lunar_layout,
                                          },
                                          SampledImageMaterialBinding{
                                              .binding = binding(SkyBinding::NightSkyAtlas),
                                              .sampler = config.textures.night_sky_sampler,
                                              .image_view = config.textures.night_sky_view,
                                              .layout = config.textures.night_sky_layout,
                                          },
                                      },
                              });
}

void SkyFrame::create_pipeline(const cubey::vulkan::Device& device,
                               const SkyFramePipelineConfig& config) {
    const std::array descriptor_set_layouts{material().layout()};
    pipeline_.emplace(device, GraphicsPipelineFileResourceConfig{
                                  .extent = config.extent,
                                  .color_format = config.color_format,
                                  .shader_stage_files = config.shader_stage_files,
                                  .descriptor_set_layouts = descriptor_set_layouts,
                                  .material_pass = sky_pass_info(),
                              });
}

void SkyFrame::destroy_pipeline() {
    pipeline_.reset();
}

void SkyFrame::destroy() {
    destroy_pipeline();
    material_.reset();
}

void SkyFrame::upload(FrameSlot frame_slot, const SkyFrameUniforms& uniforms) const {
    material().upload(frame_slot, uniforms);
}

void SkyFrame::record_pass(const cubey::vulkan::CommandRecorder& recorder, ColorTargetView target,
                           FrameSlot frame_slot) const {
    const RenderTargetRenderingInfo rendering(
        render_target_view(target),
        RenderClearValues{
            .color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        RenderTargetAttachmentOps{
            .color = cubey::vulkan::clear_store_attachment_ops(),
        });
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(target.extent);
    record_fullscreen_pipeline_draw(recorder,
                                    {
                                        .pipeline = &pipeline(),
                                        .descriptor_set = material().set(frame_slot),
                                    });
    recorder.end_rendering();
}

bool SkyFrame::materials_created() const noexcept {
    return material_.has_value();
}

const FrameUniformMaterialInstance<SkyFrameUniforms>& SkyFrame::material() const {
    if (!material_.has_value()) {
        throw std::runtime_error("sky frame material is not initialized");
    }
    return material_.value();
}

const GraphicsPipelineResource& SkyFrame::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("sky frame pipeline is not initialized");
    }
    return pipeline_.value();
}

} // namespace cubey::render
