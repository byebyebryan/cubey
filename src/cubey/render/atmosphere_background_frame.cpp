#include <cubey/render/atmosphere_background_frame.h>

#include <cubey/render/pass.h>

#include <array>
#include <span>
#include <stdexcept>
#include <utility>

namespace cubey::render {
namespace {

[[nodiscard]] constexpr std::uint32_t binding(AtmosphereBackgroundBinding value) noexcept {
    return static_cast<std::uint32_t>(value);
}

void validate_texture_bindings(const AtmosphereBackgroundTextureBindings& textures) {
    if (textures.lunar_sampler == VK_NULL_HANDLE || textures.lunar_view == VK_NULL_HANDLE) {
        throw std::runtime_error("atmosphere background lunar atlas binding is not initialized");
    }
    if (textures.night_sky_sampler == VK_NULL_HANDLE || textures.night_sky_view == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "atmosphere background night sky atlas binding is not initialized");
    }
}

} // namespace

AtmosphereBackgroundTextureBindings AtmosphereBackgroundPlaceholderTextures::bindings() const {
    return {
        .lunar_sampler = lunar.sampler().handle(),
        .lunar_view = lunar.view(),
        .night_sky_sampler = night_sky.sampler().handle(),
        .night_sky_view = night_sky.view(),
    };
}

AtmosphereBackgroundPlaceholderTextures create_atmosphere_background_placeholder_textures(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
    const std::array<std::uint8_t, 4> lunar_pixel{112U, 128U, 128U, 255U};
    const cubey::vulkan::SamplerConfig atlas_sampler{
        .min_filter = VK_FILTER_LINEAR,
        .mag_filter = VK_FILTER_LINEAR,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .max_lod = 0.0F,
    };
    Texture2D lunar = create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {1U, 1U},
            .mip_levels = 1U,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .rgba8 = std::span<const std::uint8_t>{lunar_pixel.data(), lunar_pixel.size()},
            .create_sampler = true,
            .sampler = atlas_sampler,
        });

    const std::array<float, 24> night_sky_rgba32f{};
    const std::span<const std::uint8_t> night_sky_bytes{
        reinterpret_cast<const std::uint8_t*>(night_sky_rgba32f.data()),
        night_sky_rgba32f.size() * sizeof(float),
    };
    TextureCube night_sky = create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = 1U,
            .mip_levels = 1U,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .bytes = night_sky_bytes,
            .create_sampler = true,
            .sampler = atlas_sampler,
        });

    return {
        .lunar = std::move(lunar),
        .night_sky = std::move(night_sky),
    };
}

MaterialPassInfo atmosphere_background_pass_info() {
    return MaterialPassInfo{
        .label = "atmosphere.fullscreen",
        .descriptor_sets =
            {
                MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(AtmosphereBackgroundBinding::FrameUniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(AtmosphereBackgroundBinding::MoonAtlas),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(AtmosphereBackgroundBinding::NightSkyAtlas),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
    };
}

void AtmosphereBackgroundFrame::create_materials(
    const cubey::vulkan::Device& device, const AtmosphereBackgroundFrameMaterialConfig& config) {
    validate_texture_bindings(config.textures);
    material_.emplace(
        device, FrameUniformMaterialInstanceConfig{
                    .material_pass = atmosphere_background_pass_info(),
                    .descriptor_set = 0,
                    .frame_slot_count = config.frame_slot_count,
                    .uniform_binding = binding(AtmosphereBackgroundBinding::FrameUniforms),
                    .sampled_images =
                        {
                            SampledImageMaterialBinding{
                                .binding = binding(AtmosphereBackgroundBinding::MoonAtlas),
                                .sampler = config.textures.lunar_sampler,
                                .image_view = config.textures.lunar_view,
                                .layout = config.textures.lunar_layout,
                            },
                            SampledImageMaterialBinding{
                                .binding = binding(AtmosphereBackgroundBinding::NightSkyAtlas),
                                .sampler = config.textures.night_sky_sampler,
                                .image_view = config.textures.night_sky_view,
                                .layout = config.textures.night_sky_layout,
                            },
                        },
                });
}

void AtmosphereBackgroundFrame::update_texture_bindings(
    const cubey::vulkan::Device& device,
    const AtmosphereBackgroundTextureBindings& textures) const {
    validate_texture_bindings(textures);
    const FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>& current_material =
        material();
    for (std::uint32_t slot_index = 0U;
         slot_index < current_material.material_instance().set_count(); ++slot_index) {
        const FrameSlot frame_slot{
            .index = slot_index,
            .count = current_material.material_instance().set_count(),
        };
        MaterialDescriptorWriter(current_material.set(frame_slot))
            .uniform_buffer(binding(AtmosphereBackgroundBinding::FrameUniforms),
                            current_material.uniforms().buffer(frame_slot).handle(),
                            current_material.uniforms().range())
            .combined_image_sampler(binding(AtmosphereBackgroundBinding::MoonAtlas),
                                    textures.lunar_sampler, textures.lunar_view,
                                    textures.lunar_layout)
            .combined_image_sampler(binding(AtmosphereBackgroundBinding::NightSkyAtlas),
                                    textures.night_sky_sampler, textures.night_sky_view,
                                    textures.night_sky_layout)
            .update(device);
    }
}

void AtmosphereBackgroundFrame::create_pipeline(
    const cubey::vulkan::Device& device, const AtmosphereBackgroundFramePipelineConfig& config) {
    const std::array descriptor_set_layouts{material().layout()};
    pipeline_.emplace(device, GraphicsPipelineFileResourceConfig{
                                  .extent = config.extent,
                                  .color_format = config.color_format,
                                  .depth_format = config.depth_format,
                                  .shader_stage_files = config.shader_stage_files,
                                  .descriptor_set_layouts = descriptor_set_layouts,
                                  .material_pass = atmosphere_background_pass_info(),
                              });
}

void AtmosphereBackgroundFrame::destroy_pipeline() {
    pipeline_.reset();
}

void AtmosphereBackgroundFrame::destroy() {
    destroy_pipeline();
    material_.reset();
}

void AtmosphereBackgroundFrame::upload(FrameSlot frame_slot,
                                       const AtmosphereEnvironmentFrameUniforms& uniforms) const {
    material().upload(frame_slot, uniforms);
}

void AtmosphereBackgroundFrame::record_pass(const cubey::vulkan::CommandRecorder& recorder,
                                            ColorTargetView target, FrameSlot frame_slot) const {
    record_render_target_pass(
        recorder, render_target_view(target),
        RenderClearValues{
            .color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        [this, frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
            record_fullscreen_pipeline_draw(pass_recorder,
                                            {
                                                .pipeline = &pipeline(),
                                                .descriptor_set = material().set(frame_slot),
                                            });
        });
}

bool AtmosphereBackgroundFrame::materials_created() const noexcept {
    return material_.has_value();
}

const FrameUniformMaterialInstance<AtmosphereEnvironmentFrameUniforms>&
AtmosphereBackgroundFrame::material() const {
    if (!material_.has_value()) {
        throw std::runtime_error("atmosphere background material is not initialized");
    }
    return material_.value();
}

const GraphicsPipelineResource& AtmosphereBackgroundFrame::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("atmosphere background pipeline is not initialized");
    }
    return pipeline_.value();
}

} // namespace cubey::render
