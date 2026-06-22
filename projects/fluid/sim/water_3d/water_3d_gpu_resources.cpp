#include "water_3d_gpu_resources.h"

#include <cubey/render/material.h>
#include <cubey/render/primitive_mesh.h>

#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_WATER_3D_SHADER_DIR
#error "CUBEY_WATER_3D_SHADER_DIR must be defined by the water_3d CMake target"
#endif

namespace cubey::projects::fluid::water_3d {
namespace {

static_assert(kWater3DComputeGroupSize == 4U);
inline constexpr VkDeviceSize kWater3DSimulationPushConstantBytes =
    sizeof(float) * kWater3DSimulationPushConstantFloatCount;
inline constexpr VkDeviceSize kWater3DRenderPushConstantBytes =
    sizeof(float) * kWater3DRenderPushConstantFloatCount;
inline constexpr std::uint32_t kWater3DGpuProfilerPassCapacity = 128;
inline constexpr VkFormat kWater3DSurfaceScalarFormat = VK_FORMAT_R32_SFLOAT;
inline constexpr VkFormat kWater3DSurfacePackedFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
inline constexpr VkFormat kWater3DSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_WATER_3D_SHADER_DIR) / filename;
}

void validate_water_3d_environment_texture_bindings(
    const Water3DEnvironmentTextureBindings& environment) {
    cubey::render::validate_pbr_environment_texture_bindings(environment.pbr);
    if (environment.atmosphere_background_textures.has_value()) {
        cubey::render::validate_atmosphere_background_texture_bindings(
            environment.atmosphere_background_textures.value());
    }
    if (environment.display_sampler == VK_NULL_HANDLE ||
        environment.display_view == VK_NULL_HANDLE) {
        throw std::runtime_error("water 3D display environment cube binding is not initialized");
    }
}

[[nodiscard]] VkPushConstantRange simulation_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = kWater3DSimulationPushConstantBytes,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_render_pass_info() {
    const VkPushConstantRange render_push_constant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = kWater3DRenderPushConstantBytes,
    };
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.render",
        .push_constants = {render_push_constant},
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS,
        .blend_enable = false,
    };
}

[[nodiscard]] VkPushConstantRange water_surface_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = kWater3DRenderPushConstantBytes,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_depth_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.surface.depth",
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_scene_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.surface.scene",
        .descriptor_sets = {cubey::render::MaterialDescriptorSetLayout{
            .set = 0,
            .bindings =
                {
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 0,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 1,
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                },
        }},
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_thickness_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.surface.thickness",
        .descriptor_sets = {cubey::render::sampled_texture_descriptor_set_layout(1)},
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_pack_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.surface.pack",
        .descriptor_sets = {cubey::render::sampled_texture_descriptor_set_layout(0, 2)},
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_repair_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.surface.repair",
        .descriptor_sets = {cubey::render::sampled_texture_descriptor_set_layout(0)},
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_smooth_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.surface.smooth",
        .descriptor_sets = {cubey::render::sampled_texture_descriptor_set_layout(0)},
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_surface_composite_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.surface.composite",
        .descriptor_sets = {cubey::render::MaterialDescriptorSetLayout{
            .set = 0,
            .bindings =
                {
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
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 2,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 3,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 4,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 5,
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                },
        }},
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo water_whitewater_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "water_3d.whitewater",
        .push_constants = {water_surface_push_constant_range()},
        .depth_test = false,
        .depth_write = false,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    };
}

void emplace_simulation_compute_pipeline(
    cubey::vulkan::Device& device, const char* filename, VkDescriptorSetLayout descriptor_layout,
    std::optional<cubey::render::ComputePipelineResource>& destination) {
    const std::array<VkPushConstantRange, 1> push_constants{simulation_push_constant_range()};
    cubey::render::emplace_single_set_compute_pipeline_resource(
        destination, device, cubey::render::compute_shader_file(shader_path(filename)),
        descriptor_layout, push_constants);
}

void create_graphics_pipeline_resource(
    cubey::vulkan::Device& device, VkExtent2D extent, VkFormat color_format, VkFormat depth_format,
    std::span<const cubey::render::ShaderStageFile> shader_stage_files,
    std::span<const VkDescriptorSetLayout> set_layouts,
    const cubey::render::MaterialPassInfo& material_pass,
    std::optional<cubey::render::GraphicsPipelineResource>& destination) {
    destination.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                    .extent = extent,
                                    .color_format = color_format,
                                    .depth_format = depth_format,
                                    .shader_stage_files = shader_stage_files,
                                    .descriptor_set_layouts = set_layouts,
                                    .material_pass = material_pass,
                                });
}

template <typename Value>
[[nodiscard]] const Value& require_initialized(const std::optional<Value>& value,
                                               const char* message) {
    if (!value.has_value()) {
        throw std::runtime_error(message);
    }
    return value.value();
}

[[nodiscard]] VkDeviceSize
optional_buffer_size(const std::optional<cubey::vulkan::Buffer>& buffer) {
    if (!buffer.has_value()) {
        return 0;
    }
    return buffer->size();
}

template <typename Value>
[[nodiscard]] VkDeviceSize optional_frame_uniform_buffer_size(
    const std::optional<cubey::render::FrameUniformBuffer<Value>>& buffer) {
    if (!buffer.has_value()) {
        return 0;
    }
    return static_cast<VkDeviceSize>(buffer->slot_count()) * buffer->range();
}

} // namespace

void Water3DGpuResources::create_global_resources_if_needed(cubey::vulkan::Device& device,
                                                            cubey::vulkan::GpuRuntime& mesh_gpu,
                                                            cubey::ProjectGpuServices& gpu,
                                                            const Water3DConfig& config,
                                                            std::uint32_t frame_slot_count) {
    if (particle_positions_.has_value()) {
        if (!profiler_.has_value()) {
            profiler_.emplace(device, frame_slot_count, kWater3DGpuProfilerPassCapacity);
        }
        return;
    }
    if (frame_slot_count == 0) {
        throw std::runtime_error("water 3D resources require at least one frame slot");
    }
    frame_slot_count_ = frame_slot_count;

    create_field_buffers(gpu, config);
    create_moon_mesh_if_needed(mesh_gpu);
    simulation_uniforms_.emplace(device, frame_slot_count_);
    environment_lighting_uniforms_.emplace(device, frame_slot_count_);
    create_descriptor_resources(device);
    create_compute_pipelines(device);
    profiler_.emplace(device, frame_slot_count_, kWater3DGpuProfilerPassCapacity);
}

void Water3DGpuResources::create_moon_mesh_if_needed(cubey::vulkan::GpuRuntime& gpu) {
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

void Water3DGpuResources::destroy_swapchain_resources() {
    whitewater_pipeline_resource_.reset();
    surface_composite_pipeline_resource_.reset();
    surface_smooth_pipeline_resource_.reset();
    surface_repair_pipeline_resource_.reset();
    surface_pack_pipeline_resource_.reset();
    surface_thickness_pipeline_resource_.reset();
    surface_depth_pipeline_resource_.reset();
    surface_scene_pipeline_resource_.reset();
    moon_body_frame_.destroy_pipeline();
    atmosphere_background_.destroy_pipeline();
    surface_composite_material_.reset();
    surface_source_b_material_.reset();
    surface_source_a_material_.reset();
    surface_pack_material_.reset();
    surface_thickness_material_.reset();
    surface_scene_material_.reset();
    whitewater_sampler_.reset();
    surface_sampler_.reset();
    render_pipeline_resource_.reset();
    depth_attachment_.reset();
}

void Water3DGpuResources::destroy_all_resources() {
    destroy_swapchain_resources();
    moon_body_frame_.destroy();
    atmosphere_background_.destroy();
    moon_mesh_.reset();
    whitewater_draw_args_pipeline_resource_.reset();
    active_whitewater_indices_pipeline_resource_.reset();
    emit_whitewater_pipeline_resource_.reset();
    advect_whitewater_pipeline_resource_.reset();
    clear_whitewater_pipeline_resource_.reset();
    diagnostics_pipeline_resource_.reset();
    advect_particles_pipeline_resource_.reset();
    grid_to_particle_pipeline_resource_.reset();
    extrapolate_velocity_pipeline_resource_.reset();
    projection_pipeline_resource_.reset();
    pressure_pipeline_resource_.reset();
    divergence_pipeline_resource_.reset();
    force_pipeline_resource_.reset();
    particle_to_grid_tiled_pipeline_resource_.reset();
    particle_to_grid_pipeline_resource_.reset();
    scatter_sorted_particles_pipeline_resource_.reset();
    scan_add_offsets_pipeline_resource_.reset();
    scan_offsets_pipeline_resource_.reset();
    active_tile_dispatch_args_pipeline_resource_.reset();
    build_active_tiles_pipeline_resource_.reset();
    active_face_dispatch_args_pipeline_resource_.reset();
    emit_pipeline_resource_.reset();
    build_bins_pipeline_resource_.reset();
    clear_bins_pipeline_resource_.reset();
    clear_grid_pipeline_resource_.reset();
    reset_pipeline_resource_.reset();
    field_descriptor_pool_.reset();
    field_descriptor_layout_.reset();
    field_descriptor_sets_.clear();
    environment_lighting_uniforms_.reset();
    simulation_uniforms_.reset();
    profiler_.reset();
    frame_slot_count_ = 0;
    active_face_dispatch_args_.reset();
    active_tile_dispatch_args_.reset();
    active_tile_indices_.reset();
    active_tile_flags_.reset();
    diagnostics_.reset();
    active_face_indices_.reset();
    active_face_flags_.reset();
    active_work_counts_.reset();
    whitewater_counters_.reset();
    whitewater_draw_args_.reset();
    whitewater_active_indices_.reset();
    whitewater_state_.reset();
    whitewater_velocities_.reset();
    whitewater_positions_.reset();
    sort_scan_level2_sums_.reset();
    sort_scan_level2_offsets_.reset();
    sort_scan_level1_sums_.reset();
    sort_scan_level1_offsets_.reset();
    sort_scan_level0_sums_.reset();
    cell_write_counts_.reset();
    cell_offsets_.reset();
    sorted_particle_indices_.reset();
    cell_counts_.reset();
    solid_.reset();
    divergence_.reset();
    pressure_b_.reset();
    pressure_a_.reset();
    w_weight_scratch_.reset();
    v_weight_scratch_.reset();
    u_weight_scratch_.reset();
    w_previous_scratch_.reset();
    w_scratch_.reset();
    v_previous_scratch_.reset();
    v_scratch_.reset();
    u_previous_scratch_.reset();
    u_scratch_.reset();
    w_weight_.reset();
    v_weight_.reset();
    u_weight_.reset();
    w_previous_.reset();
    w_.reset();
    v_previous_.reset();
    v_.reset();
    u_previous_.reset();
    u_.reset();
    particle_affine_.reset();
    particle_velocities_.reset();
    particle_positions_.reset();
}

VkDeviceSize Water3DGpuResources::allocated_buffer_bytes() const {
    return optional_buffer_size(particle_positions_) + optional_buffer_size(particle_velocities_) +
           optional_buffer_size(particle_affine_) + optional_buffer_size(u_) +
           optional_buffer_size(u_previous_) + optional_buffer_size(v_) +
           optional_buffer_size(v_previous_) + optional_buffer_size(w_) +
           optional_buffer_size(w_previous_) + optional_buffer_size(u_weight_) +
           optional_buffer_size(v_weight_) + optional_buffer_size(w_weight_) +
           optional_buffer_size(u_scratch_) + optional_buffer_size(u_previous_scratch_) +
           optional_buffer_size(v_scratch_) + optional_buffer_size(v_previous_scratch_) +
           optional_buffer_size(w_scratch_) + optional_buffer_size(w_previous_scratch_) +
           optional_buffer_size(u_weight_scratch_) + optional_buffer_size(v_weight_scratch_) +
           optional_buffer_size(w_weight_scratch_) + optional_buffer_size(pressure_a_) +
           optional_buffer_size(pressure_b_) + optional_buffer_size(divergence_) +
           optional_buffer_size(solid_) + optional_buffer_size(cell_counts_) +
           optional_buffer_size(sorted_particle_indices_) + optional_buffer_size(cell_offsets_) +
           optional_buffer_size(cell_write_counts_) + optional_buffer_size(sort_scan_level0_sums_) +
           optional_buffer_size(sort_scan_level1_offsets_) +
           optional_buffer_size(sort_scan_level1_sums_) +
           optional_buffer_size(sort_scan_level2_offsets_) +
           optional_buffer_size(sort_scan_level2_sums_) +
           optional_buffer_size(whitewater_positions_) +
           optional_buffer_size(whitewater_velocities_) + optional_buffer_size(whitewater_state_) +
           optional_buffer_size(whitewater_counters_) +
           optional_buffer_size(whitewater_active_indices_) +
           optional_buffer_size(whitewater_draw_args_) + optional_buffer_size(active_work_counts_) +
           optional_buffer_size(active_face_flags_) + optional_buffer_size(active_face_indices_) +
           optional_buffer_size(active_face_dispatch_args_) +
           optional_buffer_size(active_tile_flags_) + optional_buffer_size(active_tile_indices_) +
           optional_buffer_size(active_tile_dispatch_args_) + optional_buffer_size(diagnostics_) +
           optional_frame_uniform_buffer_size(simulation_uniforms_) +
           optional_frame_uniform_buffer_size(environment_lighting_uniforms_);
}

void Water3DGpuResources::create_field_buffers(cubey::ProjectGpuServices& gpu,
                                               const Water3DConfig& config) {
    const std::vector<float> particle_initial(particle_value_count(config), 0.0F);
    const std::vector<float> affine_initial(particle_affine_value_count(config), 0.0F);
    const std::vector<float> cell_initial(cell_count(config), 0.0F);
    const std::vector<float> u_initial(u_face_count(config), 0.0F);
    const std::vector<float> v_initial(v_face_count(config), 0.0F);
    const std::vector<float> w_initial(w_face_count(config), 0.0F);
    const std::vector<std::uint32_t> cell_count_initial(cell_count(config), 0U);
    const std::vector<std::uint32_t> sorted_particle_index_initial(
        config.particle_capacity, std::numeric_limits<std::uint32_t>::max());
    const std::vector<std::uint32_t> sort_scan_level0_initial(
        particle_sort_scan_level0_count(config), 0U);
    const std::vector<std::uint32_t> sort_scan_level1_initial(
        particle_sort_scan_level1_count(config), 0U);
    const std::vector<std::uint32_t> sort_scan_level2_initial(
        particle_sort_scan_level2_count(config), 0U);
    const std::array<std::uint32_t, 4> active_work_count_initial{};
    const std::vector<std::uint32_t> active_face_flag_initial(total_face_count(config), 0U);
    const std::vector<std::uint32_t> active_face_index_initial(
        total_face_count(config), std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint32_t, 3> active_face_dispatch_arg_initial{1U, 1U, 1U};
    const std::vector<std::uint32_t> active_tile_flag_initial(water_3d_p2g_tile_count(config), 0U);
    const std::vector<std::uint32_t> active_tile_index_initial(
        water_3d_p2g_tile_count(config), std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint32_t, 3> active_tile_dispatch_arg_initial{1U, 1U, 1U};
    const std::vector<std::uint32_t> diagnostics_initial(kWater3DDiagnosticSlotCount, 0U);
    const std::vector<float> whitewater_initial(whitewater_value_count(config), 0.0F);
    const std::vector<std::uint32_t> whitewater_active_index_initial(
        config.whitewater_capacity, std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint32_t, 4> whitewater_counter_initial{};
    const std::array<std::uint32_t, 4> whitewater_draw_args_initial{6U, 0U, 0U, 0U};
    const VkDeviceSize particle_byte_size =
        static_cast<VkDeviceSize>(particle_buffer_byte_size(config));
    const VkDeviceSize affine_byte_size =
        static_cast<VkDeviceSize>(particle_affine_buffer_byte_size(config));
    const VkDeviceSize cell_byte_size = static_cast<VkDeviceSize>(scalar_field_byte_size(config));
    const VkDeviceSize u_byte_size = static_cast<VkDeviceSize>(u_face_byte_size(config));
    const VkDeviceSize v_byte_size = static_cast<VkDeviceSize>(v_face_byte_size(config));
    const VkDeviceSize w_byte_size = static_cast<VkDeviceSize>(w_face_byte_size(config));
    const VkDeviceSize cell_uint_byte_size =
        static_cast<VkDeviceSize>(cell_uint_field_byte_size(config));
    const VkDeviceSize sorted_particle_index_bytes =
        static_cast<VkDeviceSize>(sorted_particle_index_byte_size(config));
    const VkDeviceSize sort_scan_level0_byte_size =
        static_cast<VkDeviceSize>(particle_sort_scan_level0_byte_size(config));
    const VkDeviceSize sort_scan_level1_byte_size =
        static_cast<VkDeviceSize>(particle_sort_scan_level1_byte_size(config));
    const VkDeviceSize sort_scan_level2_byte_size =
        static_cast<VkDeviceSize>(particle_sort_scan_level2_byte_size(config));
    const VkDeviceSize active_work_count_bytes =
        static_cast<VkDeviceSize>(active_work_count_byte_size(config));
    const VkDeviceSize active_face_flag_bytes =
        static_cast<VkDeviceSize>(active_face_flag_byte_size(config));
    const VkDeviceSize active_face_index_bytes =
        static_cast<VkDeviceSize>(active_face_index_byte_size(config));
    const VkDeviceSize active_face_dispatch_arg_bytes =
        static_cast<VkDeviceSize>(active_face_dispatch_arg_byte_size(config));
    const VkDeviceSize active_tile_flag_bytes =
        static_cast<VkDeviceSize>(active_tile_flag_byte_size(config));
    const VkDeviceSize active_tile_index_bytes =
        static_cast<VkDeviceSize>(active_tile_index_byte_size(config));
    const VkDeviceSize active_tile_dispatch_arg_bytes =
        static_cast<VkDeviceSize>(active_tile_dispatch_arg_byte_size(config));
    const VkDeviceSize diagnostics_bytes =
        static_cast<VkDeviceSize>(diagnostics_buffer_byte_size(config));
    const VkDeviceSize whitewater_byte_size =
        static_cast<VkDeviceSize>(whitewater_buffer_byte_size(config));
    const VkDeviceSize whitewater_counter_bytes =
        static_cast<VkDeviceSize>(whitewater_counter_byte_size(config));
    const VkDeviceSize whitewater_active_index_bytes =
        static_cast<VkDeviceSize>(whitewater_active_index_byte_size(config));
    const VkDeviceSize whitewater_draw_arg_bytes =
        static_cast<VkDeviceSize>(whitewater_draw_arg_byte_size(config));

    particle_positions_.emplace(gpu.upload_device_buffer(
        particle_initial.data(), particle_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d particle position upload"));
    particle_velocities_.emplace(gpu.upload_device_buffer(
        particle_initial.data(), particle_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d particle velocity upload"));
    particle_affine_.emplace(gpu.upload_device_buffer(affine_initial.data(), affine_byte_size,
                                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                      "water_3d particle affine upload"));
    u_.emplace(gpu.upload_device_buffer(u_initial.data(), u_byte_size,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d U upload"));
    u_previous_.emplace(gpu.upload_device_buffer(u_initial.data(), u_byte_size,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 "water_3d previous U upload"));
    v_.emplace(gpu.upload_device_buffer(v_initial.data(), v_byte_size,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d V upload"));
    v_previous_.emplace(gpu.upload_device_buffer(v_initial.data(), v_byte_size,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 "water_3d previous V upload"));
    w_.emplace(gpu.upload_device_buffer(w_initial.data(), w_byte_size,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d W upload"));
    w_previous_.emplace(gpu.upload_device_buffer(w_initial.data(), w_byte_size,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 "water_3d previous W upload"));
    u_weight_.emplace(gpu.upload_device_buffer(u_initial.data(), u_byte_size,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                               "water_3d U weight upload"));
    v_weight_.emplace(gpu.upload_device_buffer(v_initial.data(), v_byte_size,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                               "water_3d V weight upload"));
    w_weight_.emplace(gpu.upload_device_buffer(w_initial.data(), w_byte_size,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                               "water_3d W weight upload"));
    u_scratch_.emplace(gpu.upload_device_buffer(u_initial.data(), u_byte_size,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                "water_3d U scratch upload"));
    u_previous_scratch_.emplace(gpu.upload_device_buffer(u_initial.data(), u_byte_size,
                                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                         "water_3d previous U scratch upload"));
    v_scratch_.emplace(gpu.upload_device_buffer(v_initial.data(), v_byte_size,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                "water_3d V scratch upload"));
    v_previous_scratch_.emplace(gpu.upload_device_buffer(v_initial.data(), v_byte_size,
                                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                         "water_3d previous V scratch upload"));
    w_scratch_.emplace(gpu.upload_device_buffer(w_initial.data(), w_byte_size,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                "water_3d W scratch upload"));
    w_previous_scratch_.emplace(gpu.upload_device_buffer(w_initial.data(), w_byte_size,
                                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                         "water_3d previous W scratch upload"));
    u_weight_scratch_.emplace(gpu.upload_device_buffer(u_initial.data(), u_byte_size,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                       "water_3d U weight scratch upload"));
    v_weight_scratch_.emplace(gpu.upload_device_buffer(v_initial.data(), v_byte_size,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                       "water_3d V weight scratch upload"));
    w_weight_scratch_.emplace(gpu.upload_device_buffer(w_initial.data(), w_byte_size,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                       "water_3d W weight scratch upload"));
    pressure_a_.emplace(gpu.upload_device_buffer(cell_initial.data(), cell_byte_size,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 "water_3d pressure A upload"));
    pressure_b_.emplace(gpu.upload_device_buffer(cell_initial.data(), cell_byte_size,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 "water_3d pressure B upload"));
    divergence_.emplace(gpu.upload_device_buffer(cell_initial.data(), cell_byte_size,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 "water_3d divergence upload"));
    solid_.emplace(gpu.upload_device_buffer(cell_initial.data(), cell_byte_size,
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                            "water_3d solid upload"));
    cell_counts_.emplace(gpu.upload_device_buffer(cell_count_initial.data(), cell_uint_byte_size,
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  "water_3d cell count upload"));
    sorted_particle_indices_.emplace(gpu.upload_device_buffer(
        sorted_particle_index_initial.data(), sorted_particle_index_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d sorted particle index upload"));
    cell_offsets_.emplace(gpu.upload_device_buffer(cell_count_initial.data(), cell_uint_byte_size,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                   "water_3d cell offset upload"));
    cell_write_counts_.emplace(gpu.upload_device_buffer(
        cell_count_initial.data(), cell_uint_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d cell write count upload"));
    sort_scan_level0_sums_.emplace(gpu.upload_device_buffer(
        sort_scan_level0_initial.data(), sort_scan_level0_byte_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d sort scan level 0 sum upload"));
    sort_scan_level1_offsets_.emplace(gpu.upload_device_buffer(
        sort_scan_level0_initial.data(), sort_scan_level0_byte_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d sort scan level 1 offset upload"));
    sort_scan_level1_sums_.emplace(gpu.upload_device_buffer(
        sort_scan_level1_initial.data(), sort_scan_level1_byte_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d sort scan level 1 sum upload"));
    sort_scan_level2_offsets_.emplace(gpu.upload_device_buffer(
        sort_scan_level1_initial.data(), sort_scan_level1_byte_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d sort scan level 2 offset upload"));
    sort_scan_level2_sums_.emplace(gpu.upload_device_buffer(
        sort_scan_level2_initial.data(), sort_scan_level2_byte_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d sort scan level 2 sum upload"));
    active_work_counts_.emplace(gpu.upload_device_buffer(
        active_work_count_initial.data(), active_work_count_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d active work count upload"));
    active_face_flags_.emplace(gpu.upload_device_buffer(
        active_face_flag_initial.data(), active_face_flag_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d active face flag upload"));
    active_face_indices_.emplace(gpu.upload_device_buffer(
        active_face_index_initial.data(), active_face_index_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d active face index upload"));
    active_face_dispatch_args_.emplace(gpu.upload_device_buffer(
        active_face_dispatch_arg_initial.data(), active_face_dispatch_arg_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        "water_3d active face indirect dispatch upload"));
    active_tile_flags_.emplace(gpu.upload_device_buffer(
        active_tile_flag_initial.data(), active_tile_flag_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d active tile flag upload"));
    active_tile_indices_.emplace(gpu.upload_device_buffer(
        active_tile_index_initial.data(), active_tile_index_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d active tile index upload"));
    active_tile_dispatch_args_.emplace(gpu.upload_device_buffer(
        active_tile_dispatch_arg_initial.data(), active_tile_dispatch_arg_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        "water_3d active tile indirect dispatch upload"));
    diagnostics_.emplace(gpu.upload_device_buffer(diagnostics_initial.data(), diagnostics_bytes,
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                  "water_3d diagnostics upload"));
    whitewater_positions_.emplace(gpu.upload_device_buffer(
        whitewater_initial.data(), whitewater_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d whitewater position upload"));
    whitewater_velocities_.emplace(gpu.upload_device_buffer(
        whitewater_initial.data(), whitewater_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d whitewater velocity upload"));
    whitewater_state_.emplace(gpu.upload_device_buffer(
        whitewater_initial.data(), whitewater_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "water_3d whitewater state upload"));
    whitewater_counters_.emplace(gpu.upload_device_buffer(
        whitewater_counter_initial.data(), whitewater_counter_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d whitewater counter upload"));
    whitewater_active_indices_.emplace(gpu.upload_device_buffer(
        whitewater_active_index_initial.data(), whitewater_active_index_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "water_3d whitewater active index upload"));
    whitewater_draw_args_.emplace(gpu.upload_device_buffer(
        whitewater_draw_args_initial.data(), whitewater_draw_arg_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        "water_3d whitewater indirect draw upload"));
}

void Water3DGpuResources::create_descriptor_resources(cubey::vulkan::Device& device) {
    constexpr VkShaderStageFlags kStages =
        VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 49> field_bindings{{
        {.binding = 0, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 1, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 2, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 3, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 4, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 5, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 6, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 7, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 8, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 9, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 10, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 11, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 12, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 13, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 14, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 15, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 16, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 18,
         .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 19, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 20, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 21, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 22, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 23, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 24, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 25, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 26, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 27, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 28, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 29, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 30, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 31, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 32, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 33, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 34, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 35, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 36, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 37, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 38, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 39, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 42, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 43, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 44, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 45, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 46, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 47, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 48, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 49, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 50, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
        {.binding = 51, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = kStages},
    }};
    const cubey::vulkan::DescriptorSetInfo field_info(field_bindings, frame_slot_count_);
    field_descriptor_layout_.emplace(device, field_info.layout_info());
    field_descriptor_pool_.emplace(device, field_info.pool_info());
    field_descriptor_sets_ =
        field_descriptor_pool().allocate_many(field_descriptor_layout(), frame_slot_count_);

    update_field_descriptors(device);
}

void Water3DGpuResources::update_field_descriptors(cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;
    for (std::uint32_t slot = 0; slot < frame_slot_count_; ++slot) {
        const cubey::render::FrameSlot frame_slot{.index = slot, .count = frame_slot_count_};
        const VkDescriptorSet set = field_descriptor_sets_.at(slot);
        descriptor_writes
            .storage_buffer(set, 0, particle_positions().handle(), particle_positions().size())
            .storage_buffer(set, 1, particle_velocities().handle(), particle_velocities().size())
            .storage_buffer(set, 2, particle_affine().handle(), particle_affine().size())
            .storage_buffer(set, 3, u().handle(), u().size())
            .storage_buffer(set, 4, u_previous().handle(), u_previous().size())
            .storage_buffer(set, 5, v().handle(), v().size())
            .storage_buffer(set, 6, v_previous().handle(), v_previous().size())
            .storage_buffer(set, 7, w().handle(), w().size())
            .storage_buffer(set, 8, w_previous().handle(), w_previous().size())
            .storage_buffer(set, 9, u_weight().handle(), u_weight().size())
            .storage_buffer(set, 10, v_weight().handle(), v_weight().size())
            .storage_buffer(set, 11, w_weight().handle(), w_weight().size())
            .storage_buffer(set, 12, pressure_a().handle(), pressure_a().size())
            .storage_buffer(set, 13, pressure_b().handle(), pressure_b().size())
            .storage_buffer(set, 14, divergence().handle(), divergence().size())
            .storage_buffer(set, 15, solid().handle(), solid().size())
            .storage_buffer(set, 16, cell_counts().handle(), cell_counts().size())
            .uniform_buffer(set, 18, simulation_uniform_buffer(frame_slot).handle(),
                            simulation_uniform_buffer(frame_slot).size())
            .storage_buffer(set, 19, u_scratch().handle(), u_scratch().size())
            .storage_buffer(set, 20, u_previous_scratch().handle(), u_previous_scratch().size())
            .storage_buffer(set, 21, v_scratch().handle(), v_scratch().size())
            .storage_buffer(set, 22, v_previous_scratch().handle(), v_previous_scratch().size())
            .storage_buffer(set, 23, w_scratch().handle(), w_scratch().size())
            .storage_buffer(set, 24, w_previous_scratch().handle(), w_previous_scratch().size())
            .storage_buffer(set, 25, u_weight_scratch().handle(), u_weight_scratch().size())
            .storage_buffer(set, 26, v_weight_scratch().handle(), v_weight_scratch().size())
            .storage_buffer(set, 27, w_weight_scratch().handle(), w_weight_scratch().size())
            .storage_buffer(set, 28, whitewater_positions().handle(), whitewater_positions().size())
            .storage_buffer(set, 29, whitewater_velocities().handle(),
                            whitewater_velocities().size())
            .storage_buffer(set, 30, whitewater_state().handle(), whitewater_state().size())
            .storage_buffer(set, 31, whitewater_counters().handle(), whitewater_counters().size())
            .storage_buffer(set, 32, whitewater_active_indices().handle(),
                            whitewater_active_indices().size())
            .storage_buffer(set, 33, whitewater_draw_args().handle(), whitewater_draw_args().size())
            .storage_buffer(set, 34, active_work_counts().handle(), active_work_counts().size())
            .storage_buffer(set, 35, active_face_flags().handle(), active_face_flags().size())
            .storage_buffer(set, 36, active_face_indices().handle(), active_face_indices().size())
            .storage_buffer(set, 37, active_face_dispatch_args().handle(),
                            active_face_dispatch_args().size())
            .storage_buffer(set, 38, diagnostics().handle(), diagnostics().size())
            .storage_buffer(set, 39, sorted_particle_indices().handle(),
                            sorted_particle_indices().size())
            .storage_buffer(set, 42, cell_offsets().handle(), cell_offsets().size())
            .storage_buffer(set, 43, cell_write_counts().handle(), cell_write_counts().size())
            .storage_buffer(set, 44, sort_scan_level0_sums().handle(),
                            sort_scan_level0_sums().size())
            .storage_buffer(set, 45, sort_scan_level1_offsets().handle(),
                            sort_scan_level1_offsets().size())
            .storage_buffer(set, 46, sort_scan_level1_sums().handle(),
                            sort_scan_level1_sums().size())
            .storage_buffer(set, 47, sort_scan_level2_offsets().handle(),
                            sort_scan_level2_offsets().size())
            .storage_buffer(set, 48, sort_scan_level2_sums().handle(),
                            sort_scan_level2_sums().size())
            .storage_buffer(set, 49, active_tile_flags().handle(), active_tile_flags().size())
            .storage_buffer(set, 50, active_tile_indices().handle(), active_tile_indices().size())
            .storage_buffer(set, 51, active_tile_dispatch_args().handle(),
                            active_tile_dispatch_args().size());
    }
    descriptor_writes.update(device);
}

void Water3DGpuResources::create_compute_pipelines(cubey::vulkan::Device& device) {
    emplace_simulation_compute_pipeline(device, "water_3d_reset.comp.spv",
                                        field_descriptor_layout(), reset_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_clear_grid.comp.spv",
                                        field_descriptor_layout(), clear_grid_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_clear_bins.comp.spv",
                                        field_descriptor_layout(), clear_bins_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_build_bins.comp.spv",
                                        field_descriptor_layout(), build_bins_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_emit_particles.comp.spv",
                                        field_descriptor_layout(), emit_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_active_face_dispatch_args.comp.spv",
                                        field_descriptor_layout(),
                                        active_face_dispatch_args_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_build_active_tiles.comp.spv",
                                        field_descriptor_layout(),
                                        build_active_tiles_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_active_tile_dispatch_args.comp.spv",
                                        field_descriptor_layout(),
                                        active_tile_dispatch_args_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_scan_offsets.comp.spv",
                                        field_descriptor_layout(), scan_offsets_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_scan_add_offsets.comp.spv",
                                        field_descriptor_layout(),
                                        scan_add_offsets_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_scatter_sorted_particles.comp.spv",
                                        field_descriptor_layout(),
                                        scatter_sorted_particles_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_particle_to_grid.comp.spv",
                                        field_descriptor_layout(),
                                        particle_to_grid_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_particle_to_grid_tiled.comp.spv",
                                        field_descriptor_layout(),
                                        particle_to_grid_tiled_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_force.comp.spv",
                                        field_descriptor_layout(), force_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_divergence.comp.spv",
                                        field_descriptor_layout(), divergence_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_pressure.comp.spv",
                                        field_descriptor_layout(), pressure_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_projection.comp.spv",
                                        field_descriptor_layout(), projection_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_extrapolate_velocity.comp.spv",
                                        field_descriptor_layout(),
                                        extrapolate_velocity_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_grid_to_particle.comp.spv",
                                        field_descriptor_layout(),
                                        grid_to_particle_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_advect_particles.comp.spv",
                                        field_descriptor_layout(),
                                        advect_particles_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_whitewater_clear.comp.spv",
                                        field_descriptor_layout(),
                                        clear_whitewater_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_whitewater_advect.comp.spv",
                                        field_descriptor_layout(),
                                        advect_whitewater_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_whitewater_emit.comp.spv",
                                        field_descriptor_layout(),
                                        emit_whitewater_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_whitewater_active_indices.comp.spv",
                                        field_descriptor_layout(),
                                        active_whitewater_indices_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_whitewater_draw_args.comp.spv",
                                        field_descriptor_layout(),
                                        whitewater_draw_args_pipeline_resource_);
    emplace_simulation_compute_pipeline(device, "water_3d_diagnostics.comp.spv",
                                        field_descriptor_layout(), diagnostics_pipeline_resource_);
}

void Water3DGpuResources::create_render_pipeline(
    cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
    const Water3DEnvironmentTextureBindings& environment) {
    validate_water_3d_environment_texture_bindings(environment);
    depth_attachment_.emplace(device, extent);
    surface_sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                         .min_filter = VK_FILTER_LINEAR,
                                         .mag_filter = VK_FILTER_LINEAR,
                                         .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                     });
    whitewater_sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                            .min_filter = VK_FILTER_NEAREST,
                                            .mag_filter = VK_FILTER_NEAREST,
                                            .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                        });

    const cubey::render::MaterialPassInfo debug_material_pass = water_render_pass_info();
    const cubey::render::MaterialPassInfo surface_scene_material_pass =
        water_surface_scene_pass_info();
    const cubey::render::MaterialPassInfo surface_depth_material_pass =
        water_surface_depth_pass_info();
    const cubey::render::MaterialPassInfo surface_thickness_material_pass =
        water_surface_thickness_pass_info();
    const cubey::render::MaterialPassInfo surface_pack_material_pass =
        water_surface_pack_pass_info();
    const cubey::render::MaterialPassInfo surface_repair_material_pass =
        water_surface_repair_pass_info();
    const cubey::render::MaterialPassInfo surface_smooth_material_pass =
        water_surface_smooth_pass_info();
    const cubey::render::MaterialPassInfo surface_composite_material_pass =
        water_surface_composite_pass_info();
    const cubey::render::MaterialPassInfo whitewater_material_pass = water_whitewater_pass_info();

    surface_scene_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                .material_pass = surface_scene_material_pass,
                                                .descriptor_set = 0,
                                                .set_count = frame_slot_count_,
                                            });
    surface_thickness_material_.emplace(device,
                                        cubey::render::MaterialInstanceConfig{
                                            .material_pass = surface_thickness_material_pass,
                                            .descriptor_set = 1,
                                            .set_count = frame_slot_count_,
                                        });
    surface_pack_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                               .material_pass = surface_pack_material_pass,
                                               .descriptor_set = 0,
                                               .set_count = frame_slot_count_,
                                           });
    surface_source_a_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                   .material_pass = surface_smooth_material_pass,
                                                   .descriptor_set = 0,
                                                   .set_count = frame_slot_count_,
                                               });
    surface_source_b_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                   .material_pass = surface_smooth_material_pass,
                                                   .descriptor_set = 0,
                                                   .set_count = frame_slot_count_,
                                               });
    surface_composite_material_.emplace(device,
                                        cubey::render::MaterialInstanceConfig{
                                            .material_pass = surface_composite_material_pass,
                                            .descriptor_set = 0,
                                            .set_count = frame_slot_count_,
                                        });

    const std::array<cubey::render::ShaderStageFile, 2> debug_shader_stage_files{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_render.frag.spv"),
        },
    };

    const std::array<cubey::render::ShaderStageFile, 2> surface_scene_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_fullscreen.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_scene.frag.spv"),
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
    const std::array<cubey::render::ShaderStageFile, 2> surface_particle_depth_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_surface_particle.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_surface_depth.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_particle_thickness_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_surface_particle.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_surface_thickness.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_pack_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_fullscreen.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_surface_pack.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_repair_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_fullscreen.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_surface_repair.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_smooth_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_fullscreen.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_surface_smooth.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> surface_composite_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_fullscreen.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_surface_composite.frag.spv"),
        },
    };
    const std::array<cubey::render::ShaderStageFile, 2> whitewater_shaders{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = shader_path("water_3d_whitewater.vert.spv"),
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = shader_path("water_3d_whitewater.frag.spv"),
        },
    };

    const std::array<VkDescriptorSetLayout, 1> field_set_layouts{field_descriptor_layout()};
    const std::array<VkDescriptorSetLayout, 1> scene_set_layouts{
        surface_scene_material_->layout(),
    };
    const std::array<VkDescriptorSetLayout, 2> field_and_thickness_set_layouts{
        field_descriptor_layout(),
        surface_thickness_material_->layout(),
    };
    const std::array<VkDescriptorSetLayout, 1> pack_set_layouts{surface_pack_material_->layout()};
    const std::array<VkDescriptorSetLayout, 1> source_set_layouts{
        surface_source_a_material_->layout(),
    };
    const std::array<VkDescriptorSetLayout, 1> composite_set_layouts{
        surface_composite_material_->layout(),
    };

    create_graphics_pipeline_resource(device, extent, color_format, depth_attachment().format(),
                                      debug_shader_stage_files, field_set_layouts,
                                      debug_material_pass, render_pipeline_resource_);
    create_graphics_pipeline_resource(device, extent, kWater3DSceneColorFormat,
                                      depth_attachment().format(), surface_scene_shaders,
                                      scene_set_layouts, surface_scene_material_pass,
                                      surface_scene_pipeline_resource_);
    if (environment.atmosphere_background_textures.has_value()) {
        const cubey::render::CelestialBodyFrameTextureBindings moon_textures{
            .surface_sampler = environment.atmosphere_background_textures->lunar_sampler,
            .surface_view = environment.atmosphere_background_textures->lunar_view,
            .surface_layout = environment.atmosphere_background_textures->lunar_layout,
        };
        if (!atmosphere_background_.materials_created()) {
            atmosphere_background_.create_materials(
                device, cubey::render::AtmosphereBackgroundFrameMaterialConfig{
                            .frame_slot_count = frame_slot_count_,
                            .textures = environment.atmosphere_background_textures.value(),
                        });
        } else {
            atmosphere_background_.update_texture_bindings(
                device, environment.atmosphere_background_textures.value());
        }
        atmosphere_background_.create_pipeline(
            device, cubey::render::AtmosphereBackgroundFramePipelineConfig{
                        .extent = extent,
                        .color_format = kWater3DSceneColorFormat,
                        .depth_format = depth_attachment().format(),
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
                        .color_format = kWater3DSceneColorFormat,
                        .depth_format = depth_attachment().format(),
                        .shader_stage_files = celestial_body_shaders,
                        .depth_mode = cubey::render::CelestialBodyDepthMode::None,
                    });
    }
    create_graphics_pipeline_resource(device, extent, kWater3DSurfaceScalarFormat,
                                      depth_attachment().format(), surface_particle_depth_shaders,
                                      field_set_layouts, surface_depth_material_pass,
                                      surface_depth_pipeline_resource_);
    create_graphics_pipeline_resource(
        device, extent, kWater3DSurfaceScalarFormat, VK_FORMAT_UNDEFINED,
        surface_particle_thickness_shaders, field_and_thickness_set_layouts,
        surface_thickness_material_pass, surface_thickness_pipeline_resource_);
    create_graphics_pipeline_resource(device, extent, kWater3DSurfacePackedFormat,
                                      VK_FORMAT_UNDEFINED, surface_pack_shaders, pack_set_layouts,
                                      surface_pack_material_pass, surface_pack_pipeline_resource_);
    create_graphics_pipeline_resource(
        device, extent, kWater3DSurfacePackedFormat, VK_FORMAT_UNDEFINED, surface_repair_shaders,
        source_set_layouts, surface_repair_material_pass, surface_repair_pipeline_resource_);
    create_graphics_pipeline_resource(
        device, extent, kWater3DSurfacePackedFormat, VK_FORMAT_UNDEFINED, surface_smooth_shaders,
        source_set_layouts, surface_smooth_material_pass, surface_smooth_pipeline_resource_);
    create_graphics_pipeline_resource(device, extent, color_format, VK_FORMAT_UNDEFINED,
                                      surface_composite_shaders, composite_set_layouts,
                                      surface_composite_material_pass,
                                      surface_composite_pipeline_resource_);
    create_graphics_pipeline_resource(
        device, extent, kWater3DSceneColorFormat, depth_attachment().format(), whitewater_shaders,
        field_set_layouts, whitewater_material_pass, whitewater_pipeline_resource_);

    cubey::vulkan::DescriptorWriteBatch writes;
    for (std::uint32_t slot_index = 0; slot_index < frame_slot_count_; ++slot_index) {
        const cubey::render::FrameSlot frame_slot{
            .index = slot_index,
            .count = frame_slot_count_,
        };
        writes
            .combined_image_sampler(surface_scene_descriptor_set(frame_slot), 0,
                                    environment.display_sampler, environment.display_view,
                                    environment.display_layout)
            .uniform_buffer(surface_scene_descriptor_set(frame_slot), 1,
                            environment_lighting_uniform_buffer(frame_slot).handle(),
                            environment_lighting_uniform_buffer(frame_slot).size());
    }
    writes.update(device);
}

const cubey::vulkan::Buffer& Water3DGpuResources::particle_positions() const {
    return require_initialized(particle_positions_,
                               "water 3D particle positions are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::particle_velocities() const {
    return require_initialized(particle_velocities_,
                               "water 3D particle velocities are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::particle_affine() const {
    return require_initialized(particle_affine_,
                               "water 3D particle affine state is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::u() const {
    return require_initialized(u_, "water 3D U field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::u_previous() const {
    return require_initialized(u_previous_, "water 3D previous U field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::v() const {
    return require_initialized(v_, "water 3D V field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::v_previous() const {
    return require_initialized(v_previous_, "water 3D previous V field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::w() const {
    return require_initialized(w_, "water 3D W field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::w_previous() const {
    return require_initialized(w_previous_, "water 3D previous W field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::u_weight() const {
    return require_initialized(u_weight_, "water 3D U weight field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::v_weight() const {
    return require_initialized(v_weight_, "water 3D V weight field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::w_weight() const {
    return require_initialized(w_weight_, "water 3D W weight field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::u_scratch() const {
    return require_initialized(u_scratch_, "water 3D U scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::u_previous_scratch() const {
    return require_initialized(u_previous_scratch_,
                               "water 3D previous U scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::v_scratch() const {
    return require_initialized(v_scratch_, "water 3D V scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::v_previous_scratch() const {
    return require_initialized(v_previous_scratch_,
                               "water 3D previous V scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::w_scratch() const {
    return require_initialized(w_scratch_, "water 3D W scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::w_previous_scratch() const {
    return require_initialized(w_previous_scratch_,
                               "water 3D previous W scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::u_weight_scratch() const {
    return require_initialized(u_weight_scratch_,
                               "water 3D U weight scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::v_weight_scratch() const {
    return require_initialized(v_weight_scratch_,
                               "water 3D V weight scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::w_weight_scratch() const {
    return require_initialized(w_weight_scratch_,
                               "water 3D W weight scratch field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::pressure_a() const {
    return require_initialized(pressure_a_, "water 3D pressure A field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::pressure_b() const {
    return require_initialized(pressure_b_, "water 3D pressure B field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::divergence() const {
    return require_initialized(divergence_, "water 3D divergence field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::solid() const {
    return require_initialized(solid_, "water 3D solid field is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::cell_counts() const {
    return require_initialized(cell_counts_, "water 3D cell counts are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::sorted_particle_indices() const {
    return require_initialized(sorted_particle_indices_,
                               "water 3D sorted particle indices are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::cell_offsets() const {
    return require_initialized(cell_offsets_, "water 3D cell offsets are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::cell_write_counts() const {
    return require_initialized(cell_write_counts_,
                               "water 3D cell write counts are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::sort_scan_level0_sums() const {
    return require_initialized(sort_scan_level0_sums_,
                               "water 3D sort scan level 0 sums are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::sort_scan_level1_offsets() const {
    return require_initialized(sort_scan_level1_offsets_,
                               "water 3D sort scan level 1 offsets are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::sort_scan_level1_sums() const {
    return require_initialized(sort_scan_level1_sums_,
                               "water 3D sort scan level 1 sums are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::sort_scan_level2_offsets() const {
    return require_initialized(sort_scan_level2_offsets_,
                               "water 3D sort scan level 2 offsets are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::sort_scan_level2_sums() const {
    return require_initialized(sort_scan_level2_sums_,
                               "water 3D sort scan level 2 sums are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::whitewater_positions() const {
    return require_initialized(whitewater_positions_,
                               "water 3D whitewater positions are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::whitewater_velocities() const {
    return require_initialized(whitewater_velocities_,
                               "water 3D whitewater velocities are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::whitewater_state() const {
    return require_initialized(whitewater_state_, "water 3D whitewater state is not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::whitewater_counters() const {
    return require_initialized(whitewater_counters_,
                               "water 3D whitewater counters are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::whitewater_active_indices() const {
    return require_initialized(whitewater_active_indices_,
                               "water 3D whitewater active indices are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::whitewater_draw_args() const {
    return require_initialized(whitewater_draw_args_,
                               "water 3D whitewater draw args are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::active_work_counts() const {
    return require_initialized(active_work_counts_,
                               "water 3D active work counts are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::active_face_flags() const {
    return require_initialized(active_face_flags_,
                               "water 3D active face flags are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::active_face_indices() const {
    return require_initialized(active_face_indices_,
                               "water 3D active face indices are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::active_face_dispatch_args() const {
    return require_initialized(active_face_dispatch_args_,
                               "water 3D active face dispatch args are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::active_tile_flags() const {
    return require_initialized(active_tile_flags_,
                               "water 3D active tile flags are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::active_tile_indices() const {
    return require_initialized(active_tile_indices_,
                               "water 3D active tile indices are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::active_tile_dispatch_args() const {
    return require_initialized(active_tile_dispatch_args_,
                               "water 3D active tile dispatch args are not initialized");
}

const cubey::vulkan::Buffer& Water3DGpuResources::diagnostics() const {
    return require_initialized(diagnostics_, "water 3D diagnostics are not initialized");
}

const std::vector<cubey::vulkan::GpuPassTiming>& Water3DGpuResources::latest_timings() const {
    if (!profiler_.has_value()) {
        static const std::vector<cubey::vulkan::GpuPassTiming> empty;
        return empty;
    }
    return profiler_->latest_timings();
}

const cubey::vulkan::Buffer&
Water3DGpuResources::simulation_uniform_buffer(cubey::render::FrameSlot frame_slot) const {
    const cubey::render::FrameUniformBuffer<Water3DSimulationUniforms>& uniforms =
        require_initialized(simulation_uniforms_,
                            "water 3D simulation uniform buffers are not initialized");
    return uniforms.buffer(frame_slot);
}

void Water3DGpuResources::upload_simulation_uniforms(
    cubey::render::FrameSlot frame_slot, const Water3DSimulationUniforms& uniforms) const {
    const cubey::render::FrameUniformBuffer<Water3DSimulationUniforms>& buffers =
        require_initialized(simulation_uniforms_,
                            "water 3D simulation uniform buffers are not initialized");
    buffers.upload(frame_slot, uniforms);
}

void Water3DGpuResources::upload_environment_lighting(
    cubey::render::FrameSlot frame_slot,
    const cubey::render::EnvironmentLightingUniforms& uniforms) const {
    const cubey::render::FrameUniformBuffer<cubey::render::EnvironmentLightingUniforms>& buffers =
        require_initialized(environment_lighting_uniforms_,
                            "water 3D environment lighting uniform buffers are not initialized");
    buffers.upload(frame_slot, uniforms);
}

void Water3DGpuResources::upload_atmosphere_background(
    cubey::render::FrameSlot frame_slot,
    const cubey::render::AtmosphereEnvironmentFrameUniforms& uniforms) const {
    atmosphere_background().upload(frame_slot, uniforms);
}

void Water3DGpuResources::upload_moon_body(
    cubey::render::FrameSlot frame_slot,
    const cubey::render::CelestialBodyFrameUniforms& uniforms) const {
    moon_body_frame().upload(frame_slot, uniforms);
}

const cubey::vulkan::Buffer& Water3DGpuResources::environment_lighting_uniform_buffer(
    cubey::render::FrameSlot frame_slot) const {
    const cubey::render::FrameUniformBuffer<cubey::render::EnvironmentLightingUniforms>& buffers =
        require_initialized(environment_lighting_uniforms_,
                            "water 3D environment lighting uniform buffers are not initialized");
    return buffers.buffer(frame_slot);
}

VkDescriptorSet
Water3DGpuResources::field_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    cubey::render::validate_frame_slot(frame_slot);
    if (frame_slot.count != frame_slot_count_) {
        throw std::runtime_error("water 3D descriptor frame slot count mismatch");
    }
    if (frame_slot.index >= field_descriptor_sets_.size()) {
        throw std::runtime_error("water 3D descriptor frame slot is out of range");
    }
    return field_descriptor_sets_.at(frame_slot.index);
}

VkDescriptorSetLayout Water3DGpuResources::field_descriptor_layout() const {
    if (!field_descriptor_layout_.has_value()) {
        throw std::runtime_error("water 3D field descriptor layout is not initialized");
    }
    return field_descriptor_layout_->handle();
}

const cubey::vulkan::DescriptorPool& Water3DGpuResources::field_descriptor_pool() const {
    return require_initialized(field_descriptor_pool_,
                               "water 3D field descriptor pool is not initialized");
}

const cubey::render::ComputePipelineResource& Water3DGpuResources::reset_pipeline_resource() const {
    return require_initialized(reset_pipeline_resource_,
                               "water 3D reset pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::clear_grid_pipeline_resource() const {
    return require_initialized(clear_grid_pipeline_resource_,
                               "water 3D grid clear pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::clear_bins_pipeline_resource() const {
    return require_initialized(clear_bins_pipeline_resource_,
                               "water 3D bin clear pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::build_bins_pipeline_resource() const {
    return require_initialized(build_bins_pipeline_resource_,
                               "water 3D bin build pipeline is not initialized");
}

const cubey::render::ComputePipelineResource& Water3DGpuResources::emit_pipeline_resource() const {
    return require_initialized(emit_pipeline_resource_,
                               "water 3D emitter pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::active_face_dispatch_args_pipeline_resource() const {
    return require_initialized(active_face_dispatch_args_pipeline_resource_,
                               "water 3D active face dispatch args pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::build_active_tiles_pipeline_resource() const {
    return require_initialized(build_active_tiles_pipeline_resource_,
                               "water 3D active tile build pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::active_tile_dispatch_args_pipeline_resource() const {
    return require_initialized(active_tile_dispatch_args_pipeline_resource_,
                               "water 3D active tile dispatch args pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::scan_offsets_pipeline_resource() const {
    return require_initialized(scan_offsets_pipeline_resource_,
                               "water 3D scan offsets pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::scan_add_offsets_pipeline_resource() const {
    return require_initialized(scan_add_offsets_pipeline_resource_,
                               "water 3D scan add offsets pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::scatter_sorted_particles_pipeline_resource() const {
    return require_initialized(scatter_sorted_particles_pipeline_resource_,
                               "water 3D sorted particle scatter pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::particle_to_grid_pipeline_resource() const {
    return require_initialized(particle_to_grid_pipeline_resource_,
                               "water 3D particle-to-grid pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::particle_to_grid_tiled_pipeline_resource() const {
    return require_initialized(particle_to_grid_tiled_pipeline_resource_,
                               "water 3D tiled particle-to-grid pipeline is not initialized");
}

const cubey::render::ComputePipelineResource& Water3DGpuResources::force_pipeline_resource() const {
    return require_initialized(force_pipeline_resource_,
                               "water 3D force pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::divergence_pipeline_resource() const {
    return require_initialized(divergence_pipeline_resource_,
                               "water 3D divergence pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::pressure_pipeline_resource() const {
    return require_initialized(pressure_pipeline_resource_,
                               "water 3D pressure pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::projection_pipeline_resource() const {
    return require_initialized(projection_pipeline_resource_,
                               "water 3D projection pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::extrapolate_velocity_pipeline_resource() const {
    return require_initialized(extrapolate_velocity_pipeline_resource_,
                               "water 3D velocity extrapolation pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::grid_to_particle_pipeline_resource() const {
    return require_initialized(grid_to_particle_pipeline_resource_,
                               "water 3D grid-to-particle pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::advect_particles_pipeline_resource() const {
    return require_initialized(advect_particles_pipeline_resource_,
                               "water 3D particle advection pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::clear_whitewater_pipeline_resource() const {
    return require_initialized(clear_whitewater_pipeline_resource_,
                               "water 3D whitewater clear pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::advect_whitewater_pipeline_resource() const {
    return require_initialized(advect_whitewater_pipeline_resource_,
                               "water 3D whitewater advection pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::emit_whitewater_pipeline_resource() const {
    return require_initialized(emit_whitewater_pipeline_resource_,
                               "water 3D whitewater emission pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::active_whitewater_indices_pipeline_resource() const {
    return require_initialized(active_whitewater_indices_pipeline_resource_,
                               "water 3D whitewater active index pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::whitewater_draw_args_pipeline_resource() const {
    return require_initialized(whitewater_draw_args_pipeline_resource_,
                               "water 3D whitewater draw args pipeline is not initialized");
}

const cubey::render::ComputePipelineResource&
Water3DGpuResources::diagnostics_pipeline_resource() const {
    return require_initialized(diagnostics_pipeline_resource_,
                               "water 3D diagnostics pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::render_pipeline_resource() const {
    return require_initialized(render_pipeline_resource_,
                               "water 3D render pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::surface_scene_pipeline_resource() const {
    return require_initialized(surface_scene_pipeline_resource_,
                               "water 3D surface scene pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::surface_depth_pipeline_resource() const {
    return require_initialized(surface_depth_pipeline_resource_,
                               "water 3D surface depth pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::surface_thickness_pipeline_resource() const {
    return require_initialized(surface_thickness_pipeline_resource_,
                               "water 3D surface thickness pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::surface_pack_pipeline_resource() const {
    return require_initialized(surface_pack_pipeline_resource_,
                               "water 3D surface pack pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::surface_repair_pipeline_resource() const {
    return require_initialized(surface_repair_pipeline_resource_,
                               "water 3D surface repair pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::surface_smooth_pipeline_resource() const {
    return require_initialized(surface_smooth_pipeline_resource_,
                               "water 3D surface smooth pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::surface_composite_pipeline_resource() const {
    return require_initialized(surface_composite_pipeline_resource_,
                               "water 3D surface composite pipeline is not initialized");
}

const cubey::render::GraphicsPipelineResource&
Water3DGpuResources::whitewater_pipeline_resource() const {
    return require_initialized(whitewater_pipeline_resource_,
                               "water 3D whitewater pipeline is not initialized");
}

const cubey::render::AtmosphereBackgroundFrame& Water3DGpuResources::atmosphere_background() const {
    if (!atmosphere_background_.materials_created()) {
        throw std::runtime_error("water 3D atmosphere background is not initialized");
    }
    return atmosphere_background_;
}

const cubey::render::CelestialBodyFrame& Water3DGpuResources::moon_body_frame() const {
    if (!moon_body_frame_.materials_created()) {
        throw std::runtime_error("water 3D moon body frame is not initialized");
    }
    return moon_body_frame_;
}

const cubey::render::Mesh& Water3DGpuResources::moon_mesh() const {
    if (!moon_mesh_.has_value()) {
        throw std::runtime_error("water 3D moon mesh is not initialized");
    }
    return moon_mesh_.value();
}

VkDescriptorSet
Water3DGpuResources::surface_scene_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_scene_material_,
                               "water 3D surface scene material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water3DGpuResources::surface_thickness_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_thickness_material_,
                               "water 3D surface thickness material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water3DGpuResources::surface_pack_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_pack_material_,
                               "water 3D surface pack material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water3DGpuResources::surface_source_a_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_source_a_material_,
                               "water 3D surface source A material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water3DGpuResources::surface_source_b_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_source_b_material_,
                               "water 3D surface source B material is not initialized")
        .set(frame_slot);
}

VkDescriptorSet
Water3DGpuResources::surface_composite_descriptor_set(cubey::render::FrameSlot frame_slot) const {
    return require_initialized(surface_composite_material_,
                               "water 3D surface composite material is not initialized")
        .set(frame_slot);
}

void Water3DGpuResources::update_surface_descriptors(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    cubey::render::RenderGraphSampledTextureView raw_depth,
    cubey::render::RenderGraphSampledTextureView raw_thickness,
    cubey::render::RenderGraphSampledTextureView surface_a,
    cubey::render::RenderGraphSampledTextureView surface_b,
    cubey::render::RenderGraphSampledTextureView final_surface,
    cubey::render::RenderGraphSampledTextureView scene_color,
    cubey::render::RenderGraphSampledTextureView scene_depth,
    cubey::render::RenderGraphSampledTextureView whitewater,
    const Water3DEnvironmentTextureBindings& environment) {
    validate_water_3d_environment_texture_bindings(environment);
    const cubey::vulkan::Sampler& sampler =
        require_initialized(surface_sampler_, "water 3D surface sampler is not initialized");
    const VkSampler sampler_handle = sampler.handle();
    const cubey::vulkan::Sampler& whitewater_sampler =
        require_initialized(whitewater_sampler_, "water 3D whitewater sampler is not initialized");
    const VkDescriptorSet composite_set = surface_composite_descriptor_set(frame_slot);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(surface_thickness_descriptor_set(frame_slot), 0, sampler_handle,
                                raw_depth.view, raw_depth.layout)
        .combined_image_sampler(surface_pack_descriptor_set(frame_slot), 0, sampler_handle,
                                raw_depth.view, raw_depth.layout)
        .combined_image_sampler(surface_pack_descriptor_set(frame_slot), 1, sampler_handle,
                                raw_thickness.view, raw_thickness.layout)
        .combined_image_sampler(surface_source_a_descriptor_set(frame_slot), 0, sampler_handle,
                                surface_a.view, surface_a.layout)
        .combined_image_sampler(surface_source_b_descriptor_set(frame_slot), 0, sampler_handle,
                                surface_b.view, surface_b.layout)
        .combined_image_sampler(composite_set, 0, sampler_handle, final_surface.view,
                                final_surface.layout)
        .combined_image_sampler(composite_set, 1, sampler_handle, scene_color.view,
                                scene_color.layout)
        .combined_image_sampler(composite_set, 2, sampler_handle, scene_depth.view,
                                scene_depth.layout)
        .combined_image_sampler(composite_set, 3, environment.pbr.prefiltered_sampler,
                                environment.pbr.prefiltered_view,
                                environment.pbr.prefiltered_layout)
        .combined_image_sampler(composite_set, 4, whitewater_sampler.handle(), whitewater.view,
                                whitewater.layout)
        .uniform_buffer(composite_set, 5, environment_lighting_uniform_buffer(frame_slot).handle(),
                        environment_lighting_uniform_buffer(frame_slot).size());
    writes.update(device);
}

const cubey::vulkan::DepthAttachment& Water3DGpuResources::depth_attachment() const {
    return require_initialized(depth_attachment_, "water 3D depth attachment is not initialized");
}

} // namespace cubey::projects::fluid::water_3d
