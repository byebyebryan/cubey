#include "ocean_gpu_resources.h"

#include <cubey/render/material.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace cubey::projects::ocean {
namespace {

constexpr VkFormat kOceanFieldFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

[[nodiscard]] std::filesystem::path shader_path(const std::filesystem::path& shader_dir,
                                                const char* filename) {
    return shader_dir / filename;
}

[[nodiscard]] cubey::render::Texture2D make_ocean_field_texture(const cubey::vulkan::Device& device,
                                                                std::uint32_t resolution,
                                                                bool sampled = true) {
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
        .depth_test = false,
        .depth_write = false,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo ocean_sky_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 20U,
    };
    return {
        .label = "ocean.sky",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo ocean_scene_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 36U,
    };
    return {
        .label = "ocean.scene",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_ALWAYS,
        .blend_enable = false,
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

[[nodiscard]] std::uint32_t spectrum_texture_index(std::uint32_t cascade, std::uint32_t field) {
    return cascade * kOceanSpectrumFieldCount + field;
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
    if (config.scene_depth_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("ocean scene pipeline requires a depth format");
    }
    if (config.target_extent.width == 0 || config.target_extent.height == 0) {
        throw std::runtime_error("ocean surface pipeline requires a target extent");
    }
    if (config.frame_slot_count == 0) {
        throw std::runtime_error("ocean GPU resources require at least one frame slot");
    }

    resolution_ = config.ocean.spectrum_resolution;
    create_textures(device, config.ocean);
    create_descriptor_sets(device, config.frame_slot_count);
    update_descriptors(device);
    create_pipelines(device, config);
}

void OceanGpuResources::reset() {
    surface_pipeline_.reset();
    sky_pipeline_.reset();
    scene_pipeline_.reset();
    foam_update_pipeline_.reset();
    detail_pipeline_.reset();
    finalize_pipeline_.reset();
    fft_pipeline_.reset();
    spectrum_evolve_pipeline_.reset();
    spectrum_init_pipeline_.reset();

    surface_pool_.reset();
    surface_layout_.reset();
    scene_sampler_.reset();
    scene_pool_.reset();
    scene_layout_.reset();
    scene_sets_.clear();
    foam_update_pool_.reset();
    foam_update_layout_.reset();
    detail_pool_.reset();
    detail_layout_.reset();
    finalize_pool_.reset();
    finalize_layout_.reset();
    fft_pool_.reset();
    fft_layout_.reset();
    spectrum_evolve_pool_.reset();
    spectrum_evolve_layout_.reset();
    spectrum_init_pool_.reset();
    spectrum_init_layout_.reset();
    surface_set_ = VK_NULL_HANDLE;
    spectrum_init_sets_ = {};
    spectrum_evolve_sets_ = {};
    fft_sets_ = {};
    finalize_sets_ = {};
    detail_sets_ = {};
    foam_update_sets_ = {};

    foam_history_b_ = {};
    foam_history_a_ = {};
    detail_normal_foam_ = {};
    normal_foam_ = {};
    displacement_ = {};
    pong_ = {};
    ping_ = {};
    spectrum_ = {};
    h0_ = {};
    resolution_ = 0;
}

void OceanGpuResources::create_textures(const cubey::vulkan::Device& device,
                                        const OceanConfig& config) {
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        h0_[cascade].emplace(make_ocean_field_texture(device, config.spectrum_resolution, false));
        for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
            const std::uint32_t index = spectrum_texture_index(cascade, field);
            spectrum_[index].emplace(
                make_ocean_field_texture(device, config.spectrum_resolution, false));
            ping_[index].emplace(
                make_ocean_field_texture(device, config.spectrum_resolution, false));
            pong_[index].emplace(
                make_ocean_field_texture(device, config.spectrum_resolution, false));
        }
        displacement_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, true));
        normal_foam_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, true));
        detail_normal_foam_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, true));
        foam_history_a_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, true));
        foam_history_b_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, true));
    }
}

void OceanGpuResources::create_descriptor_sets(const cubey::vulkan::Device& device,
                                               std::uint32_t frame_slot_count) {
    if (frame_slot_count == 0) {
        throw std::runtime_error("ocean descriptor sets require at least one frame slot");
    }
    scene_sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                       .min_filter = VK_FILTER_LINEAR,
                                       .mag_filter = VK_FILTER_LINEAR,
                                       .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   });

    const std::array init_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo init_info =
        descriptor_info(init_bindings, kOceanCascadeCount);
    spectrum_init_layout_.emplace(device, init_info.layout_info());
    spectrum_init_pool_.emplace(device, init_info.pool_info());
    for (VkDescriptorSet& set : spectrum_init_sets_) {
        set = spectrum_init_pool_->allocate(spectrum_init_layout_->handle());
    }

    const std::array evolve_bindings{
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
    };
    const cubey::vulkan::DescriptorSetInfo evolve_info =
        descriptor_info(evolve_bindings, kOceanCascadeCount);
    spectrum_evolve_layout_.emplace(device, evolve_info.layout_info());
    spectrum_evolve_pool_.emplace(device, evolve_info.pool_info());
    for (VkDescriptorSet& set : spectrum_evolve_sets_) {
        set = spectrum_evolve_pool_->allocate(spectrum_evolve_layout_->handle());
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

    const std::array finalize_bindings{
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
    const cubey::vulkan::DescriptorSetInfo finalize_info =
        descriptor_info(finalize_bindings, kOceanCascadeCount);
    finalize_layout_.emplace(device, finalize_info.layout_info());
    finalize_pool_.emplace(device, finalize_info.pool_info());
    for (VkDescriptorSet& set : finalize_sets_) {
        set = finalize_pool_->allocate(finalize_layout_->handle());
    }

    const std::array detail_bindings{
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
    const cubey::vulkan::DescriptorSetInfo detail_info =
        descriptor_info(detail_bindings, kOceanCascadeCount);
    detail_layout_.emplace(device, detail_info.layout_info());
    detail_pool_.emplace(device, detail_info.pool_info());
    for (VkDescriptorSet& set : detail_sets_) {
        set = detail_pool_->allocate(detail_layout_->handle());
    }

    const std::array foam_update_bindings{
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
    const cubey::vulkan::DescriptorSetInfo foam_update_info =
        descriptor_info(foam_update_bindings, static_cast<std::uint32_t>(foam_update_sets_.size()));
    foam_update_layout_.emplace(device, foam_update_info.layout_info());
    foam_update_pool_.emplace(device, foam_update_info.pool_info());
    for (VkDescriptorSet& set : foam_update_sets_) {
        set = foam_update_pool_->allocate(foam_update_layout_->handle());
    }

    const std::array surface_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 3,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 4,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 5,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 6,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 7,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 8,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 9,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 10,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 11,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 12,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 13,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 14,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo surface_info = descriptor_info(surface_bindings, 1);
    surface_layout_.emplace(device, surface_info.layout_info());
    surface_pool_.emplace(device, surface_info.pool_info());
    surface_set_ = surface_pool_->allocate(surface_layout_->handle());

    const std::array scene_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo scene_info =
        descriptor_info(scene_bindings, frame_slot_count);
    scene_layout_.emplace(device, scene_info.layout_info());
    scene_pool_.emplace(device, scene_info.pool_info());
    scene_sets_.resize(frame_slot_count);
    for (VkDescriptorSet& set : scene_sets_) {
        set = scene_pool_->allocate(scene_layout_->handle());
    }
}

void OceanGpuResources::update_descriptors(const cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch writes;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        writes.storage_image(spectrum_init_set(cascade), 0, h0(cascade).view())
            .storage_image(spectrum_evolve_set(cascade), 0, h0(cascade).view())
            .storage_image(spectrum_evolve_set(cascade), 1, spectrum(cascade, 0).view())
            .storage_image(spectrum_evolve_set(cascade), 2, spectrum(cascade, 1).view())
            .storage_image(spectrum_evolve_set(cascade), 3, spectrum(cascade, 2).view())
            .storage_image(finalize_set(cascade), 0, pong(cascade, 0).view())
            .storage_image(finalize_set(cascade), 1, pong(cascade, 1).view())
            .storage_image(finalize_set(cascade), 2, pong(cascade, 2).view())
            .storage_image(finalize_set(cascade), 3, displacement(cascade).view())
            .storage_image(finalize_set(cascade), 4, normal_foam(cascade).view())
            .storage_image(detail_set(cascade), 0, normal_foam(cascade).view())
            .storage_image(detail_set(cascade), 1, detail_normal_foam(cascade).view());
        writes.storage_image(foam_update_set(cascade, 0), 0, detail_normal_foam(cascade).view())
            .storage_image(foam_update_set(cascade, 0), 1, foam_history(cascade, 0).view())
            .storage_image(foam_update_set(cascade, 0), 2, foam_history(cascade, 1).view())
            .storage_image(foam_update_set(cascade, 1), 0, detail_normal_foam(cascade).view())
            .storage_image(foam_update_set(cascade, 1), 1, foam_history(cascade, 1).view())
            .storage_image(foam_update_set(cascade, 1), 2, foam_history(cascade, 0).view());

        for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
            const std::uint32_t base_fft_set = (cascade * kOceanSpectrumFieldCount + field) * 3U;
            writes.storage_image(fft_sets_[base_fft_set + 0U], 0, spectrum(cascade, field).view())
                .storage_image(fft_sets_[base_fft_set + 0U], 1, ping(cascade, field).view())
                .storage_image(fft_sets_[base_fft_set + 1U], 0, ping(cascade, field).view())
                .storage_image(fft_sets_[base_fft_set + 1U], 1, pong(cascade, field).view())
                .storage_image(fft_sets_[base_fft_set + 2U], 0, pong(cascade, field).view())
                .storage_image(fft_sets_[base_fft_set + 2U], 1, ping(cascade, field).view());
        }

        writes
            .combined_image_sampler(surface_set_, cascade, displacement(cascade).sampler().handle(),
                                    displacement(cascade).view(), VK_IMAGE_LAYOUT_GENERAL)
            .combined_image_sampler(surface_set_, cascade + kOceanCascadeCount,
                                    normal_foam(cascade).sampler().handle(),
                                    normal_foam(cascade).view(), VK_IMAGE_LAYOUT_GENERAL)
            .combined_image_sampler(surface_set_, cascade + (kOceanCascadeCount * 2U),
                                    foam_history(cascade, 0).sampler().handle(),
                                    foam_history(cascade, 0).view(), VK_IMAGE_LAYOUT_GENERAL)
            .combined_image_sampler(surface_set_, cascade + (kOceanCascadeCount * 3U),
                                    foam_history(cascade, 1).sampler().handle(),
                                    foam_history(cascade, 1).view(), VK_IMAGE_LAYOUT_GENERAL)
            .combined_image_sampler(surface_set_, cascade + (kOceanCascadeCount * 4U),
                                    detail_normal_foam(cascade).sampler().handle(),
                                    detail_normal_foam(cascade).view(), VK_IMAGE_LAYOUT_GENERAL);
    }
    writes.update(device);
}

void OceanGpuResources::create_pipelines(const cubey::vulkan::Device& device,
                                         const OceanGpuResourceConfig& config) {
    const VkPushConstantRange spectrum_push_constants = compute_push_constant_range(16U);
    const VkPushConstantRange fft_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange finalize_push_constants = compute_push_constant_range(16U);
    const VkPushConstantRange detail_push_constants = compute_push_constant_range(16U);
    const VkPushConstantRange foam_update_push_constants = compute_push_constant_range(8U);

    const std::array init_layouts{spectrum_init_layout_->handle()};
    spectrum_init_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(
                        shader_path(config.shader_dir, "ocean_spectrum_init.comp.spv")),
                    .descriptor_set_layouts = init_layouts,
                    .push_constants = {&spectrum_push_constants, 1},
                });

    const std::array evolve_layouts{spectrum_evolve_layout_->handle()};
    spectrum_evolve_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(
                        shader_path(config.shader_dir, "ocean_spectrum_evolve.comp.spv")),
                    .descriptor_set_layouts = evolve_layouts,
                    .push_constants = {&spectrum_push_constants, 1},
                });

    const std::array fft_layouts{fft_layout_->handle()};
    fft_pipeline_.emplace(device, cubey::render::ComputePipelineResourceConfig{
                                      .shader_stage = cubey::render::compute_shader_file(
                                          shader_path(config.shader_dir, "ocean_fft.comp.spv")),
                                      .descriptor_set_layouts = fft_layouts,
                                      .push_constants = {&fft_push_constants, 1},
                                  });

    const std::array finalize_layouts{finalize_layout_->handle()};
    finalize_pipeline_.emplace(device,
                               cubey::render::ComputePipelineResourceConfig{
                                   .shader_stage = cubey::render::compute_shader_file(
                                       shader_path(config.shader_dir, "ocean_finalize.comp.spv")),
                                   .descriptor_set_layouts = finalize_layouts,
                                   .push_constants = {&finalize_push_constants, 1},
                               });

    const std::array detail_layouts{detail_layout_->handle()};
    detail_pipeline_.emplace(device,
                             cubey::render::ComputePipelineResourceConfig{
                                 .shader_stage = cubey::render::compute_shader_file(
                                     shader_path(config.shader_dir, "ocean_detail.comp.spv")),
                                 .descriptor_set_layouts = detail_layouts,
                                 .push_constants = {&detail_push_constants, 1},
                             });

    const std::array foam_update_layouts{foam_update_layout_->handle()};
    foam_update_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(
                        shader_path(config.shader_dir, "ocean_foam_update.comp.spv")),
                    .descriptor_set_layouts = foam_update_layouts,
                    .push_constants = {&foam_update_push_constants, 1},
                });

    const std::array shader_stage_files{
        cubey::render::vertex_shader_file(shader_path(config.shader_dir, "ocean.vert.spv")),
        cubey::render::fragment_shader_file(shader_path(config.shader_dir, "ocean.frag.spv")),
    };
    const std::array surface_scene_layouts{surface_layout_->handle(), scene_layout_->handle()};
    const std::array sky_shader_stage_files{
        cubey::render::vertex_shader_file(shader_path(config.shader_dir, "ocean_sky.vert.spv")),
        cubey::render::fragment_shader_file(shader_path(config.shader_dir, "ocean_sky.frag.spv")),
    };
    const std::array scene_shader_stage_files{
        cubey::render::vertex_shader_file(shader_path(config.shader_dir, "ocean_sky.vert.spv")),
        cubey::render::fragment_shader_file(shader_path(config.shader_dir, "ocean_scene.frag.spv")),
    };
    sky_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                      .extent = config.target_extent,
                                      .color_format = config.color_format,
                                      .shader_stage_files = sky_shader_stage_files,
                                      .material_pass = ocean_sky_pass_info(),
                                  });
    scene_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                        .extent = config.target_extent,
                                        .color_format = config.color_format,
                                        .depth_format = config.scene_depth_format,
                                        .shader_stage_files = scene_shader_stage_files,
                                        .material_pass = ocean_scene_pass_info(),
                                    });
    surface_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                          .extent = config.target_extent,
                                          .color_format = config.color_format,
                                          .shader_stage_files = shader_stage_files,
                                          .descriptor_set_layouts = surface_scene_layouts,
                                          .material_pass = ocean_surface_pass_info(),
                                      });
}

const cubey::render::GraphicsPipelineResource& OceanGpuResources::sky_pipeline() const {
    if (!sky_pipeline_.has_value()) {
        throw std::runtime_error("ocean sky pipeline is not initialized");
    }
    return sky_pipeline_.value();
}

const cubey::render::GraphicsPipelineResource& OceanGpuResources::scene_pipeline() const {
    if (!scene_pipeline_.has_value()) {
        throw std::runtime_error("ocean scene pipeline is not initialized");
    }
    return scene_pipeline_.value();
}

const cubey::render::GraphicsPipelineResource& OceanGpuResources::surface_pipeline() const {
    if (!surface_pipeline_.has_value()) {
        throw std::runtime_error("ocean surface pipeline is not initialized");
    }
    return surface_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::spectrum_init_pipeline() const {
    if (!spectrum_init_pipeline_.has_value()) {
        throw std::runtime_error("ocean spectrum init pipeline is not initialized");
    }
    return spectrum_init_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::spectrum_evolve_pipeline() const {
    if (!spectrum_evolve_pipeline_.has_value()) {
        throw std::runtime_error("ocean spectrum evolve pipeline is not initialized");
    }
    return spectrum_evolve_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::fft_pipeline() const {
    if (!fft_pipeline_.has_value()) {
        throw std::runtime_error("ocean FFT pipeline is not initialized");
    }
    return fft_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::finalize_pipeline() const {
    if (!finalize_pipeline_.has_value()) {
        throw std::runtime_error("ocean finalize pipeline is not initialized");
    }
    return finalize_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::detail_pipeline() const {
    if (!detail_pipeline_.has_value()) {
        throw std::runtime_error("ocean detail pipeline is not initialized");
    }
    return detail_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanGpuResources::foam_update_pipeline() const {
    if (!foam_update_pipeline_.has_value()) {
        throw std::runtime_error("ocean foam update pipeline is not initialized");
    }
    return foam_update_pipeline_.value();
}

VkDescriptorSet OceanGpuResources::spectrum_init_set(std::uint32_t cascade) const {
    return descriptor_at(spectrum_init_sets_, cascade, "ocean spectrum init descriptor set");
}

VkDescriptorSet OceanGpuResources::spectrum_evolve_set(std::uint32_t cascade) const {
    return descriptor_at(spectrum_evolve_sets_, cascade, "ocean spectrum evolve descriptor set");
}

VkDescriptorSet OceanGpuResources::fft_set(std::uint32_t cascade, std::uint32_t field,
                                           std::uint32_t set_index) const {
    if (set_index >= 3U) {
        throw std::runtime_error("ocean FFT descriptor set index out of range");
    }
    if (field >= kOceanSpectrumFieldCount) {
        throw std::runtime_error("ocean FFT field index out of range");
    }
    return descriptor_at(fft_sets_, (cascade * kOceanSpectrumFieldCount + field) * 3U + set_index,
                         "ocean FFT descriptor set");
}

VkDescriptorSet OceanGpuResources::finalize_set(std::uint32_t cascade) const {
    return descriptor_at(finalize_sets_, cascade, "ocean finalize descriptor set");
}

VkDescriptorSet OceanGpuResources::detail_set(std::uint32_t cascade) const {
    return descriptor_at(detail_sets_, cascade, "ocean detail descriptor set");
}

VkDescriptorSet OceanGpuResources::foam_update_set(std::uint32_t cascade,
                                                   std::uint32_t history_index) const {
    if (history_index >= 2U) {
        throw std::runtime_error("ocean foam history index out of range");
    }
    return descriptor_at(foam_update_sets_, cascade * 2U + history_index,
                         "ocean foam update descriptor set");
}

VkDescriptorSet OceanGpuResources::surface_set() const {
    if (surface_set_ == VK_NULL_HANDLE) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    return surface_set_;
}

VkDescriptorSet OceanGpuResources::scene_set(cubey::render::FrameSlot frame_slot) const {
    cubey::render::validate_frame_slot(frame_slot);
    if (static_cast<std::size_t>(frame_slot.count) != scene_sets_.size()) {
        throw std::runtime_error("ocean scene descriptor frame slot count does not match");
    }
    return descriptor_at(scene_sets_, frame_slot.index, "ocean scene descriptor set");
}

void OceanGpuResources::update_scene_descriptors(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    cubey::render::RenderGraphSampledTextureView scene_color,
    cubey::render::RenderGraphSampledTextureView scene_depth) const {
    if (!scene_sampler_.has_value()) {
        throw std::runtime_error("ocean scene sampler is not initialized");
    }
    const cubey::vulkan::Sampler& sampler = scene_sampler_.value();
    const VkDescriptorSet set = scene_set(frame_slot);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.combined_image_sampler(set, 0, sampler.handle(), scene_color.view, scene_color.layout)
        .combined_image_sampler(set, 1, sampler.handle(), scene_depth.view, scene_depth.layout);
    writes.update(device);
}

const cubey::render::Texture2D& OceanGpuResources::h0(std::uint32_t cascade) const {
    return texture_at(h0_, cascade, "ocean h0 texture");
}

const cubey::render::Texture2D& OceanGpuResources::spectrum(std::uint32_t cascade,
                                                            std::uint32_t field) const {
    return spectrum_texture_at(spectrum_, cascade, field, "ocean spectrum texture");
}

const cubey::render::Texture2D& OceanGpuResources::ping(std::uint32_t cascade,
                                                        std::uint32_t field) const {
    return spectrum_texture_at(ping_, cascade, field, "ocean ping texture");
}

const cubey::render::Texture2D& OceanGpuResources::pong(std::uint32_t cascade,
                                                        std::uint32_t field) const {
    return spectrum_texture_at(pong_, cascade, field, "ocean pong texture");
}

const cubey::render::Texture2D& OceanGpuResources::foam_history(std::uint32_t cascade,
                                                                std::uint32_t history_index) const {
    if (history_index == 0U) {
        return texture_at(foam_history_a_, cascade, "ocean foam history texture");
    }
    if (history_index == 1U) {
        return texture_at(foam_history_b_, cascade, "ocean foam history texture");
    }
    throw std::runtime_error("ocean foam history index out of range");
}

const cubey::render::Texture2D& OceanGpuResources::displacement(std::uint32_t cascade) const {
    return texture_at(displacement_, cascade, "ocean displacement texture");
}

const cubey::render::Texture2D& OceanGpuResources::normal_foam(std::uint32_t cascade) const {
    return texture_at(normal_foam_, cascade, "ocean normal foam texture");
}

const cubey::render::Texture2D& OceanGpuResources::detail_normal_foam(std::uint32_t cascade) const {
    return texture_at(detail_normal_foam_, cascade, "ocean detail normal foam texture");
}

const cubey::render::Texture2D& OceanGpuResources::texture_at(const TextureArray& textures,
                                                              std::uint32_t cascade,
                                                              const char* label) const {
    if (cascade >= textures.size() || !textures[cascade].has_value()) {
        throw std::runtime_error(std::string(label) + " is not initialized");
    }
    return textures[cascade].value();
}

const cubey::render::Texture2D&
OceanGpuResources::spectrum_texture_at(const SpectrumTextureArray& textures, std::uint32_t cascade,
                                       std::uint32_t field, const char* label) const {
    if (cascade >= kOceanCascadeCount || field >= kOceanSpectrumFieldCount) {
        throw std::runtime_error(std::string(label) + " index out of range");
    }
    const std::uint32_t index = spectrum_texture_index(cascade, field);
    if (index >= textures.size() || !textures[index].has_value()) {
        throw std::runtime_error(std::string(label) + " is not initialized");
    }
    return textures[index].value();
}

VkDescriptorSet OceanGpuResources::descriptor_at(std::span<const VkDescriptorSet> sets,
                                                 std::uint32_t index, const char* label) const {
    if (index >= sets.size() || sets[index] == VK_NULL_HANDLE) {
        throw std::runtime_error(std::string(label) + " is not initialized");
    }
    return sets[index];
}

} // namespace cubey::projects::ocean
