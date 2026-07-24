#include <cubey/engine/ocean_surface_runtime.h>

#include <cubey/render/material.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace cubey {
namespace {

using render::OceanFieldPrecision;

constexpr std::uint32_t kOceanGpuProfilerPassCapacity = 64U;
constexpr std::uint32_t kOceanSurfaceReflectionBinding = cubey::render::kOceanCascadeCount * 3U;
constexpr std::uint32_t kOceanSurfaceSkyRadianceBinding = kOceanSurfaceReflectionBinding + 1U;
constexpr std::uint32_t kOceanSurfaceTerrainFieldBinding = kOceanSurfaceSkyRadianceBinding + 1U;
constexpr std::uint32_t kOceanSurfaceTerrainFieldUniformBinding =
    kOceanSurfaceTerrainFieldBinding + 1U;
constexpr std::uint32_t kOceanSurfaceFeatureUniformBinding =
    kOceanSurfaceTerrainFieldUniformBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudShadowBinding =
    kOceanSurfaceFeatureUniformBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudEnvironmentPreviousBinding =
    kOceanSurfaceCloudShadowBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudEnvironmentCurrentBinding =
    kOceanSurfaceCloudEnvironmentPreviousBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudPlanarReflectionBinding =
    kOceanSurfaceCloudEnvironmentCurrentBinding + 1U;
constexpr std::uint32_t kOceanSurfaceReflectionCurrentBinding =
    kOceanSurfaceCloudPlanarReflectionBinding + 1U;
constexpr std::uint32_t kOceanSurfaceBindingCount = kOceanSurfaceReflectionCurrentBinding + 1U;

[[nodiscard]] std::filesystem::path shader_path(const std::filesystem::path& shader_dir,
                                                const char* filename) {
    return shader_dir / filename;
}

[[nodiscard]] std::uint32_t field_texture_index(std::uint32_t cascade, std::uint32_t field) {
    return cascade * cubey::render::kOceanSpectrumFieldCount + field;
}

[[nodiscard]] VkFormat ocean_field_format(OceanFieldPrecision precision) {
    switch (precision) {
    case OceanFieldPrecision::Full:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case OceanFieldPrecision::Half:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

[[nodiscard]] const char* ocean_precision_shader_suffix(OceanFieldPrecision precision) {
    switch (precision) {
    case OceanFieldPrecision::Full:
        return "";
    case OceanFieldPrecision::Half:
        return "_half";
    }
    return "";
}

[[nodiscard]] std::filesystem::path ocean_compute_shader_path(const std::filesystem::path& shader_dir,
                                                              const char* stem,
                                                              OceanFieldPrecision precision) {
    return shader_dir / (std::string(stem) + ocean_precision_shader_suffix(precision) + ".comp.spv");
}

void validate_ocean_field_format_support(const cubey::vulkan::Device& device,
                                         OceanFieldPrecision precision) {
    if (precision != OceanFieldPrecision::Half) {
        return;
    }
    constexpr VkFormatFeatureFlags kRequiredFormatFeatures =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (!device.supports_shader_storage_image_extended_formats() ||
        !device.supports_image_format_features(VK_FORMAT_R16G16B16A16_SFLOAT,
                                               kRequiredFormatFeatures)) {
        throw std::runtime_error(
            "ocean half field precision requires sampled rgba16f storage image support");
    }
}

[[nodiscard]] cubey::render::Texture2D make_ocean_field_texture(const cubey::vulkan::Device& device,
                                                                std::uint32_t resolution,
                                                                VkFormat format,
                                                                bool sampled) {
    return cubey::render::Texture2D(device,
                                    cubey::render::Texture2DConfig{
                                        .extent = {resolution, resolution},
                                        .format = format,
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

void OceanSurfaceRuntime::create(const cubey::vulkan::Device& device,
                                 const OceanSurfaceRuntimeCreateInfo& config) {
    reset();
    render::validate_ocean_config(config.ocean);
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
    validate_ocean_field_format_support(device, config.ocean.field_precision);
    create_textures(device, config.ocean);
    surface_feature_uniforms_.emplace(device, config.frame_slot_count);
    create_descriptor_sets(device, config.frame_slot_count);
    update_descriptors(device);
    create_pipelines(device, config);
    profiler_.emplace(device, config.frame_slot_count, kOceanGpuProfilerPassCapacity);
}

void OceanSurfaceRuntime::reset() {
    profiler_.reset();
    for (auto& pipeline : surface_pipelines_) {
        pipeline.reset();
    }
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
    fallback_field_.reset();
    cascade_allocated_ = {};
    cascade_resolutions_ = {};
    resolution_ = 0;
}

void OceanSurfaceRuntime::create_textures(const cubey::vulkan::Device& device,
                                        const cubey::render::OceanSurfaceConfig& config) {
    const VkFormat field_format = ocean_field_format(config.field_precision);
    fallback_field_.emplace(make_ocean_field_texture(device, 1U, field_format, true));
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        cascade_allocated_[cascade] = cubey::render::ocean_cascade_enabled(config, cascade);
        cascade_resolutions_[cascade] = cubey::render::ocean_cascade_map_size(config, cascade);
        if (!cascade_allocated_[cascade]) {
            continue;
        }
        const std::uint32_t map_size = cascade_resolutions_[cascade];
        h0_[cascade].emplace(make_ocean_field_texture(device, map_size, field_format, false));
        for (std::uint32_t field = 0; field < cubey::render::kOceanSpectrumFieldCount; ++field) {
            const std::uint32_t index = field_texture_index(cascade, field);
            fields_[index].emplace(
                make_ocean_field_texture(device, map_size, field_format, false));
            ping_[index].emplace(
                make_ocean_field_texture(device, map_size, field_format, false));
            pong_[index].emplace(
                make_ocean_field_texture(device, map_size, field_format, false));
        }
        displacement_[cascade].emplace(
            make_ocean_field_texture(device, map_size, field_format, true));
        normal_[cascade].emplace(
            make_ocean_field_texture(device, map_size, field_format, true));
        foam_[cascade].emplace(make_ocean_field_texture(device, map_size, field_format, true));
    }
}

void OceanSurfaceRuntime::create_descriptor_sets(const cubey::vulkan::Device& device,
                                               std::uint32_t frame_slot_count) {
    const std::array spectrum_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo spectrum_info =
        descriptor_info(spectrum_bindings, cubey::render::kOceanCascadeCount);
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
    };
    const cubey::vulkan::DescriptorSetInfo modulate_info =
        descriptor_info(modulate_bindings, cubey::render::kOceanCascadeCount);
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
    };
    const cubey::vulkan::DescriptorSetInfo unpack_info =
        descriptor_info(unpack_bindings, cubey::render::kOceanCascadeCount);
    unpack_layout_.emplace(device, unpack_info.layout_info());
    unpack_pool_.emplace(device, unpack_info.pool_info());
    for (VkDescriptorSet& set : unpack_sets_) {
        set = unpack_pool_->allocate(unpack_layout_->handle());
    }

    std::array<cubey::vulkan::DescriptorSetBindingConfig, kOceanSurfaceBindingCount>
        surface_bindings{};
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        surface_bindings[cascade] = cubey::vulkan::DescriptorSetBindingConfig{
            .binding = cascade,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        surface_bindings[cubey::render::kOceanCascadeCount + cascade] = cubey::vulkan::DescriptorSetBindingConfig{
            .binding = cubey::render::kOceanCascadeCount + cascade,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        surface_bindings[cubey::render::kOceanCascadeCount * 2U + cascade] =
            cubey::vulkan::DescriptorSetBindingConfig{
                .binding = cubey::render::kOceanCascadeCount * 2U + cascade,
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
    surface_bindings[kOceanSurfaceCloudShadowBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceCloudShadowBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceCloudEnvironmentPreviousBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceCloudEnvironmentPreviousBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceCloudEnvironmentCurrentBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceCloudEnvironmentCurrentBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceCloudPlanarReflectionBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceCloudPlanarReflectionBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceReflectionCurrentBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceReflectionCurrentBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
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

void OceanSurfaceRuntime::update_descriptors(const cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch writes;
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        const cubey::render::Texture2D& displacement_texture =
            cascade_allocated(cascade) ? displacement(cascade) : fallback_field();
        const cubey::render::Texture2D& normal_texture =
            cascade_allocated(cascade) ? normal(cascade) : fallback_field();
        const cubey::render::Texture2D& foam_texture =
            cascade_allocated(cascade) ? foam(cascade) : fallback_field();

        for (VkDescriptorSet surface_set : surface_sets_) {
            writes.combined_image_sampler(surface_set, cascade,
                                          displacement_texture.sampler().handle(),
                                          displacement_texture.view(), VK_IMAGE_LAYOUT_GENERAL);
            writes.combined_image_sampler(surface_set, cascade + cubey::render::kOceanCascadeCount,
                                          normal_texture.sampler().handle(), normal_texture.view(),
                                          VK_IMAGE_LAYOUT_GENERAL);
            writes.combined_image_sampler(surface_set, cascade + cubey::render::kOceanCascadeCount * 2U,
                                          foam_texture.sampler().handle(), foam_texture.view(),
                                          VK_IMAGE_LAYOUT_GENERAL);
        }
        if (!cascade_allocated(cascade)) {
            continue;
        }

        writes.storage_image(spectrum_set(cascade), 0, h0(cascade).view())
            .storage_image(modulate_set(cascade), 0, h0(cascade).view())
            .storage_image(modulate_set(cascade), 1, field(cascade, 0).view())
            .storage_image(modulate_set(cascade), 2, field(cascade, 1).view())
            .storage_image(unpack_set(cascade), 0, pong(cascade, 0).view())
            .storage_image(unpack_set(cascade), 1, pong(cascade, 1).view())
            .storage_image(unpack_set(cascade), 2, displacement(cascade).view())
            .storage_image(unpack_set(cascade), 3, normal(cascade).view())
            .storage_image(unpack_set(cascade), 4, foam(cascade).view());

        for (std::uint32_t field_index = 0; field_index < cubey::render::kOceanSpectrumFieldCount; ++field_index) {
            const std::uint32_t base_fft_set =
                (cascade * cubey::render::kOceanSpectrumFieldCount + field_index) * 3U;
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

void OceanSurfaceRuntime::update_atmosphere_probe_descriptors(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    const cubey::render::TextureCube& previous, const cubey::render::TextureCube& current,
    const cubey::render::TextureCube& sky_radiance) {
    if (surface_sets_.empty()) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    cubey::vulkan::DescriptorWriteBatch writes;
    const VkDescriptorSet set = surface_set(frame_slot);
    writes
        .combined_image_sampler(set, kOceanSurfaceReflectionBinding, previous.sampler().handle(),
                                previous.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceReflectionCurrentBinding,
                                current.sampler().handle(), current.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceSkyRadianceBinding,
                                sky_radiance.sampler().handle(), sky_radiance.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceCloudEnvironmentPreviousBinding,
                                current.sampler().handle(), current.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceCloudEnvironmentCurrentBinding,
                                current.sampler().handle(), current.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .update(device);
}

void OceanSurfaceRuntime::update_terrain_ocean_field_descriptor(
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

void OceanSurfaceRuntime::update_terrain_ocean_field_uniform_descriptor(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot, VkBuffer buffer,
    VkDeviceSize range) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .uniform_buffer(surface_set(frame_slot), kOceanSurfaceTerrainFieldUniformBinding, buffer,
                        range)
        .update(device);
}

void OceanSurfaceRuntime::update_cloud_shadow_descriptor(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot, VkSampler sampler,
    VkImageView image_view, VkImageLayout image_layout) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(surface_set(frame_slot), kOceanSurfaceCloudShadowBinding, sampler,
                                image_view, image_layout)
        .update(device);
}

void OceanSurfaceRuntime::update_cloud_environment_descriptors(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    const cubey::render::TextureCube& previous, const cubey::render::TextureCube& current) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(surface_set(frame_slot),
                                kOceanSurfaceCloudEnvironmentPreviousBinding,
                                previous.sampler().handle(), previous.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(surface_set(frame_slot),
                                kOceanSurfaceCloudEnvironmentCurrentBinding,
                                current.sampler().handle(), current.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .update(device);
}

void OceanSurfaceRuntime::update_cloud_planar_reflection_descriptor(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    const cubey::render::Texture2D& texture) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(surface_set(frame_slot),
                                kOceanSurfaceCloudPlanarReflectionBinding,
                                texture.sampler().handle(), texture.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .update(device);
}

void OceanSurfaceRuntime::upload_surface_feature_uniforms(
    cubey::render::FrameSlot frame_slot, const OceanSurfaceFeatureUniforms& uniforms) const {
    if (!surface_feature_uniforms_.has_value()) {
        throw std::runtime_error("ocean surface feature uniforms are not initialized");
    }
    surface_feature_uniforms_->upload(frame_slot, uniforms);
}

void OceanSurfaceRuntime::create_pipelines(const cubey::vulkan::Device& device,
                                         const OceanSurfaceRuntimeCreateInfo& config) {
    const VkPushConstantRange spectrum_push_constants = compute_push_constant_range(16U);
    const VkPushConstantRange modulate_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange fft_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange unpack_push_constants = compute_push_constant_range(8U);

    const std::array spectrum_layouts{spectrum_layout_->handle()};
    spectrum_pipeline_.emplace(device,
                               cubey::render::ComputePipelineResourceConfig{
                                   .shader_stage = cubey::render::compute_shader_file(
                                       ocean_compute_shader_path(config.shader_dir,
                                                                 "ocean_spectrum",
                                                                 config.ocean.field_precision)),
                                   .descriptor_set_layouts = spectrum_layouts,
                                   .push_constants = {&spectrum_push_constants, 1},
                               });

    const std::array modulate_layouts{modulate_layout_->handle()};
    modulate_pipeline_.emplace(device,
                               cubey::render::ComputePipelineResourceConfig{
                                   .shader_stage = cubey::render::compute_shader_file(
                                       ocean_compute_shader_path(config.shader_dir,
                                                                 "ocean_modulate",
                                                                 config.ocean.field_precision)),
                                   .descriptor_set_layouts = modulate_layouts,
                                   .push_constants = {&modulate_push_constants, 1},
                               });

    const std::array fft_layouts{fft_layout_->handle()};
    fft_pipeline_.emplace(device, cubey::render::ComputePipelineResourceConfig{
                                      .shader_stage = cubey::render::compute_shader_file(
                                          ocean_compute_shader_path(config.shader_dir, "ocean_fft",
                                                                    config.ocean.field_precision)),
                                      .descriptor_set_layouts = fft_layouts,
                                      .push_constants = {&fft_push_constants, 1},
                                  });

    const std::array unpack_layouts{unpack_layout_->handle()};
    unpack_pipeline_.emplace(device,
                             cubey::render::ComputePipelineResourceConfig{
                                 .shader_stage = cubey::render::compute_shader_file(
                                     ocean_compute_shader_path(config.shader_dir, "ocean_unpack",
                                                               config.ocean.field_precision)),
                                 .descriptor_set_layouts = unpack_layouts,
                                 .push_constants = {&unpack_push_constants, 1},
                             });

    const std::array surface_layouts{surface_layout_->handle()};
    for (std::size_t index = 0; index < surface_pipelines_.size(); ++index) {
        const std::filesystem::path fragment_path =
            index == 0U
                ? shader_path(config.shader_dir, "ocean.frag.spv")
                : config.shader_dir / "filters" /
                      (index == 1U ? std::filesystem::path("bilinear")
                                   : std::filesystem::path("bicubic")) /
                      "ocean.frag.spv";
        const std::array surface_shader_stage_files{
            cubey::render::vertex_shader_file(shader_path(config.shader_dir, "ocean.vert.spv")),
            cubey::render::fragment_shader_file(fragment_path),
        };
        surface_pipelines_[index].emplace(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = config.target_extent,
                        .color_format = config.color_format,
                        .depth_format = config.depth_format,
                        .shader_stage_files = surface_shader_stage_files,
                        .descriptor_set_layouts = surface_layouts,
                        .material_pass = ocean_surface_pass_info(),
                    });
    }
}

const cubey::render::GraphicsPipelineResource&
OceanSurfaceRuntime::surface_pipeline(cubey::render::OceanDetailFilter filter) const {
    const std::size_t index = static_cast<std::size_t>(filter);
    if (index >= surface_pipelines_.size() || !surface_pipelines_[index].has_value()) {
        throw std::runtime_error("ocean surface pipeline is not initialized");
    }
    return surface_pipelines_[index].value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::spectrum_pipeline() const {
    if (!spectrum_pipeline_.has_value()) {
        throw std::runtime_error("ocean spectrum pipeline is not initialized");
    }
    return spectrum_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::modulate_pipeline() const {
    if (!modulate_pipeline_.has_value()) {
        throw std::runtime_error("ocean modulate pipeline is not initialized");
    }
    return modulate_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::fft_pipeline() const {
    if (!fft_pipeline_.has_value()) {
        throw std::runtime_error("ocean FFT pipeline is not initialized");
    }
    return fft_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::unpack_pipeline() const {
    if (!unpack_pipeline_.has_value()) {
        throw std::runtime_error("ocean unpack pipeline is not initialized");
    }
    return unpack_pipeline_.value();
}

VkDescriptorSet OceanSurfaceRuntime::spectrum_set(std::uint32_t cascade) const {
    return descriptor_at(spectrum_sets_, cascade, "ocean spectrum descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::modulate_set(std::uint32_t cascade) const {
    return descriptor_at(modulate_sets_, cascade, "ocean modulate descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::fft_set(std::uint32_t cascade, std::uint32_t field,
                                           std::uint32_t set_index) const {
    if (field >= cubey::render::kOceanSpectrumFieldCount || set_index >= 3U) {
        throw std::runtime_error("ocean FFT descriptor index out of range");
    }
    return descriptor_at(fft_sets_, (cascade * cubey::render::kOceanSpectrumFieldCount + field) * 3U + set_index,
                         "ocean FFT descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::unpack_set(std::uint32_t cascade) const {
    return descriptor_at(unpack_sets_, cascade, "ocean unpack descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::surface_set(cubey::render::FrameSlot frame_slot) const {
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

const cubey::render::Texture2D& OceanSurfaceRuntime::h0(std::uint32_t cascade) const {
    return texture_at(h0_, cascade, "ocean h0 texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::field(std::uint32_t cascade,
                                                         std::uint32_t field) const {
    return field_texture_at(fields_, cascade, field, "ocean spectrum field texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::ping(std::uint32_t cascade,
                                                        std::uint32_t field) const {
    return field_texture_at(ping_, cascade, field, "ocean FFT ping texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::pong(std::uint32_t cascade,
                                                        std::uint32_t field) const {
    return field_texture_at(pong_, cascade, field, "ocean FFT pong texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::displacement(std::uint32_t cascade) const {
    return texture_at(displacement_, cascade, "ocean displacement texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::normal(std::uint32_t cascade) const {
    return texture_at(normal_, cascade, "ocean normal texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::foam(std::uint32_t cascade) const {
    return texture_at(foam_, cascade, "ocean foam texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::fallback_field() const {
    if (!fallback_field_.has_value()) {
        throw std::runtime_error("ocean fallback field texture is not initialized");
    }
    return fallback_field_.value();
}

bool OceanSurfaceRuntime::cascade_allocated(std::uint32_t cascade) const {
    if (cascade >= cubey::render::kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return cascade_allocated_[cascade];
}

std::uint32_t OceanSurfaceRuntime::cascade_resolution(std::uint32_t cascade) const {
    if (cascade >= cubey::render::kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return cascade_resolutions_[cascade];
}

const std::vector<cubey::vulkan::GpuPassTiming>& OceanSurfaceRuntime::latest_timings() const {
    static const std::vector<cubey::vulkan::GpuPassTiming> kEmptyTimings;
    if (!profiler_.has_value()) {
        return kEmptyTimings;
    }
    return profiler_->latest_timings();
}

const cubey::render::Texture2D& OceanSurfaceRuntime::texture_at(const TextureArray& textures,
                                                              std::uint32_t cascade,
                                                              const char* label) const {
    if (cascade >= textures.size() || !textures[cascade].has_value()) {
        throw std::runtime_error(label == nullptr ? "ocean texture is not initialized" : label);
    }
    return textures[cascade].value();
}

const cubey::render::Texture2D&
OceanSurfaceRuntime::field_texture_at(const FieldTextureArray& textures, std::uint32_t cascade,
                                    std::uint32_t field, const char* label) const {
    if (cascade >= cubey::render::kOceanCascadeCount || field >= cubey::render::kOceanSpectrumFieldCount) {
        throw std::runtime_error(label == nullptr ? "ocean field index out of range" : label);
    }
    const std::uint32_t index = field_texture_index(cascade, field);
    if (!textures[index].has_value()) {
        throw std::runtime_error(label == nullptr ? "ocean field texture is not initialized"
                                                  : label);
    }
    return textures[index].value();
}

VkDescriptorSet OceanSurfaceRuntime::descriptor_at(std::span<const VkDescriptorSet> sets,
                                                 std::uint32_t index, const char* label) const {
    if (index >= sets.size() || sets[index] == VK_NULL_HANDLE) {
        throw std::runtime_error(label == nullptr ? "ocean descriptor set is not initialized"
                                                  : label);
    }
    return sets[index];
}

} // namespace cubey
