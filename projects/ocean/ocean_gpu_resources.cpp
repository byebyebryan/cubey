#include "ocean_gpu_resources.h"

#include <cubey/render/material.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::ocean {
namespace {

constexpr VkFormat kOceanFieldFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr std::uint32_t kOceanSurfaceReflectionBinding = kOceanCascadeCount * 3U;
constexpr std::uint32_t kOceanSurfaceSkyRadianceBinding = kOceanSurfaceReflectionBinding + 1U;
constexpr std::uint32_t kOceanSurfaceTerrainFieldBinding = kOceanSurfaceSkyRadianceBinding + 1U;
constexpr std::uint32_t kOceanSurfaceTerrainFieldUniformBinding =
    kOceanSurfaceTerrainFieldBinding + 1U;
constexpr std::uint32_t kOceanSurfaceFeatureUniformBinding =
    kOceanSurfaceTerrainFieldUniformBinding + 1U;

[[nodiscard]] std::filesystem::path shader_path(const std::filesystem::path& shader_dir,
                                                const char* filename) {
    return shader_dir / filename;
}

[[nodiscard]] std::uint32_t field_texture_index(std::uint32_t cascade, std::uint32_t field) {
    return cascade * kOceanSpectrumFieldCount + field;
}

[[nodiscard]] cubey::render::Texture2D make_ocean_field_texture(const cubey::vulkan::Device& device,
                                                                std::uint32_t resolution,
                                                                bool sampled) {
    return cubey::render::Texture2D(device,
                                    cubey::render::Texture2DConfig{
                                        .extent = {resolution, resolution},
                                        .format = kOceanFieldFormat,
                                        .usage = cubey::render::Texture2DUsage::StorageSampled,
                                        .create_sampler = sampled,
                                        .sampler =
                                            {
                                                .min_filter = VK_FILTER_LINEAR,
                                                .mag_filter = VK_FILTER_LINEAR,
                                                .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                            },
                                    });
}

[[nodiscard]] cubey::render::MaterialPassInfo ocean_surface_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 64U,
    };
    return {
        .label = "ocean.surface",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
}

[[nodiscard]] VkPushConstantRange compute_push_constant_range(std::uint32_t float_count) {
    return {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = static_cast<std::uint32_t>(sizeof(float) * float_count),
    };
}

[[nodiscard]] cubey::vulkan::DescriptorSetInfo
descriptor_info(std::span<const cubey::vulkan::DescriptorSetBindingConfig> bindings,
                std::uint32_t max_sets) {
    return cubey::vulkan::DescriptorSetInfo(bindings, max_sets);
}

} // namespace

void OceanGpuResources::create(const cubey::vulkan::Device& device,
                               const OceanGpuResourceConfig& config) {
    reset();
    validate_ocean_config(config.ocean);
    if (config.shader_dir.empty()) {
        throw std::runtime_error("ocean GPU resources require a shader directory");
    }
    if (config.color_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("ocean surface pipeline requires a color format");
    }
    if (config.depth_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("ocean surface pipeline requires a depth format");
    }
    if (config.target_extent.width == 0U || config.target_extent.height == 0U) {
        throw std::runtime_error("ocean surface pipeline requires a target extent");
    }
    if (config.frame_slot_count == 0U) {
        throw std::runtime_error("ocean surface pipeline requires at least one frame slot");
    }

    resolution_ = config.ocean.map_size;
    create_textures(device, config.ocean);
    surface_feature_uniforms_.emplace(device, config.frame_slot_count);
    create_descriptor_sets(device, config.frame_slot_count);
    update_descriptors(device);
    create_pipelines(device, config);
}

void OceanGpuResources::reset() {
    surface_pipeline_.reset();
    unpack_pipeline_.reset();
    fft_pipeline_.reset();
    modulate_pipeline_.reset();
    spectrum_pipeline_.reset();

    surface_pool_.reset();
    surface_layout_.reset();
    surface_feature_uniforms_.reset();
    unpack_pool_.reset();
    unpack_layout_.reset();
    fft_pool_.reset();
    fft_layout_.reset();
    modulate_pool_.reset();
    modulate_layout_.reset();
    spectrum_pool_.reset();
    spectrum_layout_.reset();

    surface_sets_.clear();
    unpack_sets_ = {};
    fft_sets_ = {};
    modulate_sets_ = {};
    spectrum_sets_ = {};

    foam_ = {};
    normal_ = {};
    displacement_ = {};
    pong_ = {};
    ping_ = {};
    fields_ = {};
    h0_ = {};
    resolution_ = 0;
}

void OceanGpuResources::create_textures(const cubey::vulkan::Device& device,
                                        const OceanConfig& config) {
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        h0_[cascade].emplace(make_ocean_field_texture(device, config.map_size, false));
        for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
            const std::uint32_t index = field_texture_index(cascade, field);
            fields_[index].emplace(make_ocean_field_texture(device, config.map_size, false));
            ping_[index].emplace(make_ocean_field_texture(device, config.map_size, false));
            pong_[index].emplace(make_ocean_field_texture(device, config.map_size, false));
        }
        displacement_[cascade].emplace(make_ocean_field_texture(device, config.map_size, true));
        normal_[cascade].emplace(make_ocean_field_texture(device, config.map_size, true));
        foam_[cascade].emplace(make_ocean_field_texture(device, config.map_size, true));
    }
}

void OceanGpuResources::create_descriptor_sets(const cubey::vulkan::Device& device,
                                               std::uint32_t frame_slot_count) {
    const std::array spectrum_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo spectrum_info =
        descriptor_info(spectrum_bindings, kOceanCascadeCount);
    spectrum_layout_.emplace(device, spectrum_info.layout_info());
    spectrum_pool_.emplace(device, spectrum_info.pool_info());
    for (VkDescriptorSet& set : spectrum_sets_) {
        set = spectrum_pool_->allocate(spectrum_layout_->handle());
    }

    const std::array modulate_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 3,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 4,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo modulate_info =
        descriptor_info(modulate_bindings, kOceanCascadeCount);
    modulate_layout_.emplace(device, modulate_info.layout_info());
    modulate_pool_.emplace(device, modulate_info.pool_info());
    for (VkDescriptorSet& set : modulate_sets_) {
        set = modulate_pool_->allocate(modulate_layout_->handle());
    }

    const std::array fft_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo fft_info =
        descriptor_info(fft_bindings, static_cast<std::uint32_t>(fft_sets_.size()));
    fft_layout_.emplace(device, fft_info.layout_info());
    fft_pool_.emplace(device, fft_info.pool_info());
    for (VkDescriptorSet& set : fft_sets_) {
        set = fft_pool_->allocate(fft_layout_->handle());
    }

    const std::array unpack_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 3,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 4,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 5,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 6,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo unpack_info =
        descriptor_info(unpack_bindings, kOceanCascadeCount);
    unpack_layout_.emplace(device, unpack_info.layout_info());
    unpack_pool_.emplace(device, unpack_info.pool_info());
    for (VkDescriptorSet& set : unpack_sets_) {
        set = unpack_pool_->allocate(unpack_layout_->handle());
    }

    std::array<cubey::vulkan::DescriptorSetBindingConfig, kOceanCascadeCount * 3U + 5U>
        surface_bindings{};
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        surface_bindings[cascade] = cubey::vulkan::DescriptorSetBindingConfig{
            .binding = cascade,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
        };
    }
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        surface_bindings[kOceanCascadeCount + cascade] = cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanCascadeCount + cascade,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        surface_bindings[kOceanCascadeCount * 2U + cascade] =
            cubey::vulkan::DescriptorSetBindingConfig{
                .binding = kOceanCascadeCount * 2U + cascade,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
    }
    surface_bindings[kOceanSurfaceReflectionBinding] = cubey::vulkan::DescriptorSetBindingConfig{
        .binding = kOceanSurfaceReflectionBinding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    surface_bindings[kOceanSurfaceSkyRadianceBinding] = cubey::vulkan::DescriptorSetBindingConfig{
        .binding = kOceanSurfaceSkyRadianceBinding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    surface_bindings[kOceanSurfaceTerrainFieldBinding] = cubey::vulkan::DescriptorSetBindingConfig{
        .binding = kOceanSurfaceTerrainFieldBinding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    surface_bindings[kOceanSurfaceTerrainFieldUniformBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceTerrainFieldUniformBinding,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceFeatureUniformBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceFeatureUniformBinding,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    const cubey::vulkan::DescriptorSetInfo surface_info =
        descriptor_info(surface_bindings, frame_slot_count);
    surface_layout_.emplace(device, surface_info.layout_info());
    surface_pool_.emplace(device, surface_info.pool_info());
    surface_sets_.resize(frame_slot_count, VK_NULL_HANDLE);
    for (VkDescriptorSet& set : surface_sets_) {
        set = surface_pool_->allocate(surface_layout_->handle());
    }
}

void OceanGpuResources::update_descriptors(const cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch writes;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        writes.storage_image(spectrum_set(cascade), 0, h0(cascade).view())
            .storage_image(modulate_set(cascade), 0, h0(cascade).view())
            .storage_image(modulate_set(cascade), 1, field(cascade, 0).view())
            .storage_image(modulate_set(cascade), 2, field(cascade, 1).view())
            .storage_image(modulate_set(cascade), 3, field(cascade, 2).view())
            .storage_image(modulate_set(cascade), 4, field(cascade, 3).view())
            .storage_image(unpack_set(cascade), 0, pong(cascade, 0).view())
            .storage_image(unpack_set(cascade), 1, pong(cascade, 1).view())
            .storage_image(unpack_set(cascade), 2, pong(cascade, 2).view())
            .storage_image(unpack_set(cascade), 3, pong(cascade, 3).view())
            .storage_image(unpack_set(cascade), 4, displacement(cascade).view())
            .storage_image(unpack_set(cascade), 5, normal(cascade).view())
            .storage_image(unpack_set(cascade), 6, foam(cascade).view());
        for (VkDescriptorSet surface_set : surface_sets_) {
            writes.combined_image_sampler(surface_set, cascade,
                                          displacement(cascade).sampler().handle(),
                                          displacement(cascade).view(), VK_IMAGE_LAYOUT_GENERAL);
            writes.combined_image_sampler(surface_set, cascade + kOceanCascadeCount,
                                          normal(cascade).sampler().handle(), normal(cascade).view(),
                                          VK_IMAGE_LAYOUT_GENERAL);
            writes.combined_image_sampler(surface_set, cascade + kOceanCascadeCount * 2U,
                                          foam(cascade).sampler().handle(), foam(cascade).view(),
                                          VK_IMAGE_LAYOUT_GENERAL);
        }

        for (std::uint32_t field_index = 0; field_index < kOceanSpectrumFieldCount; ++field_index) {
            const std::uint32_t base_fft_set =
                (cascade * kOceanSpectrumFieldCount + field_index) * 3U;
            writes
                .storage_image(fft_sets_[base_fft_set + 0U], 0, field(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 0U], 1, ping(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 1U], 0, ping(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 1U], 1, pong(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 2U], 0, pong(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 2U], 1, ping(cascade, field_index).view());
        }
    }
    if (!surface_feature_uniforms_.has_value()) {
        throw std::runtime_error("ocean surface feature uniforms are not initialized");
    }
    const std::uint32_t slot_count = surface_feature_uniforms_->slot_count();
    for (std::uint32_t index = 0; index < slot_count; ++index) {
        const cubey::render::FrameSlot frame_slot{.index = index, .count = slot_count};
        writes.uniform_buffer(surface_set(frame_slot), kOceanSurfaceFeatureUniformBinding,
                              surface_feature_uniforms_->buffer(frame_slot).handle(),
                              surface_feature_uniforms_->range());
    }
    writes.update(device);
}

void OceanGpuResources::update_atmosphere_probe_descriptors(
    const cubey::vulkan::Device& device, const cubey::render::TextureCube& reflection_probe,
    const cubey::render::TextureCube& sky_radiance) {
    if (surface_sets_.empty()) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    cubey::vulkan::DescriptorWriteBatch writes;
    for (VkDescriptorSet surface_set : surface_sets_) {
        writes
            .combined_image_sampler(surface_set, kOceanSurfaceReflectionBinding,
                                    reflection_probe.sampler().handle(), reflection_probe.view(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .combined_image_sampler(surface_set, kOceanSurfaceSkyRadianceBinding,
                                    sky_radiance.sampler().handle(), sky_radiance.view(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    writes.update(device);
}

void OceanGpuResources::update_terrain_ocean_field_descriptor(
    const cubey::vulkan::Device& device, const cubey::render::Texture2D& fields) {
    if (surface_sets_.empty()) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    cubey::vulkan::DescriptorWriteBatch writes;
    for (VkDescriptorSet surface_set : surface_sets_) {
        writes.combined_image_sampler(surface_set, kOceanSurfaceTerrainFieldBinding,
                                      fields.sampler().handle(), fields.view(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    writes.update(device);
}

void OceanGpuResources::update_terrain_ocean_field_uniform_descriptor(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot, VkBuffer buffer,
    VkDeviceSize range) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .uniform_buffer(surface_set(frame_slot), kOceanSurfaceTerrainFieldUniformBinding, buffer,
                        range)
        .update(device);
}

void OceanGpuResources::upload_surface_feature_uniforms(
    cubey::render::FrameSlot frame_slot, const OceanSurfaceFeatureUniforms& uniforms) const {
    if (!surface_feature_uniforms_.has_value()) {
        throw std::runtime_error("ocean surface feature uniforms are not initialized");
    }
    surface_feature_uniforms_->upload(frame_slot, uniforms);
}

void OceanGpuResources::create_pipelines(const cubey::vulkan::Device& device,
                                         const OceanGpuResourceConfig& config) {
    const VkPushConstantRange spectrum_push_constants = compute_push_constant_range(16U);
    const VkPushConstantRange modulate_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange fft_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange unpack_push_constants = compute_push_constant_range(8U);

    const std::array spectrum_layouts{spectrum_layout_->handle()};
    spectrum_pipeline_.emplace(device,
                               cubey::render::ComputePipelineResourceConfig{
                                   .shader_stage = cubey::render::compute_shader_file(
                                       shader_path(config.shader_dir, "ocean_spectrum.comp.spv")),
                                   .descriptor_set_layouts = spectrum_layouts,
                                   .push_constants = {&spectrum_push_constants, 1},
                               });

    const std::array modulate_layouts{modulate_layout_->handle()};
    modulate_pipeline_.emplace(device,
                               cubey::render::ComputePipelineResourceConfig{
                                   .shader_stage = cubey::render::compute_shader_file(
                                       shader_path(config.shader_dir, "ocean_modulate.comp.spv")),
                                   .descriptor_set_layouts = modulate_layouts,
                                   .push_constants = {&modulate_push_constants, 1},
                               });

    const std::array fft_layouts{fft_layout_->handle()};
    fft_pipeline_.emplace(device, cubey::render::ComputePipelineResourceConfig{
                                      .shader_stage = cubey::render::compute_shader_file(
                                          shader_path(config.shader_dir, "ocean_fft.comp.spv")),
                                      .descriptor_set_layouts = fft_layouts,
                                      .push_constants = {&fft_push_constants, 1},
                                  });

    const std::array unpack_layouts{unpack_layout_->handle()};
    unpack_pipeline_.emplace(device,
                             cubey::render::ComputePipelineResourceConfig{
                                 .shader_stage = cubey::render::compute_shader_file(
                                     shader_path(config.shader_dir, "ocean_unpack.comp.spv")),
                                 .descriptor_set_layouts = unpack_layouts,
                                 .push_constants = {&unpack_push_constants, 1},
                             });

    const std::array surface_shader_stage_files{
        cubey::render::vertex_shader_file(shader_path(config.shader_dir, "ocean.vert.spv")),
        cubey::render::fragment_shader_file(shader_path(config.shader_dir, "ocean.frag.spv")),
    };
    const std::array surface_layouts{surface_layout_->handle()};
    surface_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                          .extent = config.target_extent,
                                          .color_format = config.color_format,
                                          .depth_format = config.depth_format,
                                          .shader_stage_files = surface_shader_stage_files,
                                          .descriptor_set_layouts = surface_layouts,
                                          .material_pass = ocean_surface_pass_info(),
                                      });
}

const cubey::render::GraphicsPipelineResource& OceanGpuResources::surface_pipeline() const {
    if (!surface_pipeline_.has_value()) {
        throw std::runtime_error("ocean surface pipeline is not initialized");
    }
    return surface_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::spectrum_pipeline() const {
    if (!spectrum_pipeline_.has_value()) {
        throw std::runtime_error("ocean spectrum pipeline is not initialized");
    }
    return spectrum_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::modulate_pipeline() const {
    if (!modulate_pipeline_.has_value()) {
        throw std::runtime_error("ocean modulate pipeline is not initialized");
    }
    return modulate_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::fft_pipeline() const {
    if (!fft_pipeline_.has_value()) {
        throw std::runtime_error("ocean FFT pipeline is not initialized");
    }
    return fft_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::unpack_pipeline() const {
    if (!unpack_pipeline_.has_value()) {
        throw std::runtime_error("ocean unpack pipeline is not initialized");
    }
    return unpack_pipeline_.value();
}

VkDescriptorSet OceanGpuResources::spectrum_set(std::uint32_t cascade) const {
    return descriptor_at(spectrum_sets_, cascade, "ocean spectrum descriptor set");
}

VkDescriptorSet OceanGpuResources::modulate_set(std::uint32_t cascade) const {
    return descriptor_at(modulate_sets_, cascade, "ocean modulate descriptor set");
}

VkDescriptorSet OceanGpuResources::fft_set(std::uint32_t cascade, std::uint32_t field,
                                           std::uint32_t set_index) const {
    if (field >= kOceanSpectrumFieldCount || set_index >= 3U) {
        throw std::runtime_error("ocean FFT descriptor index out of range");
    }
    return descriptor_at(fft_sets_, (cascade * kOceanSpectrumFieldCount + field) * 3U + set_index,
                         "ocean FFT descriptor set");
}

VkDescriptorSet OceanGpuResources::unpack_set(std::uint32_t cascade) const {
    return descriptor_at(unpack_sets_, cascade, "ocean unpack descriptor set");
}

VkDescriptorSet OceanGpuResources::surface_set(cubey::render::FrameSlot frame_slot) const {
    cubey::render::validate_frame_slot(frame_slot);
    if (frame_slot.count != surface_sets_.size()) {
        throw std::runtime_error("ocean surface descriptor set frame slot count mismatch");
    }
    const VkDescriptorSet set = surface_sets_.at(static_cast<std::size_t>(frame_slot.index));
    if (set == VK_NULL_HANDLE) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    return set;
}

const cubey::render::Texture2D& OceanGpuResources::h0(std::uint32_t cascade) const {
    return texture_at(h0_, cascade, "ocean h0 texture");
}

const cubey::render::Texture2D& OceanGpuResources::field(std::uint32_t cascade,
                                                         std::uint32_t field) const {
    return field_texture_at(fields_, cascade, field, "ocean spectrum field texture");
}

const cubey::render::Texture2D& OceanGpuResources::ping(std::uint32_t cascade,
                                                        std::uint32_t field) const {
    return field_texture_at(ping_, cascade, field, "ocean FFT ping texture");
}

const cubey::render::Texture2D& OceanGpuResources::pong(std::uint32_t cascade,
                                                        std::uint32_t field) const {
    return field_texture_at(pong_, cascade, field, "ocean FFT pong texture");
}

const cubey::render::Texture2D& OceanGpuResources::displacement(std::uint32_t cascade) const {
    return texture_at(displacement_, cascade, "ocean displacement texture");
}

const cubey::render::Texture2D& OceanGpuResources::normal(std::uint32_t cascade) const {
    return texture_at(normal_, cascade, "ocean normal texture");
}

const cubey::render::Texture2D& OceanGpuResources::foam(std::uint32_t cascade) const {
    return texture_at(foam_, cascade, "ocean foam texture");
}

const cubey::render::Texture2D& OceanGpuResources::texture_at(const TextureArray& textures,
                                                              std::uint32_t cascade,
                                                              const char* label) const {
    if (cascade >= textures.size() || !textures[cascade].has_value()) {
        throw std::runtime_error(label == nullptr ? "ocean texture is not initialized" : label);
    }
    return textures[cascade].value();
}

const cubey::render::Texture2D&
OceanGpuResources::field_texture_at(const FieldTextureArray& textures, std::uint32_t cascade,
                                    std::uint32_t field, const char* label) const {
    if (cascade >= kOceanCascadeCount || field >= kOceanSpectrumFieldCount) {
        throw std::runtime_error(label == nullptr ? "ocean field index out of range" : label);
    }
    const std::uint32_t index = field_texture_index(cascade, field);
    if (!textures[index].has_value()) {
        throw std::runtime_error(label == nullptr ? "ocean field texture is not initialized"
                                                  : label);
    }
    return textures[index].value();
}

VkDescriptorSet OceanGpuResources::descriptor_at(std::span<const VkDescriptorSet> sets,
                                                 std::uint32_t index, const char* label) const {
    if (index >= sets.size() || sets[index] == VK_NULL_HANDLE) {
        throw std::runtime_error(label == nullptr ? "ocean descriptor set is not initialized"
                                                  : label);
    }
    return sets[index];
}

} // namespace cubey::projects::ocean
