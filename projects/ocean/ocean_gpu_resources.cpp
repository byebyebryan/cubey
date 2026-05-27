#include "ocean_gpu_resources.h"

#include <cubey/render/material.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace cubey::projects::ocean {
namespace {

constexpr VkFormat kOceanFieldFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

[[nodiscard]] std::filesystem::path shader_path(const std::filesystem::path& shader_dir,
                                                const char* filename) {
    return shader_dir / filename;
}

[[nodiscard]] cubey::render::Texture2D make_ocean_field_texture(
    const cubey::vulkan::Device& device, std::uint32_t resolution, bool sampled = true) {
    return cubey::render::Texture2D(
        device, cubey::render::Texture2DConfig{
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
        .size = sizeof(float) * 48U,
    };
    return {
        .label = "ocean.surface",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = false,
        .depth_write = false,
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

[[nodiscard]] cubey::vulkan::DescriptorSetInfo descriptor_info(
    std::span<const cubey::vulkan::DescriptorSetBindingConfig> bindings,
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
    if (config.target_extent.width == 0 || config.target_extent.height == 0) {
        throw std::runtime_error("ocean surface pipeline requires a target extent");
    }

    resolution_ = config.ocean.spectrum_resolution;
    create_textures(device, config.ocean);
    create_descriptor_sets(device);
    update_descriptors(device);
    create_pipelines(device, config);
}

void OceanGpuResources::reset() {
    surface_pipeline_.reset();
    finalize_pipeline_.reset();
    fft_pipeline_.reset();
    spectrum_evolve_pipeline_.reset();
    spectrum_init_pipeline_.reset();

    surface_pool_.reset();
    surface_layout_.reset();
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
        spectrum_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, false));
        ping_[cascade].emplace(make_ocean_field_texture(device, config.spectrum_resolution, false));
        pong_[cascade].emplace(make_ocean_field_texture(device, config.spectrum_resolution, false));
        displacement_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, true));
        normal_foam_[cascade].emplace(
            make_ocean_field_texture(device, config.spectrum_resolution, true));
    }
}

void OceanGpuResources::create_descriptor_sets(const cubey::vulkan::Device& device) {
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
    };
    const cubey::vulkan::DescriptorSetInfo finalize_info =
        descriptor_info(finalize_bindings, kOceanCascadeCount);
    finalize_layout_.emplace(device, finalize_info.layout_info());
    finalize_pool_.emplace(device, finalize_info.pool_info());
    for (VkDescriptorSet& set : finalize_sets_) {
        set = finalize_pool_->allocate(finalize_layout_->handle());
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
    };
    const cubey::vulkan::DescriptorSetInfo surface_info = descriptor_info(surface_bindings, 1);
    surface_layout_.emplace(device, surface_info.layout_info());
    surface_pool_.emplace(device, surface_info.pool_info());
    surface_set_ = surface_pool_->allocate(surface_layout_->handle());
}

void OceanGpuResources::update_descriptors(const cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch writes;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        writes.storage_image(spectrum_init_set(cascade), 0, h0(cascade).view())
            .storage_image(spectrum_evolve_set(cascade), 0, h0(cascade).view())
            .storage_image(spectrum_evolve_set(cascade), 1, spectrum(cascade).view())
            .storage_image(finalize_set(cascade), 0, pong(cascade).view())
            .storage_image(finalize_set(cascade), 1, displacement(cascade).view())
            .storage_image(finalize_set(cascade), 2, normal_foam(cascade).view());

        const std::uint32_t base_fft_set = cascade * 3U;
        writes.storage_image(fft_sets_[base_fft_set + 0U], 0, spectrum(cascade).view())
            .storage_image(fft_sets_[base_fft_set + 0U], 1, ping(cascade).view())
            .storage_image(fft_sets_[base_fft_set + 1U], 0, ping(cascade).view())
            .storage_image(fft_sets_[base_fft_set + 1U], 1, pong(cascade).view())
            .storage_image(fft_sets_[base_fft_set + 2U], 0, pong(cascade).view())
            .storage_image(fft_sets_[base_fft_set + 2U], 1, ping(cascade).view());

        writes.combined_image_sampler(surface_set_, cascade, displacement(cascade).sampler().handle(),
                                      displacement(cascade).view(), VK_IMAGE_LAYOUT_GENERAL)
            .combined_image_sampler(surface_set_, cascade + kOceanCascadeCount,
                                    normal_foam(cascade).sampler().handle(),
                                    normal_foam(cascade).view(), VK_IMAGE_LAYOUT_GENERAL);
    }
    writes.update(device);
}

void OceanGpuResources::create_pipelines(const cubey::vulkan::Device& device,
                                         const OceanGpuResourceConfig& config) {
    const VkPushConstantRange spectrum_push_constants = compute_push_constant_range(16U);
    const VkPushConstantRange fft_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange finalize_push_constants = compute_push_constant_range(16U);

    const std::array init_layouts{spectrum_init_layout_->handle()};
    spectrum_init_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage =
                        cubey::render::compute_shader_file(
                            shader_path(config.shader_dir, "ocean_spectrum_init.comp.spv")),
                    .descriptor_set_layouts = init_layouts,
                    .push_constants = {&spectrum_push_constants, 1},
                });

    const std::array evolve_layouts{spectrum_evolve_layout_->handle()};
    spectrum_evolve_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage =
                        cubey::render::compute_shader_file(
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
    finalize_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage =
                        cubey::render::compute_shader_file(
                            shader_path(config.shader_dir, "ocean_finalize.comp.spv")),
                    .descriptor_set_layouts = finalize_layouts,
                    .push_constants = {&finalize_push_constants, 1},
                });

    const std::array shader_stage_files{
        cubey::render::vertex_shader_file(shader_path(config.shader_dir, "ocean.vert.spv")),
        cubey::render::fragment_shader_file(shader_path(config.shader_dir, "ocean.frag.spv")),
    };
    const std::array surface_layouts{surface_layout_->handle()};
    surface_pipeline_.emplace(
        device, cubey::render::GraphicsPipelineFileResourceConfig{
                    .extent = config.target_extent,
                    .color_format = config.color_format,
                    .shader_stage_files = shader_stage_files,
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

VkDescriptorSet OceanGpuResources::spectrum_init_set(std::uint32_t cascade) const {
    return descriptor_at(spectrum_init_sets_, cascade, "ocean spectrum init descriptor set");
}

VkDescriptorSet OceanGpuResources::spectrum_evolve_set(std::uint32_t cascade) const {
    return descriptor_at(spectrum_evolve_sets_, cascade, "ocean spectrum evolve descriptor set");
}

VkDescriptorSet OceanGpuResources::fft_set(std::uint32_t cascade,
                                           std::uint32_t set_index) const {
    if (set_index >= 3U) {
        throw std::runtime_error("ocean FFT descriptor set index out of range");
    }
    return descriptor_at(fft_sets_, cascade * 3U + set_index, "ocean FFT descriptor set");
}

VkDescriptorSet OceanGpuResources::finalize_set(std::uint32_t cascade) const {
    return descriptor_at(finalize_sets_, cascade, "ocean finalize descriptor set");
}

VkDescriptorSet OceanGpuResources::surface_set() const {
    if (surface_set_ == VK_NULL_HANDLE) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    return surface_set_;
}

const cubey::render::Texture2D& OceanGpuResources::h0(std::uint32_t cascade) const {
    return texture_at(h0_, cascade, "ocean h0 texture");
}

const cubey::render::Texture2D& OceanGpuResources::spectrum(std::uint32_t cascade) const {
    return texture_at(spectrum_, cascade, "ocean spectrum texture");
}

const cubey::render::Texture2D& OceanGpuResources::ping(std::uint32_t cascade) const {
    return texture_at(ping_, cascade, "ocean ping texture");
}

const cubey::render::Texture2D& OceanGpuResources::pong(std::uint32_t cascade) const {
    return texture_at(pong_, cascade, "ocean pong texture");
}

const cubey::render::Texture2D& OceanGpuResources::displacement(std::uint32_t cascade) const {
    return texture_at(displacement_, cascade, "ocean displacement texture");
}

const cubey::render::Texture2D& OceanGpuResources::normal_foam(std::uint32_t cascade) const {
    return texture_at(normal_foam_, cascade, "ocean normal foam texture");
}

const cubey::render::Texture2D&
OceanGpuResources::texture_at(const TextureArray& textures, std::uint32_t cascade,
                              const char* label) const {
    if (cascade >= textures.size() || !textures[cascade].has_value()) {
        throw std::runtime_error(std::string(label) + " is not initialized");
    }
    return textures[cascade].value();
}

VkDescriptorSet OceanGpuResources::descriptor_at(std::span<const VkDescriptorSet> sets,
                                                 std::uint32_t index, const char* label) const {
    if (index >= sets.size() || sets[index] == VK_NULL_HANDLE) {
        throw std::runtime_error(std::string(label) + " is not initialized");
    }
    return sets[index];
}

} // namespace cubey::projects::ocean
