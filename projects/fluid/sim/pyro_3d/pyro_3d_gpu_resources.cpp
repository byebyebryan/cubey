#include "pyro_3d_gpu_resources.h"

#include <cubey/render/primitive_mesh.h>
#include <cubey/vulkan/buffer.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_PYRO_3D_SHADER_DIR
#error "CUBEY_PYRO_3D_SHADER_DIR must be defined by the pyro_3d CMake target"
#endif

namespace cubey::projects::fluid::pyro_3d {
namespace {

constexpr VkFormat kFluidVectorVolumeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kFluidScalarVolumeFormat = VK_FORMAT_R32_SFLOAT;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PYRO_3D_SHADER_DIR) / filename;
}

[[nodiscard]] VkPushConstantRange simulation_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(float) * kPyro3DSimulationPushConstantFloatCount,
    };
}

[[nodiscard]] VkPushConstantRange render_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * kPyro3DRenderPushConstantFloatCount,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo pyro_3d_render_pass_info() {
    return {
        .label = "pyro_3d.raymarch",
        .push_constants = {render_push_constant_range()},
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
}

[[nodiscard]] VkExtent3D solver_volume_extent(const Pyro3DConfig& config) {
    return {
        .width = config.grid_width,
        .height = config.grid_height,
        .depth = config.grid_depth,
    };
}

[[nodiscard]] VkExtent3D shadow_volume_extent(const Pyro3DConfig& config) {
    return {
        .width = config.shadow_grid_width,
        .height = config.shadow_grid_height,
        .depth = config.shadow_grid_depth,
    };
}

void validate_volume_format_features(const cubey::vulkan::Device& device, VkFormat format,
                                     const char* label) {
    if (format == kFluidVectorVolumeFormat &&
        !device.supports_shader_storage_image_extended_formats()) {
        throw std::runtime_error(
            "pyro 3D RGBA16F storage images require shaderStorageImageExtendedFormats");
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device.physical_device(), format, &properties);
    constexpr VkFormatFeatureFlags kRequired =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & kRequired) != kRequired) {
        throw std::runtime_error(std::string("pyro 3D ") + label +
                                 " format does not support sampled storage images");
    }
}

[[nodiscard]] cubey::render::Texture3DConfig volume_texture_config(VkExtent3D extent,
                                                                   VkFormat format, bool sampled) {
    return {
        .extent = extent,
        .format = format,
        .create_sampler = sampled,
        .sampler =
            {
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            },
    };
}

[[nodiscard]] cubey::render::Texture3DConfig
shadow_volume_texture_config(const Pyro3DConfig& config) {
    return {
        .extent = shadow_volume_extent(config),
        .format = kFluidScalarVolumeFormat,
        .create_sampler = true,
        .sampler =
            {
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            },
    };
}

[[nodiscard]] std::uint32_t advect_index(bool density_a_current, bool velocity_a_current) {
    return (density_a_current ? 0U : 2U) + (velocity_a_current ? 0U : 1U);
}

[[nodiscard]] std::uint32_t projection_index(bool velocity_a_current, bool pressure_a_current) {
    return (velocity_a_current ? 0U : 2U) + (pressure_a_current ? 0U : 1U);
}

void emplace_simulation_compute_pipeline(
    cubey::vulkan::Device& device, const char* filename, VkDescriptorSetLayout descriptor_layout,
    std::optional<cubey::render::ComputePipelineResource>& destination) {
    const std::array<VkPushConstantRange, 1> push_constants{simulation_push_constant_range()};
    cubey::render::emplace_single_set_compute_pipeline_resource(
        destination, device, cubey::render::compute_shader_file(shader_path(filename)),
        descriptor_layout, push_constants);
}

void emplace_shadow_compute_pipeline(
    cubey::vulkan::Device& device, VkDescriptorSetLayout shadow_descriptor_layout,
    VkDescriptorSetLayout environment_descriptor_layout,
    std::optional<cubey::render::ComputePipelineResource>& destination) {
    const std::array<VkDescriptorSetLayout, 2> set_layouts{shadow_descriptor_layout,
                                                           environment_descriptor_layout};
    const std::array<VkPushConstantRange, 1> push_constants{simulation_push_constant_range()};
    destination.emplace(device, cubey::render::ComputePipelineResourceConfig{
                                    .shader_stage = cubey::render::compute_shader_file(
                                        shader_path("pyro_3d_shadow.comp.spv")),
                                    .descriptor_set_layouts = set_layouts,
                                    .push_constants = push_constants,
                                });
}

} // namespace

void Pyro3DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                           cubey::vulkan::GpuRuntime& mesh_gpu,
                                                           cubey::ProjectGpuServices& gpu,
                                                           const Pyro3DConfig& config,
                                                           std::uint32_t frame_slot_count) {
    if (density_a_.has_value()) {
        if (!profiler_.has_value()) {
            profiler_.emplace(device, frame_slot_count, 9);
        }
        return;
    }

    if (frame_slot_count == 0) {
        throw std::runtime_error("pyro 3D resources require at least one frame slot");
    }
    frame_slot_count_ = frame_slot_count;

    create_volume_resources(device, gpu, config);
    create_moon_mesh_if_needed(mesh_gpu);
    environment_lighting_uniforms_.emplace(device, frame_slot_count_);
    create_descriptor_resources(device);
    create_compute_pipelines(device);
    profiler_.emplace(device, frame_slot_count, 9);
}

void Pyro3DGpuResources::create_volume_resources(cubey::vulkan::Device& device,
                                                 cubey::ProjectGpuServices& gpu,
                                                 const Pyro3DConfig& config) {
    validate_volume_format_features(device, kFluidVectorVolumeFormat, "RGBA16F volume");
    validate_volume_format_features(device, kFluidScalarVolumeFormat, "R32F volume");
    const VkExtent3D solver_extent = solver_volume_extent(config);
    density_a_.emplace(device,
                       volume_texture_config(solver_extent, kFluidVectorVolumeFormat, true));
    density_b_.emplace(device,
                       volume_texture_config(solver_extent, kFluidVectorVolumeFormat, true));
    velocity_a_.emplace(device,
                        volume_texture_config(solver_extent, kFluidVectorVolumeFormat, true));
    velocity_b_.emplace(device,
                        volume_texture_config(solver_extent, kFluidVectorVolumeFormat, true));
    density_prediction_.emplace(
        device, volume_texture_config(solver_extent, kFluidVectorVolumeFormat, false));
    velocity_prediction_.emplace(
        device, volume_texture_config(solver_extent, kFluidVectorVolumeFormat, false));
    divergence_.emplace(device,
                        volume_texture_config(solver_extent, kFluidScalarVolumeFormat, false));
    pressure_a_.emplace(device,
                        volume_texture_config(solver_extent, kFluidScalarVolumeFormat, false));
    pressure_b_.emplace(device,
                        volume_texture_config(solver_extent, kFluidScalarVolumeFormat, false));
    shadow_volume_.emplace(device, shadow_volume_texture_config(config));

    const std::vector<Pyro3DSourceGpu> empty(kMaxPyro3DSourceCount);
    sources_.emplace(gpu.upload_device_buffer(
        empty.data(), static_cast<VkDeviceSize>(pyro_3d_source_capacity_byte_size()),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        "pyro_3d source upload"));
}

void Pyro3DGpuResources::create_moon_mesh_if_needed(cubey::vulkan::GpuRuntime& gpu) {
    if (moon_mesh_.has_value()) {
        return;
    }
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> moon_mesh =
        cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
            .radius = 1.0F,
            .latitude_segments = 32U,
            .longitude_segments = 64U,
            .color = {0.86F, 0.86F, 0.86F},
        });
    moon_mesh_.emplace(gpu, moon_mesh.mesh_config());
}

void Pyro3DGpuResources::destroy_swapchain_resources() {
    render_pipeline_.reset();
    moon_body_frame_.destroy_pipeline();
    atmosphere_background_.destroy_pipeline();
}

void Pyro3DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    moon_body_frame_.destroy();
    atmosphere_background_.destroy();
    moon_mesh_.reset();
    profiler_.reset();
    shadow_pipeline_.reset();
    projection_pipeline_.reset();
    pressure_pipeline_.reset();
    divergence_pipeline_.reset();
    combustion_pipeline_.reset();
    advect_correct_pipeline_.reset();
    advect_pipeline_.reset();
    reset_pipeline_.reset();
    shadow_descriptor_pool_.reset();
    shadow_descriptor_layout_.reset();
    render_descriptors_.reset();
    environment_descriptors_.reset();
    environment_lighting_uniforms_.reset();
    frame_slot_count_ = 0;
    projection_descriptor_pool_.reset();
    projection_descriptor_layout_.reset();
    pressure_descriptor_pool_.reset();
    pressure_descriptor_layout_.reset();
    divergence_descriptor_pool_.reset();
    divergence_descriptor_layout_.reset();
    advect_correct_descriptor_pool_.reset();
    advect_correct_descriptor_layout_.reset();
    combustion_descriptor_pool_.reset();
    combustion_descriptor_layout_.reset();
    advect_descriptor_pool_.reset();
    advect_descriptor_layout_.reset();
    reset_descriptor_pool_.reset();
    reset_descriptor_layout_.reset();
    sources_.reset();
    pressure_b_.reset();
    pressure_a_.reset();
    shadow_volume_.reset();
    divergence_.reset();
    velocity_prediction_.reset();
    density_prediction_.reset();
    velocity_b_.reset();
    velocity_a_.reset();
    density_b_.reset();
    density_a_.reset();
}

void Pyro3DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 8> reset_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 4,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 5,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 6,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 7,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo reset_info(reset_bindings);
    reset_descriptor_layout_.emplace(device, reset_info.layout_info());
    reset_descriptor_pool_.emplace(device, reset_info.pool_info());
    reset_descriptor_set_ = reset_descriptor_pool().allocate(reset_descriptor_layout());

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> transport_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo advect_info(transport_bindings, 4);
    advect_descriptor_layout_.emplace(device, advect_info.layout_info());
    advect_descriptor_pool_.emplace(device, advect_info.pool_info());
    for (VkDescriptorSet& set : advect_descriptor_sets_) {
        set = advect_descriptor_pool().allocate(advect_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 6> advect_correct_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 4,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 5,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo advect_correct_info(advect_correct_bindings, 4);
    advect_correct_descriptor_layout_.emplace(device, advect_correct_info.layout_info());
    advect_correct_descriptor_pool_.emplace(device, advect_correct_info.pool_info());
    for (VkDescriptorSet& set : advect_correct_descriptor_sets_) {
        set = advect_correct_descriptor_pool().allocate(advect_correct_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 5> combustion_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 3,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 4,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo combustion_info(combustion_bindings, 4);
    combustion_descriptor_layout_.emplace(device, combustion_info.layout_info());
    combustion_descriptor_pool_.emplace(device, combustion_info.pool_info());
    for (VkDescriptorSet& set : combustion_descriptor_sets_) {
        set = combustion_descriptor_pool().allocate(combustion_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> divergence_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo divergence_info(divergence_bindings, 4);
    divergence_descriptor_layout_.emplace(device, divergence_info.layout_info());
    divergence_descriptor_pool_.emplace(device, divergence_info.pool_info());
    for (VkDescriptorSet& set : divergence_descriptor_sets_) {
        set = divergence_descriptor_pool().allocate(divergence_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> pressure_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo pressure_info(pressure_bindings, 2);
    pressure_descriptor_layout_.emplace(device, pressure_info.layout_info());
    pressure_descriptor_pool_.emplace(device, pressure_info.pool_info());
    pressure_a_to_b_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());
    pressure_b_to_a_descriptor_set_ =
        pressure_descriptor_pool().allocate(pressure_descriptor_layout());

    const cubey::vulkan::DescriptorSetInfo projection_info(pressure_bindings, 4);
    projection_descriptor_layout_.emplace(device, projection_info.layout_info());
    projection_descriptor_pool_.emplace(device, projection_info.pool_info());
    for (VkDescriptorSet& set : projection_descriptor_sets_) {
        set = projection_descriptor_pool().allocate(projection_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> shadow_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo shadow_info(shadow_bindings, 2);
    shadow_descriptor_layout_.emplace(device, shadow_info.layout_info());
    shadow_descriptor_pool_.emplace(device, shadow_info.pool_info());
    for (VkDescriptorSet& set : shadow_descriptor_sets_) {
        set = shadow_descriptor_pool().allocate(shadow_descriptor_layout());
    }

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> render_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 2,
         .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo render_info(render_bindings, 4);
    render_descriptors_.emplace(device, render_info);

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> environment_bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
    }};
    const cubey::vulkan::DescriptorSetInfo environment_info(environment_bindings,
                                                            frame_slot_count_);
    environment_descriptors_.emplace(device, environment_info);

    update_descriptors(device);
}

void Pyro3DGpuResources::update_descriptors(cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_image(reset_descriptor_set_, 0, density_a().view())
        .storage_image(reset_descriptor_set_, 1, velocity_a().view())
        .storage_image(reset_descriptor_set_, 2, divergence().view())
        .storage_image(reset_descriptor_set_, 3, pressure_a().view())
        .storage_image(reset_descriptor_set_, 4, pressure_b().view())
        .storage_image(reset_descriptor_set_, 5, shadow_volume().view())
        .storage_image(reset_descriptor_set_, 6, density_prediction().view())
        .storage_image(reset_descriptor_set_, 7, velocity_prediction().view());

    const std::array<const cubey::render::Texture3D*, 2> densities{&density_a(), &density_b()};
    const std::array<const cubey::render::Texture3D*, 2> velocities{&velocity_a(), &velocity_b()};
    for (std::uint32_t slot_index = 0; slot_index < frame_slot_count_; ++slot_index) {
        const cubey::vulkan::Buffer& buffer = environment_lighting_uniform_buffer(slot_index);
        writes.uniform_buffer(environment_descriptor_set(slot_index), 0, buffer.handle(),
                              buffer.size());
    }
    for (std::uint32_t density_index = 0; density_index < 2; ++density_index) {
        for (std::uint32_t velocity_index = 0; velocity_index < 2; ++velocity_index) {
            const bool density_a_current = density_index == 0U;
            const bool velocity_a_current = velocity_index == 0U;
            const VkDescriptorSet set =
                advect_descriptor_set(density_a_current, velocity_a_current);
            writes.storage_image(set, 0, densities[density_index]->view())
                .storage_image(set, 1, velocities[velocity_index]->view())
                .storage_image(set, 2, density_prediction().view())
                .storage_image(set, 3, velocity_prediction().view());

            const VkDescriptorSet correct_set =
                advect_correct_descriptor_set(density_a_current, velocity_a_current);
            writes.storage_image(correct_set, 0, densities[density_index]->view())
                .storage_image(correct_set, 1, velocities[velocity_index]->view())
                .storage_image(correct_set, 2, density_prediction().view())
                .storage_image(correct_set, 3, velocity_prediction().view())
                .storage_image(correct_set, 4, densities[1U - density_index]->view())
                .storage_image(correct_set, 5, velocities[1U - velocity_index]->view());

            const VkDescriptorSet combustion_set =
                combustion_descriptor_set(density_a_current, velocity_a_current);
            writes.storage_image(combustion_set, 0, densities[density_index]->view())
                .storage_image(combustion_set, 1, velocities[velocity_index]->view())
                .storage_image(combustion_set, 2, densities[1U - density_index]->view())
                .storage_image(combustion_set, 3, velocities[1U - velocity_index]->view())
                .storage_buffer(combustion_set, 4, sources().handle(), sources().size());

            const VkDescriptorSet render_set =
                render_descriptor_set(density_a_current, velocity_a_current);
            writes
                .combined_image_sampler(render_set, 0, densities[density_index]->sampler().handle(),
                                        densities[density_index]->view(), VK_IMAGE_LAYOUT_GENERAL)
                .combined_image_sampler(render_set, 1,
                                        velocities[velocity_index]->sampler().handle(),
                                        velocities[velocity_index]->view(), VK_IMAGE_LAYOUT_GENERAL)
                .combined_image_sampler(render_set, 2, shadow_volume().sampler().handle(),
                                        shadow_volume().view(), VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    writes
        .combined_image_sampler(shadow_descriptor_set(true), 0, density_a().sampler().handle(),
                                density_a().view(), VK_IMAGE_LAYOUT_GENERAL)
        .storage_image(shadow_descriptor_set(true), 1, shadow_volume().view())
        .combined_image_sampler(shadow_descriptor_set(false), 0, density_b().sampler().handle(),
                                density_b().view(), VK_IMAGE_LAYOUT_GENERAL)
        .storage_image(shadow_descriptor_set(false), 1, shadow_volume().view());

    for (std::uint32_t density_index = 0; density_index < 2; ++density_index) {
        for (std::uint32_t velocity_index = 0; velocity_index < 2; ++velocity_index) {
            const bool density_a_current = density_index == 0U;
            const bool velocity_a_current = velocity_index == 0U;
            const VkDescriptorSet set =
                divergence_descriptor_set(density_a_current, velocity_a_current);
            writes.storage_image(set, 0, velocities[velocity_index]->view())
                .storage_image(set, 1, densities[density_index]->view())
                .storage_image(set, 2, divergence().view());
        }
    }

    writes.storage_image(pressure_a_to_b_descriptor_set_, 0, divergence().view())
        .storage_image(pressure_a_to_b_descriptor_set_, 1, pressure_a().view())
        .storage_image(pressure_a_to_b_descriptor_set_, 2, pressure_b().view())
        .storage_image(pressure_b_to_a_descriptor_set_, 0, divergence().view())
        .storage_image(pressure_b_to_a_descriptor_set_, 1, pressure_b().view())
        .storage_image(pressure_b_to_a_descriptor_set_, 2, pressure_a().view());

    for (std::uint32_t velocity_index = 0; velocity_index < 2; ++velocity_index) {
        for (std::uint32_t pressure_index = 0; pressure_index < 2; ++pressure_index) {
            const bool velocity_a_current = velocity_index == 0U;
            const bool pressure_a_current = pressure_index == 0U;
            const VkDescriptorSet set =
                projection_descriptor_set(velocity_a_current, pressure_a_current);
            writes.storage_image(set, 0, velocities[velocity_index]->view())
                .storage_image(set, 1,
                               pressure_a_current ? pressure_a().view() : pressure_b().view())
                .storage_image(set, 2, velocities[1U - velocity_index]->view());
        }
    }

    writes.update(device);
}

void Pyro3DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    emplace_simulation_compute_pipeline(device, "pyro_3d_reset.comp.spv", reset_descriptor_layout(),
                                        reset_pipeline_);
    emplace_simulation_compute_pipeline(device, "pyro_3d_advect.comp.spv",
                                        advect_descriptor_layout(), advect_pipeline_);
    emplace_simulation_compute_pipeline(device, "pyro_3d_advect_correct.comp.spv",
                                        advect_correct_descriptor_layout(),
                                        advect_correct_pipeline_);
    emplace_simulation_compute_pipeline(device, "pyro_3d_combust.comp.spv",
                                        combustion_descriptor_layout(), combustion_pipeline_);
    emplace_simulation_compute_pipeline(device, "pyro_3d_divergence.comp.spv",
                                        divergence_descriptor_layout(), divergence_pipeline_);
    emplace_simulation_compute_pipeline(device, "pyro_3d_pressure.comp.spv",
                                        pressure_descriptor_layout(), pressure_pipeline_);
    emplace_simulation_compute_pipeline(device, "pyro_3d_projection.comp.spv",
                                        projection_descriptor_layout(), projection_pipeline_);
    emplace_shadow_compute_pipeline(device, shadow_descriptor_layout(),
                                    environment_descriptor_layout(), shadow_pipeline_);
}

void Pyro3DGpuResources::create_render_pipeline(
    cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
    const std::optional<cubey::render::AtmosphereBackgroundTextureBindings>&
        atmosphere_background_textures) {
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("pyro_3d.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("pyro_3d_raymarch.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> atmosphere_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("atmosphere.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("atmosphere.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> celestial_body_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("celestial_body.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("celestial_body.frag.spv"),
        },
    };
    const std::array<VkDescriptorSetLayout, 2> set_layouts{render_descriptors().layout(),
                                                           environment_descriptor_layout()};
    if (atmosphere_background_textures.has_value()) {
        const cubey::render::CelestialBodyFrameTextureBindings moon_textures{
            .surface_sampler = atmosphere_background_textures->lunar_surface_sampler,
            .surface_view = atmosphere_background_textures->lunar_surface_view,
            .surface_layout = atmosphere_background_textures->lunar_surface_layout,
        };
        if (!atmosphere_background_.materials_created()) {
            atmosphere_background_.create_materials(
                device, cubey::render::AtmosphereBackgroundFrameMaterialConfig{
                            .frame_slot_count = frame_slot_count_,
                            .textures = atmosphere_background_textures.value(),
                        });
        } else {
            atmosphere_background_.update_texture_bindings(device,
                                                           atmosphere_background_textures.value());
        }
        atmosphere_background_.create_pipeline(
            device, cubey::render::AtmosphereBackgroundFramePipelineConfig{
                        .extent = extent,
                        .color_format = color_format,
                        .shader_stage_files = atmosphere_shaders,
                    });
        if (!moon_body_frame_.materials_created()) {
            moon_body_frame_.create_materials(device,
                                              cubey::render::CelestialBodyFrameMaterialConfig{
                                                  .frame_slot_count = frame_slot_count_,
                                                  .textures = moon_textures,
                                              });
        } else {
            moon_body_frame_.update_texture_bindings(device, moon_textures);
        }
        moon_body_frame_.create_pipeline(
            device, cubey::render::CelestialBodyFramePipelineConfig{
                        .extent = extent,
                        .color_format = color_format,
                        .shader_stage_files = celestial_body_shaders,
                        .depth_mode = cubey::render::CelestialBodyDepthMode::None,
                    });
    }
    render_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                         .extent = extent,
                                         .color_format = color_format,
                                         .shader_stage_files = shader_stage_files,
                                         .descriptor_set_layouts = set_layouts,
                                         .material_pass = pyro_3d_render_pass_info(),
                                     });
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
VkDescriptorSet Pyro3DGpuResources::advect_descriptor_set(bool density_a_current,
                                                          bool velocity_a_current) const {
    return advect_descriptor_sets_.at(advect_index(density_a_current, velocity_a_current));
}

VkDescriptorSet Pyro3DGpuResources::advect_correct_descriptor_set(bool density_a_current,
                                                                  bool velocity_a_current) const {
    return advect_correct_descriptor_sets_.at(advect_index(density_a_current, velocity_a_current));
}

VkDescriptorSet Pyro3DGpuResources::combustion_descriptor_set(bool density_a_current,
                                                              bool velocity_a_current) const {
    return combustion_descriptor_sets_.at(advect_index(density_a_current, velocity_a_current));
}

VkDescriptorSet Pyro3DGpuResources::divergence_descriptor_set(bool density_a_current,
                                                              bool velocity_a_current) const {
    return divergence_descriptor_sets_.at(advect_index(density_a_current, velocity_a_current));
}

VkDescriptorSet Pyro3DGpuResources::projection_descriptor_set(bool velocity_a_current,
                                                              bool pressure_a_current) const {
    return projection_descriptor_sets_.at(projection_index(velocity_a_current, pressure_a_current));
}

VkDescriptorSet Pyro3DGpuResources::shadow_descriptor_set(bool density_a_current) const {
    return shadow_descriptor_sets_.at(density_a_current ? 0U : 1U);
}

VkDescriptorSet Pyro3DGpuResources::render_descriptor_set(bool density_a_current,
                                                          bool velocity_a_current) const {
    return render_descriptors().set(advect_index(density_a_current, velocity_a_current));
}

VkDescriptorSet
Pyro3DGpuResources::environment_descriptor_set(std::uint32_t frame_slot_index) const {
    if (frame_slot_index >= frame_slot_count_) {
        throw std::runtime_error("pyro 3D environment descriptor frame slot is out of range");
    }
    return environment_descriptors().set(frame_slot_index);
}

void Pyro3DGpuResources::upload_environment_lighting(
    std::uint32_t frame_slot_index,
    const cubey::render::EnvironmentLightingUniforms& uniforms) const {
    const cubey::render::FrameSlot frame_slot{
        .index = frame_slot_index,
        .count = frame_slot_count_,
    };
    if (!environment_lighting_uniforms_.has_value()) {
        throw std::runtime_error(
            "pyro 3D environment lighting uniform buffers are not initialized");
    }
    environment_lighting_uniforms_->upload(frame_slot, uniforms);
}

void Pyro3DGpuResources::upload_atmosphere_background(
    std::uint32_t frame_slot_index,
    const cubey::render::AtmosphereEnvironmentFrameUniforms& uniforms) const {
    const cubey::render::FrameSlot frame_slot{
        .index = frame_slot_index,
        .count = frame_slot_count_,
    };
    atmosphere_background().upload(frame_slot, uniforms);
}

void Pyro3DGpuResources::upload_moon_body(
    std::uint32_t frame_slot_index,
    const cubey::render::CelestialBodyFrameUniforms& uniforms) const {
    const cubey::render::FrameSlot frame_slot{
        .index = frame_slot_index,
        .count = frame_slot_count_,
    };
    moon_body_frame().upload(frame_slot, uniforms);
}

const cubey::render::AtmosphereBackgroundFrame& Pyro3DGpuResources::atmosphere_background() const {
    if (!atmosphere_background_.materials_created()) {
        throw std::runtime_error("pyro 3D atmosphere background is not initialized");
    }
    return atmosphere_background_;
}

const cubey::render::CelestialBodyFrame& Pyro3DGpuResources::moon_body_frame() const {
    if (!moon_body_frame_.materials_created()) {
        throw std::runtime_error("pyro 3D moon body frame is not initialized");
    }
    return moon_body_frame_;
}

const cubey::render::Mesh& Pyro3DGpuResources::moon_mesh() const {
    if (!moon_mesh_.has_value()) {
        throw std::runtime_error("pyro 3D moon mesh is not initialized");
    }
    return moon_mesh_.value();
}

VkDescriptorSet
Pyro3DGpuResources::atmosphere_background_descriptor_set(std::uint32_t frame_slot_index) const {
    return atmosphere_background().material().set(cubey::render::FrameSlot{
        .index = frame_slot_index,
        .count = frame_slot_count_,
    });
}

const cubey::render::Texture3D& Pyro3DGpuResources::density_a() const {
    if (!density_a_.has_value()) {
        throw std::runtime_error("pyro 3D density A is not initialized");
    }
    return density_a_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::density_b() const {
    if (!density_b_.has_value()) {
        throw std::runtime_error("pyro 3D density B is not initialized");
    }
    return density_b_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::velocity_a() const {
    if (!velocity_a_.has_value()) {
        throw std::runtime_error("pyro 3D velocity A is not initialized");
    }
    return velocity_a_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::velocity_b() const {
    if (!velocity_b_.has_value()) {
        throw std::runtime_error("pyro 3D velocity B is not initialized");
    }
    return velocity_b_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::density_prediction() const {
    if (!density_prediction_.has_value()) {
        throw std::runtime_error("pyro 3D density prediction is not initialized");
    }
    return density_prediction_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::velocity_prediction() const {
    if (!velocity_prediction_.has_value()) {
        throw std::runtime_error("pyro 3D velocity prediction is not initialized");
    }
    return velocity_prediction_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::divergence() const {
    if (!divergence_.has_value()) {
        throw std::runtime_error("pyro 3D divergence is not initialized");
    }
    return divergence_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::pressure_a() const {
    if (!pressure_a_.has_value()) {
        throw std::runtime_error("pyro 3D pressure A is not initialized");
    }
    return pressure_a_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::pressure_b() const {
    if (!pressure_b_.has_value()) {
        throw std::runtime_error("pyro 3D pressure B is not initialized");
    }
    return pressure_b_.value();
}

const cubey::render::Texture3D& Pyro3DGpuResources::shadow_volume() const {
    if (!shadow_volume_.has_value()) {
        throw std::runtime_error("pyro 3D shadow volume is not initialized");
    }
    return shadow_volume_.value();
}

const cubey::vulkan::Buffer& Pyro3DGpuResources::sources() const {
    if (!sources_.has_value()) {
        throw std::runtime_error("pyro 3D source buffer is not initialized");
    }
    return sources_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::reset_pipeline() const {
    if (!reset_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D reset pipeline is not initialized");
    }
    return reset_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::advect_pipeline() const {
    if (!advect_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D advect pipeline is not initialized");
    }
    return advect_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::advect_correct_pipeline() const {
    if (!advect_correct_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D advect correct pipeline is not initialized");
    }
    return advect_correct_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::combustion_pipeline() const {
    if (!combustion_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D combustion pipeline is not initialized");
    }
    return combustion_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::divergence_pipeline() const {
    if (!divergence_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D divergence pipeline is not initialized");
    }
    return divergence_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::pressure_pipeline() const {
    if (!pressure_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D pressure pipeline is not initialized");
    }
    return pressure_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::projection_pipeline() const {
    if (!projection_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D projection pipeline is not initialized");
    }
    return projection_pipeline_.value();
}

const cubey::render::ComputePipelineResource& Pyro3DGpuResources::shadow_pipeline() const {
    if (!shadow_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D shadow pipeline is not initialized");
    }
    return shadow_pipeline_.value();
}

const cubey::render::GraphicsPipelineResource& Pyro3DGpuResources::render_pipeline() const {
    if (!render_pipeline_.has_value()) {
        throw std::runtime_error("pyro 3D render pipeline is not initialized");
    }
    return render_pipeline_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::reset_descriptor_layout() const {
    if (!reset_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D reset descriptor layout is not initialized");
    }
    return reset_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::reset_descriptor_pool() const {
    if (!reset_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D reset descriptor pool is not initialized");
    }
    return reset_descriptor_pool_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::advect_descriptor_layout() const {
    if (!advect_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D advect descriptor layout is not initialized");
    }
    return advect_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::advect_descriptor_pool() const {
    if (!advect_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D advect descriptor pool is not initialized");
    }
    return advect_descriptor_pool_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::advect_correct_descriptor_layout() const {
    if (!advect_correct_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D advect correct descriptor layout is not initialized");
    }
    return advect_correct_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::advect_correct_descriptor_pool() const {
    if (!advect_correct_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D advect correct descriptor pool is not initialized");
    }
    return advect_correct_descriptor_pool_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::combustion_descriptor_layout() const {
    if (!combustion_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D combustion descriptor layout is not initialized");
    }
    return combustion_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::combustion_descriptor_pool() const {
    if (!combustion_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D combustion descriptor pool is not initialized");
    }
    return combustion_descriptor_pool_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::divergence_descriptor_layout() const {
    if (!divergence_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D divergence descriptor layout is not initialized");
    }
    return divergence_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::divergence_descriptor_pool() const {
    if (!divergence_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D divergence descriptor pool is not initialized");
    }
    return divergence_descriptor_pool_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::pressure_descriptor_layout() const {
    if (!pressure_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D pressure descriptor layout is not initialized");
    }
    return pressure_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::pressure_descriptor_pool() const {
    if (!pressure_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D pressure descriptor pool is not initialized");
    }
    return pressure_descriptor_pool_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::projection_descriptor_layout() const {
    if (!projection_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D projection descriptor layout is not initialized");
    }
    return projection_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::projection_descriptor_pool() const {
    if (!projection_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D projection descriptor pool is not initialized");
    }
    return projection_descriptor_pool_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::shadow_descriptor_layout() const {
    if (!shadow_descriptor_layout_.has_value()) {
        throw std::runtime_error("pyro 3D shadow descriptor layout is not initialized");
    }
    return shadow_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Pyro3DGpuResources::shadow_descriptor_pool() const {
    if (!shadow_descriptor_pool_.has_value()) {
        throw std::runtime_error("pyro 3D shadow descriptor pool is not initialized");
    }
    return shadow_descriptor_pool_.value();
}

const cubey::vulkan::DescriptorSetArray& Pyro3DGpuResources::render_descriptors() const {
    if (!render_descriptors_.has_value()) {
        throw std::runtime_error("pyro 3D render descriptors are not initialized");
    }
    return render_descriptors_.value();
}

VkDescriptorSetLayout Pyro3DGpuResources::environment_descriptor_layout() const {
    return environment_descriptors().layout();
}

const cubey::vulkan::DescriptorSetArray& Pyro3DGpuResources::environment_descriptors() const {
    if (!environment_descriptors_.has_value()) {
        throw std::runtime_error("pyro 3D environment descriptors are not initialized");
    }
    return environment_descriptors_.value();
}

const cubey::vulkan::Buffer&
Pyro3DGpuResources::environment_lighting_uniform_buffer(std::uint32_t frame_slot_index) const {
    const cubey::render::FrameSlot frame_slot{
        .index = frame_slot_index,
        .count = frame_slot_count_,
    };
    if (!environment_lighting_uniforms_.has_value()) {
        throw std::runtime_error(
            "pyro 3D environment lighting uniform buffers are not initialized");
    }
    return environment_lighting_uniforms_->buffer(frame_slot);
}

const std::vector<cubey::vulkan::GpuPassTiming>& Pyro3DGpuResources::latest_timings() const {
    if (!profiler_.has_value()) {
        static const std::vector<cubey::vulkan::GpuPassTiming> empty;
        return empty;
    }
    return profiler_->latest_timings();
}

} // namespace cubey::projects::fluid::pyro_3d
