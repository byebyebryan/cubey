#include "water_3d_commands.h"

#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::water_3d {
namespace {

inline constexpr std::uint32_t kWater3DVelocityExtrapolationIterations = 4;
inline constexpr VkFormat kWater3DSurfaceScalarFormat = VK_FORMAT_R32_SFLOAT;
inline constexpr VkFormat kWater3DSurfacePackedFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
inline constexpr VkFormat kWater3DSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr float kWater3DSurfaceDepthSentinel = 1000000.0F;

struct RenderPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    std::array<float, 4> camera_right_radius{};
    std::array<float, 4> camera_up_debug{};
    std::array<float, 4> grid_slice{};
    std::array<float, 4> color_options{};
};

static_assert(sizeof(RenderPushConstants) <= sizeof(float) * kWater3DRenderPushConstantFloatCount);

struct SurfacePushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    std::array<float, 4> camera_position_view{};
    std::array<float, 4> camera_right_tan{};
    std::array<float, 4> camera_up_aspect{};
    std::array<float, 4> camera_forward_radius{};
    std::array<float, 4> particle_options{};
    std::array<float, 4> surface_options{};
    std::array<float, 4> environment_options{};
    std::array<float, 4> display_transform{};
};

static_assert(sizeof(SurfacePushConstants) <= sizeof(float) * kWater3DRenderPushConstantFloatCount);

struct DispatchGroups {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
};

struct ShaderWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

enum class SurfacePassKind {
    Shading,
    Filter,
};

struct SurfaceTextureSlot {
    cubey::render::RenderGraphTextureHandle handle{};
    bool source_a = false;
};

[[nodiscard]] DispatchGroups compute_dispatch_groups(std::uint32_t width, std::uint32_t height,
                                                     std::uint32_t depth) {
    return {
        .x = (width + kWater3DComputeGroupSize - 1U) / kWater3DComputeGroupSize,
        .y = (height + kWater3DComputeGroupSize - 1U) / kWater3DComputeGroupSize,
        .z = (depth + kWater3DComputeGroupSize - 1U) / kWater3DComputeGroupSize,
    };
}

[[nodiscard]] DispatchGroups linear_dispatch_groups(std::size_t count) {
    return {
        .x = static_cast<std::uint32_t>((count + kWater3DParticleGroupSize - 1U) /
                                        kWater3DParticleGroupSize),
        .y = 1U,
        .z = 1U,
    };
}

[[nodiscard]] DispatchGroups cell_dispatch_groups(const Water3DConfig& config) {
    return compute_dispatch_groups(config.grid_width, config.grid_height, config.grid_depth);
}

[[nodiscard]] DispatchGroups face_dispatch_groups(const Water3DConfig& config) {
    return compute_dispatch_groups(config.grid_width + 1U, config.grid_height + 1U,
                                   config.grid_depth + 1U);
}

[[nodiscard]] DispatchGroups particle_scan_dispatch_groups(const Water3DConfig& config,
                                                           const Water3DRuntimeState& state) {
    return linear_dispatch_groups(water_3d_runtime_particle_scan_count(config, state));
}

[[nodiscard]] DispatchGroups bin_dispatch_groups(const Water3DConfig& config) {
    return linear_dispatch_groups(std::max(cell_count(config), particle_bin_index_count(config)));
}

[[nodiscard]] DispatchGroups reset_dispatch_groups(const Water3DConfig& config) {
    return linear_dispatch_groups(std::max(
        {static_cast<std::size_t>(config.particle_capacity), cell_count(config),
         u_face_count(config), v_face_count(config), w_face_count(config),
         particle_bin_index_count(config), static_cast<std::size_t>(config.whitewater_capacity)}));
}

[[nodiscard]] DispatchGroups whitewater_dispatch_groups(const Water3DConfig& config) {
    return linear_dispatch_groups(config.whitewater_capacity);
}

[[nodiscard]] DispatchGroups whitewater_counter_dispatch_groups() {
    return linear_dispatch_groups(4U);
}

void record_shader_write_barrier(VkCommandBuffer command_buffer, ShaderWriteBarrier config) {
    auto barrier = cubey::vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, config.dst_stage, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
}

void record_compute_barrier(VkCommandBuffer command_buffer) {
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });
}

void record_final_barrier(VkCommandBuffer command_buffer) {
    record_shader_write_barrier(
        command_buffer, {
                            .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            .dst_access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        });
}

[[nodiscard]] float render_view_push_value(Water3DRenderView view) {
    return static_cast<float>(static_cast<std::uint32_t>(view));
}

[[nodiscard]] SurfacePushConstants
surface_push_constants(const Water3DConfig& config, const Water3DRuntimeState& runtime_state,
                       Water3DRenderView render_view, const Water3DRenderCamera& camera,
                       VkExtent2D extent, VkFormat output_format, float smooth_direction_x,
                       float smooth_direction_y,
                       SurfacePassKind pass_kind = SurfacePassKind::Shading) {
    const std::uint32_t particle_scan_count =
        water_3d_runtime_particle_scan_count(config, runtime_state);
    const float aspect = extent.height > 0U
                             ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                             : 1.0F;
    const float radians = config.environment_rotation_degrees * 0.017453292519943295F;
    const cubey::render::PbrDisplayTransform display_transform =
        cubey::render::pbr_display_transform_for_target(output_format, config.exposure,
                                                        cubey::render::PbrTonemap::Aces);
    const cubey::math::Vec4 display_transform_uniform =
        cubey::render::pbr_display_transform_uniform(display_transform);
    const bool filter_pass = pass_kind == SurfacePassKind::Filter;
    const std::array<float, 4> surface_options =
        filter_pass
            ? std::array<float, 4>{
                  config.surface_smoothing_radius_world,
                  config.surface_depth_sigma,
                  config.surface_gap_fill_radius_px,
                  config.surface_thickness_smoothing,
              }
            : std::array<float, 4>{
                  config.foam_amount,
                  config.foam_sharpness,
                  config.surface_absorption,
                  config.surface_refraction_strength,
              };
    return {
        .view_projection = camera.view_projection,
        .camera_position_view =
            {
                camera.position.x,
                camera.position.y,
                camera.position.z,
                render_view_push_value(render_view),
            },
        .camera_right_tan =
            {
                camera.right.x,
                camera.right.y,
                camera.right.z,
                std::tan(camera.fovy_radians * 0.5F),
            },
        .camera_up_aspect =
            {
                camera.up.x,
                camera.up.y,
                camera.up.z,
                aspect,
            },
        .camera_forward_radius =
            {
                camera.forward.x,
                camera.forward.y,
                camera.forward.z,
                config.particle_radius,
            },
        .particle_options =
            {
                water_3d_shader_count_float(
                    particle_scan_count,
                    "water 3D particle scan count exceeds exact shader integer range"),
                config.surface_thickness_scale,
                smooth_direction_x,
                smooth_direction_y,
            },
        .surface_options = surface_options,
        .environment_options =
            {
                std::cos(radians),
                std::sin(radians),
                config.environment_intensity,
                0.0F,
            },
        .display_transform =
            {
                display_transform_uniform.x,
                display_transform_uniform.y,
                display_transform_uniform.z,
                display_transform_uniform.w,
            },
    };
}

[[nodiscard]] SurfacePushConstants whitewater_push_constants(const Water3DConfig& config,
                                                             Water3DRenderView render_view,
                                                             const Water3DRenderCamera& camera,
                                                             VkExtent2D extent,
                                                             VkFormat output_format) {
    const float aspect = extent.height > 0U
                             ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                             : 1.0F;
    const float radians = config.environment_rotation_degrees * 0.017453292519943295F;
    const cubey::render::PbrDisplayTransform display_transform =
        cubey::render::pbr_display_transform_for_target(output_format, config.exposure,
                                                        cubey::render::PbrTonemap::Aces);
    const cubey::math::Vec4 display_transform_uniform =
        cubey::render::pbr_display_transform_uniform(display_transform);
    return {
        .view_projection = camera.view_projection,
        .camera_position_view =
            {
                camera.position.x,
                camera.position.y,
                camera.position.z,
                render_view_push_value(render_view),
            },
        .camera_right_tan =
            {
                camera.right.x,
                camera.right.y,
                camera.right.z,
                std::tan(camera.fovy_radians * 0.5F),
            },
        .camera_up_aspect =
            {
                camera.up.x,
                camera.up.y,
                camera.up.z,
                aspect,
            },
        .camera_forward_radius =
            {
                camera.forward.x,
                camera.forward.y,
                camera.forward.z,
                config.whitewater_radius,
            },
        .particle_options =
            {
                water_3d_shader_count_float(
                    config.whitewater_capacity,
                    "water 3D whitewater capacity exceeds exact shader integer range"),
                0.0F,
                0.0F,
                0.0F,
            },
        .surface_options =
            {
                config.foam_amount,
                config.foam_sharpness,
                config.surface_absorption,
                config.surface_refraction_strength,
            },
        .environment_options =
            {
                std::cos(radians),
                std::sin(radians),
                config.environment_intensity,
                0.0F,
            },
        .display_transform =
            {
                display_transform_uniform.x,
                display_transform_uniform.y,
                display_transform_uniform.z,
                display_transform_uniform.w,
            },
    };
}

[[nodiscard]] cubey::render::DepthTargetView
resolved_depth_target_view(const cubey::render::RenderGraphExecutionContext& context,
                           cubey::render::RenderGraphTextureHandle handle) {
    const cubey::render::RenderGraphTextureResource& resource = context.texture(handle);
    if (resource.desc.aspects != VK_IMAGE_ASPECT_DEPTH_BIT) {
        throw std::runtime_error("water 3D surface graph expected a depth texture");
    }
    const cubey::render::RenderGraphResolvedTexture resolved = context.resolved_texture(handle);
    return cubey::render::depth_target_view(
        {resource.desc.extent.width, resource.desc.extent.height}, resource.desc.format,
        resolved.image, resolved.view);
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
surface_color_texture_desc(const char* label, VkExtent2D extent, VkFormat format) {
    return {
        .label = label,
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
surface_depth_texture_desc(const char* label, VkExtent2D extent, VkFormat format) {
    return {
        .label = label,
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

[[nodiscard]] Water3DSimulationUniforms simulation_uniforms(const Water3DConfig& config) {
    return {
        .grid_options =
            {
                water_3d_shader_count_float(
                    config.grid_width, "water 3D grid width exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.grid_height, "water 3D grid height exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.grid_depth, "water 3D grid depth exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.active_particle_count,
                    "water 3D active particle count exceeds exact shader integer range"),
            },
        .particle_options =
            {
                water_3d_shader_count_float(
                    config.particle_capacity,
                    "water 3D particle capacity exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.max_particles_per_cell,
                    "water 3D max particles per cell exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.particles_per_cell,
                    "water 3D particles per cell exceeds exact shader integer range"),
                config.flip_ratio,
            },
        .fill_options =
            {
                config.initial_fill_width,
                config.initial_fill_height,
                config.initial_fill_depth,
                config.gravity,
            },
        .solve_options =
            {
                config.velocity_limit,
                config.particle_damping,
                config.particle_volume_strength,
                static_cast<float>(static_cast<std::uint32_t>(config.transfer_mode)),
            },
        .lifecycle_options =
            {
                water_3d_shader_count_float(
                    config.active_particle_count,
                    "water 3D active particle count exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.particle_capacity,
                    "water 3D particle capacity exceeds exact shader integer range"),
                config.boundary_restitution,
                0.0F,
            },
        .render_options =
            {
                config.particle_radius,
                config.slice_depth,
                0.0F,
                0.0F,
            },
        .whitewater_options =
            {
                water_3d_shader_count_float(
                    config.whitewater_capacity,
                    "water 3D whitewater capacity exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    std::min(config.whitewater_max_emit_per_frame, config.whitewater_capacity),
                    "water 3D whitewater max emit count exceeds exact shader integer range"),
                config.whitewater_enabled ? config.whitewater_intensity : 0.0F,
                config.whitewater_radius,
            },
        .whitewater_lifecycle =
            {
                config.whitewater_speed_threshold,
                config.whitewater_lifetime,
                config.whitewater_drag,
                config.whitewater_gravity_scale,
            },
    };
}

[[nodiscard]] Water3DDispatchPushConstants
dispatch_push_constants(const ProjectFrame& frame, float delta_seconds, float pressure_read_b,
                        std::uint32_t particle_scan_count) {
    return {
        .dispatch_options =
            {
                delta_seconds,
                static_cast<float>(frame.elapsed_seconds),
                pressure_read_b,
                water_3d_shader_count_float(
                    particle_scan_count,
                    "water 3D particle scan count exceeds exact shader integer range"),
            },
    };
}

void record_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                     const cubey::render::ComputePipelineResource& pipeline,
                     VkDescriptorSet descriptor_set, DispatchGroups groups,
                     const Water3DDispatchPushConstants& push_constants) {
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptor_set);
    recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(groups.x, groups.y, groups.z);
}

void record_refresh_bins(const cubey::vulkan::CommandRecorder& recorder,
                         VkCommandBuffer command_buffer, Water3DGpuResources& resources,
                         VkDescriptorSet descriptor_set, const Water3DConfig& config,
                         const Water3DRuntimeState& runtime_state,
                         const Water3DDispatchPushConstants& push_constants,
                         cubey::render::FrameSlot frame_slot,
                         cubey::vulkan::GpuTimestampProfiler* profiler,
                         const char* profile_label) {
    if (profiler != nullptr && profile_label != nullptr) {
        profiler->begin_pass(command_buffer, frame_slot.index, profile_label);
    }
    record_dispatch(recorder, resources.clear_bins_pipeline_resource(), descriptor_set,
                    bin_dispatch_groups(config), push_constants);
    record_compute_barrier(command_buffer);
    record_dispatch(recorder, resources.build_bins_pipeline_resource(), descriptor_set,
                    particle_scan_dispatch_groups(config, runtime_state), push_constants);
    record_compute_barrier(command_buffer);
    if (profiler != nullptr && profile_label != nullptr) {
        profiler->end_pass(command_buffer, frame_slot.index);
    }
}

void record_whitewater_compute(const cubey::vulkan::CommandRecorder& recorder,
                               VkCommandBuffer command_buffer, Water3DGpuResources& resources,
                               VkDescriptorSet descriptor_set, const Water3DConfig& config,
                               const Water3DRuntimeState& runtime_state,
                               const Water3DDispatchPushConstants& push_constants,
                               cubey::render::FrameSlot frame_slot,
                               cubey::vulkan::GpuTimestampProfiler* profiler) {
    auto begin_pass = [&](const char* label) {
        if (profiler != nullptr) {
            profiler->begin_pass(command_buffer, frame_slot.index, label);
        }
    };
    auto end_pass = [&]() {
        if (profiler != nullptr) {
            profiler->end_pass(command_buffer, frame_slot.index);
        }
    };

    begin_pass("whitewater clear");
    record_dispatch(recorder, resources.clear_whitewater_pipeline_resource(), descriptor_set,
                    whitewater_counter_dispatch_groups(), push_constants);
    record_compute_barrier(command_buffer);
    end_pass();

    begin_pass("whitewater advect");
    record_dispatch(recorder, resources.advect_whitewater_pipeline_resource(), descriptor_set,
                    whitewater_dispatch_groups(config), push_constants);
    record_compute_barrier(command_buffer);
    end_pass();

    if (config.whitewater_enabled && config.whitewater_intensity > 0.0F &&
        config.whitewater_max_emit_per_frame > 0U) {
        begin_pass("whitewater emit");
        record_dispatch(recorder, resources.emit_whitewater_pipeline_resource(), descriptor_set,
                        particle_scan_dispatch_groups(config, runtime_state), push_constants);
        record_compute_barrier(command_buffer);
        end_pass();
    }

    begin_pass("whitewater active indices");
    record_dispatch(recorder, resources.active_whitewater_indices_pipeline_resource(),
                    descriptor_set, whitewater_dispatch_groups(config), push_constants);
    record_compute_barrier(command_buffer);
    end_pass();

    begin_pass("whitewater draw args");
    record_dispatch(recorder, resources.whitewater_draw_args_pipeline_resource(), descriptor_set,
                    linear_dispatch_groups(1U), push_constants);
    record_compute_barrier(command_buffer);
    end_pass();
}

void record_surface_depth_pass(const cubey::vulkan::CommandRecorder& recorder,
                               Water3DGpuResources& resources, const Water3DConfig& config,
                               cubey::render::FrameSlot frame_slot,
                               const Water3DRuntimeState& runtime_state,
                               Water3DRenderView render_view, const Water3DRenderCamera& camera,
                               cubey::render::ColorTargetView color_target,
                               cubey::render::DepthTargetView depth_target) {
    const SurfacePushConstants push_constants =
        surface_push_constants(config, runtime_state, render_view, camera, color_target.extent,
                               color_target.format, 0.0F, 0.0F);
    const std::uint32_t particle_scan_count =
        water_3d_runtime_particle_scan_count(config, runtime_state);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target, depth_target),
        cubey::render::RenderClearValues{
            .color =
                cubey::render::color_clear_value(kWater3DSurfaceDepthSentinel, 0.0F, 0.0F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [&resources, frame_slot, particle_scan_count,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline =
                resources.surface_depth_pipeline_resource();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                                              resources.field_descriptor_set(frame_slot));
            pass_recorder.push_constants(pipeline.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, push_constants);
            pass_recorder.draw(6, std::max(1U, particle_scan_count));
        });
}

void record_surface_thickness_pass(const cubey::vulkan::CommandRecorder& recorder,
                                   Water3DGpuResources& resources, const Water3DConfig& config,
                                   cubey::render::FrameSlot frame_slot,
                                   const Water3DRuntimeState& runtime_state,
                                   Water3DRenderView render_view, const Water3DRenderCamera& camera,
                                   cubey::render::ColorTargetView color_target) {
    const SurfacePushConstants push_constants =
        surface_push_constants(config, runtime_state, render_view, camera, color_target.extent,
                               color_target.format, 0.0F, 0.0F);
    const std::uint32_t particle_scan_count =
        water_3d_runtime_particle_scan_count(config, runtime_state);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 0.0F),
        },
        [&resources, frame_slot, particle_scan_count,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline =
                resources.surface_thickness_pipeline_resource();
            const std::array<VkDescriptorSet, 2> sets{
                resources.field_descriptor_set(frame_slot),
                resources.surface_thickness_descriptor_set(frame_slot),
            };
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(),
                                               0, sets);
            pass_recorder.push_constants(pipeline.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, push_constants);
            pass_recorder.draw(6, std::max(1U, particle_scan_count));
        });
}

void record_surface_fullscreen_pass(const cubey::vulkan::CommandRecorder& recorder,
                                    const cubey::render::GraphicsPipelineResource& pipeline,
                                    VkDescriptorSet descriptor_set,
                                    const SurfacePushConstants& push_constants,
                                    cubey::render::ColorTargetView color_target,
                                    VkClearValue clear_color) {
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target),
        cubey::render::RenderClearValues{.color = clear_color},
        [&pipeline, descriptor_set,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &pipeline,
                    .descriptor_set = descriptor_set,
                },
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

template <typename RecordCallback>
void record_render_target_pass_with_stored_depth(const cubey::vulkan::CommandRecorder& recorder,
                                                 const cubey::render::RenderTargetView& target,
                                                 const cubey::render::RenderClearValues& clear,
                                                 RecordCallback&& record_callback) {
    VkRenderingAttachmentInfo color_attachment =
        cubey::vulkan::color_rendering_attachment(target.color.view, clear.color);
    auto rendering = cubey::vulkan::vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea.offset = {0, 0};
    rendering.renderArea.extent = target.color.extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_attachment;

    std::optional<VkRenderingAttachmentInfo> depth_attachment;
    if (target.depth.has_value()) {
        depth_attachment =
            cubey::vulkan::depth_rendering_attachment(target.depth->view, clear.depth);
        depth_attachment->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        rendering.pDepthAttachment = &depth_attachment.value();
    }

    recorder.begin_rendering(rendering);
    std::forward<RecordCallback>(record_callback)(recorder);
    recorder.end_rendering();
}

template <typename RecordCallback>
void record_render_target_pass_with_loaded_depth(const cubey::vulkan::CommandRecorder& recorder,
                                                 const cubey::render::RenderTargetView& target,
                                                 const cubey::render::RenderClearValues& clear,
                                                 RecordCallback&& record_callback) {
    VkRenderingAttachmentInfo color_attachment =
        cubey::vulkan::color_rendering_attachment(target.color.view, clear.color);
    auto rendering = cubey::vulkan::vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea.offset = {0, 0};
    rendering.renderArea.extent = target.color.extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_attachment;

    std::optional<VkRenderingAttachmentInfo> depth_attachment;
    if (target.depth.has_value()) {
        depth_attachment =
            cubey::vulkan::depth_rendering_attachment(target.depth->view, clear.depth);
        depth_attachment->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        rendering.pDepthAttachment = &depth_attachment.value();
    }

    recorder.begin_rendering(rendering);
    std::forward<RecordCallback>(record_callback)(recorder);
    recorder.end_rendering();
}

void record_surface_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                               const Water3DGpuResources& resources,
                               cubey::render::FrameSlot frame_slot,
                               const SurfacePushConstants& push_constants,
                               cubey::render::ColorTargetView color_target,
                               cubey::render::DepthTargetView depth_target) {
    record_render_target_pass_with_stored_depth(
        recorder, cubey::render::render_target_view(color_target, depth_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [&resources, frame_slot,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_fullscreen_pipeline_draw(
                pass_recorder,
                {
                    .pipeline = &resources.surface_scene_pipeline_resource(),
                    .descriptor_set = resources.surface_scene_descriptor_set(frame_slot),
                },
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push_constants);
        });
}

void record_whitewater_pass(const cubey::vulkan::CommandRecorder& recorder,
                            const Water3DGpuResources& resources, const Water3DConfig& config,
                            cubey::render::FrameSlot frame_slot, Water3DRenderView render_view,
                            const Water3DRenderCamera& camera,
                            cubey::render::ColorTargetView color_target,
                            cubey::render::DepthTargetView depth_target, VkFormat output_format) {
    const SurfacePushConstants push_constants =
        whitewater_push_constants(config, render_view, camera, color_target.extent, output_format);
    record_render_target_pass_with_loaded_depth(
        recorder, cubey::render::render_target_view(color_target, depth_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 0.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [&resources, frame_slot,
         push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline =
                resources.whitewater_pipeline_resource();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                                              resources.field_descriptor_set(frame_slot));
            pass_recorder.push_constants(pipeline.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, push_constants);
            pass_recorder.draw_indirect(resources.whitewater_draw_args().handle(), 0, 1,
                                        sizeof(VkDrawIndirectCommand));
        });
}

struct SurfaceRenderGraph {
    cubey::render::CompiledRenderGraph graph;
    cubey::render::RenderGraphTextureHandle scene_color{};
    cubey::render::RenderGraphTextureHandle scene_depth{};
    cubey::render::RenderGraphTextureHandle raw_depth{};
    cubey::render::RenderGraphTextureHandle raw_thickness{};
    cubey::render::RenderGraphTextureHandle packed_a{};
    cubey::render::RenderGraphTextureHandle packed_b{};
    cubey::render::RenderGraphTextureHandle final_surface{};
    cubey::render::RenderGraphTextureHandle whitewater{};
};

[[nodiscard]] VkDescriptorSet surface_source_descriptor_set(Water3DGpuResources& resources,
                                                            cubey::render::FrameSlot frame_slot,
                                                            SurfaceTextureSlot source) {
    return source.source_a ? resources.surface_source_a_descriptor_set(frame_slot)
                           : resources.surface_source_b_descriptor_set(frame_slot);
}

[[nodiscard]] SurfaceRenderGraph build_water_3d_surface_graph(
    cubey::render::ColorTargetView color_target, Water3DGpuResources& resources,
    const Water3DConfig& config, cubey::render::FrameSlot frame_slot,
    const Water3DRuntimeState& runtime_state, Water3DRenderView render_view,
    const Water3DRenderCamera& camera, Water3DRenderTargetMode target_mode) {
    Water3DGpuResources* resource_ptr = &resources;
    const Water3DConfig* config_ptr = &config;
    const Water3DRuntimeState* runtime_state_ptr = &runtime_state;
    const VkFormat output_format = color_target.format;
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureState initial_state =
        target_mode == Water3DRenderTargetMode::Present
            ? cubey::render::render_graph_undefined_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureState final_state =
        target_mode == Water3DRenderTargetMode::Present
            ? cubey::render::render_graph_present_texture_state()
            : cubey::render::render_graph_color_attachment_texture_state();
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("backbuffer", color_target, initial_state, final_state);
    const cubey::render::RenderGraphTextureHandle scene_color =
        graph.create_texture(surface_color_texture_desc("water scene color", color_target.extent,
                                                        kWater3DSceneColorFormat));
    const cubey::render::RenderGraphTextureHandle scene_depth =
        graph.create_texture(surface_depth_texture_desc("water scene depth", color_target.extent,
                                                        resources.depth_attachment().format()));
    const cubey::render::RenderGraphTextureHandle raw_depth =
        graph.create_texture(surface_color_texture_desc(
            "water surface raw depth", color_target.extent, kWater3DSurfaceScalarFormat));
    const cubey::render::RenderGraphTextureHandle raw_thickness =
        graph.create_texture(surface_color_texture_desc(
            "water surface raw thickness", color_target.extent, kWater3DSurfaceScalarFormat));
    const cubey::render::RenderGraphTextureHandle packed_a =
        graph.create_texture(surface_color_texture_desc(
            "water surface packed A", color_target.extent, kWater3DSurfacePackedFormat));
    const cubey::render::RenderGraphTextureHandle packed_b =
        graph.create_texture(surface_color_texture_desc(
            "water surface packed B", color_target.extent, kWater3DSurfacePackedFormat));
    const cubey::render::RenderGraphTextureHandle visibility_depth = graph.create_texture(
        surface_depth_texture_desc("water surface visibility depth", color_target.extent,
                                   resources.depth_attachment().format()));
    const cubey::render::RenderGraphTextureHandle whitewater =
        graph.create_texture(surface_color_texture_desc("water whitewater", color_target.extent,
                                                        kWater3DSceneColorFormat));

    graph.add_pass("water scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(scene_color)
        .write_depth(scene_depth)
        .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                  output_format, scene_color,
                  scene_depth](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::render::ColorTargetView target =
                cubey::render::resolved_color_target_view(context, scene_color);
            const SurfacePushConstants push_constants =
                surface_push_constants(*config_ptr, *runtime_state_ptr, render_view, camera,
                                       target.extent, output_format, 0.0F, 0.0F);
            record_surface_scene_pass(context.recorder(), *resource_ptr, frame_slot, push_constants,
                                      target, resolved_depth_target_view(context, scene_depth));
        });
    graph.add_pass("water surface depth", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(raw_depth)
        .write_depth(visibility_depth)
        .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                  raw_depth,
                  visibility_depth](const cubey::render::RenderGraphExecutionContext& context) {
            record_surface_depth_pass(context.recorder(), *resource_ptr, *config_ptr, frame_slot,
                                      *runtime_state_ptr, render_view, camera,
                                      cubey::render::resolved_color_target_view(context, raw_depth),
                                      resolved_depth_target_view(context, visibility_depth));
        });
    graph.add_pass("water surface thickness", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(raw_depth)
        .write_color(raw_thickness)
        .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                  raw_thickness](const cubey::render::RenderGraphExecutionContext& context) {
            record_surface_thickness_pass(
                context.recorder(), *resource_ptr, *config_ptr, frame_slot, *runtime_state_ptr,
                render_view, camera,
                cubey::render::resolved_color_target_view(context, raw_thickness));
        });
    graph.add_pass("water surface pack", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(raw_depth)
        .read_texture(raw_thickness)
        .write_color(packed_a)
        .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                  output_format,
                  packed_a](const cubey::render::RenderGraphExecutionContext& context) {
            const SurfacePushConstants push_constants = surface_push_constants(
                *config_ptr, *runtime_state_ptr, render_view, camera,
                cubey::render::resolved_color_target_view(context, packed_a).extent, output_format,
                0.0F, 0.0F);
            record_surface_fullscreen_pass(
                context.recorder(), resource_ptr->surface_pack_pipeline_resource(),
                resource_ptr->surface_pack_descriptor_set(frame_slot), push_constants,
                cubey::render::resolved_color_target_view(context, packed_a),
                cubey::render::color_clear_value(kWater3DSurfaceDepthSentinel, 0.0F, 0.0F,
                                                 kWater3DSurfaceDepthSentinel));
        });

    SurfaceTextureSlot current_surface{
        .handle = packed_a,
        .source_a = true,
    };
    SurfaceTextureSlot next_surface{
        .handle = packed_b,
        .source_a = false,
    };

    graph.add_pass("water surface repair", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(current_surface.handle)
        .write_color(next_surface.handle)
        .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                  output_format, source = current_surface, destination = next_surface](
                     const cubey::render::RenderGraphExecutionContext& context) {
            const SurfacePushConstants push_constants = surface_push_constants(
                *config_ptr, *runtime_state_ptr, render_view, camera,
                cubey::render::resolved_color_target_view(context, destination.handle).extent,
                output_format, 0.0F, 0.0F, SurfacePassKind::Filter);
            record_surface_fullscreen_pass(
                context.recorder(), resource_ptr->surface_repair_pipeline_resource(),
                surface_source_descriptor_set(*resource_ptr, frame_slot, source), push_constants,
                cubey::render::resolved_color_target_view(context, destination.handle),
                cubey::render::color_clear_value(kWater3DSurfaceDepthSentinel, 0.0F, 0.0F,
                                                 kWater3DSurfaceDepthSentinel));
        });
    std::swap(current_surface, next_surface);

    const std::uint32_t smoothing_iterations =
        std::min(config.surface_smoothing_iterations, std::uint32_t{8});
    for (std::uint32_t iteration = 0; iteration < smoothing_iterations; ++iteration) {
        graph.add_pass("water surface smooth x", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(current_surface.handle)
            .write_color(next_surface.handle)
            .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                      output_format, source = current_surface, destination = next_surface](
                         const cubey::render::RenderGraphExecutionContext& context) {
                const SurfacePushConstants push_constants = surface_push_constants(
                    *config_ptr, *runtime_state_ptr, render_view, camera,
                    cubey::render::resolved_color_target_view(context, destination.handle).extent,
                    output_format, 1.0F, 0.0F, SurfacePassKind::Filter);
                record_surface_fullscreen_pass(
                    context.recorder(), resource_ptr->surface_smooth_pipeline_resource(),
                    surface_source_descriptor_set(*resource_ptr, frame_slot, source),
                    push_constants,
                    cubey::render::resolved_color_target_view(context, destination.handle),
                    cubey::render::color_clear_value(kWater3DSurfaceDepthSentinel, 0.0F, 0.0F,
                                                     kWater3DSurfaceDepthSentinel));
            });
        std::swap(current_surface, next_surface);

        graph.add_pass("water surface smooth y", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(current_surface.handle)
            .write_color(next_surface.handle)
            .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                      output_format, source = current_surface, destination = next_surface](
                         const cubey::render::RenderGraphExecutionContext& context) {
                const SurfacePushConstants push_constants = surface_push_constants(
                    *config_ptr, *runtime_state_ptr, render_view, camera,
                    cubey::render::resolved_color_target_view(context, destination.handle).extent,
                    output_format, 0.0F, 1.0F, SurfacePassKind::Filter);
                record_surface_fullscreen_pass(
                    context.recorder(), resource_ptr->surface_smooth_pipeline_resource(),
                    surface_source_descriptor_set(*resource_ptr, frame_slot, source),
                    push_constants,
                    cubey::render::resolved_color_target_view(context, destination.handle),
                    cubey::render::color_clear_value(kWater3DSurfaceDepthSentinel, 0.0F, 0.0F,
                                                     kWater3DSurfaceDepthSentinel));
            });
        std::swap(current_surface, next_surface);
    }

    graph.add_pass("water whitewater", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(whitewater)
        .write_depth(visibility_depth)
        .execute([resource_ptr, config_ptr, frame_slot, render_view, camera, output_format,
                  whitewater,
                  visibility_depth](const cubey::render::RenderGraphExecutionContext& context) {
            record_whitewater_pass(
                context.recorder(), *resource_ptr, *config_ptr, frame_slot, render_view, camera,
                cubey::render::resolved_color_target_view(context, whitewater),
                resolved_depth_target_view(context, visibility_depth), output_format);
        });

    graph.add_pass("water surface composite", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(scene_color)
        .read_texture(scene_depth)
        .read_texture(current_surface.handle)
        .read_texture(whitewater)
        .write_color(backbuffer)
        .execute([resource_ptr, config_ptr, runtime_state_ptr, frame_slot, render_view, camera,
                  output_format,
                  backbuffer](const cubey::render::RenderGraphExecutionContext& context) {
            const SurfacePushConstants push_constants = surface_push_constants(
                *config_ptr, *runtime_state_ptr, render_view, camera,
                cubey::render::resolved_color_target_view(context, backbuffer).extent,
                output_format, 0.0F, 0.0F);
            record_surface_fullscreen_pass(
                context.recorder(), resource_ptr->surface_composite_pipeline_resource(),
                resource_ptr->surface_composite_descriptor_set(frame_slot), push_constants,
                cubey::render::resolved_color_target_view(context, backbuffer),
                cubey::render::color_clear_value(0.006F, 0.009F, 0.014F, 1.0F));
        });

    return {
        .graph = graph.compile(),
        .scene_color = scene_color,
        .scene_depth = scene_depth,
        .raw_depth = raw_depth,
        .raw_thickness = raw_thickness,
        .packed_a = packed_a,
        .packed_b = packed_b,
        .final_surface = current_surface.handle,
        .whitewater = whitewater,
    };
}

} // namespace

void record_water_3d_compute(VkCommandBuffer command_buffer, Water3DGpuResources& resources,
                             const Water3DConfig& config, Water3DRuntimeState& runtime_state,
                             cubey::render::FrameSlot frame_slot, bool paused,
                             bool& reset_requested, const ProjectFrame& frame,
                             bool include_render_visibility_barrier,
                             cubey::vulkan::GpuTimestampProfiler* profiler) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    auto begin_pass = [&](const char* label) {
        if (profiler != nullptr) {
            profiler->begin_pass(command_buffer, frame_slot.index, label);
        }
    };
    auto end_pass = [&]() {
        if (profiler != nullptr) {
            profiler->end_pass(command_buffer, frame_slot.index);
        }
    };

    resources.upload_simulation_uniforms(frame_slot, simulation_uniforms(config));
    const VkDescriptorSet descriptor_set = resources.field_descriptor_set(frame_slot);
    const DispatchGroups cell_groups = cell_dispatch_groups(config);
    const DispatchGroups face_groups = face_dispatch_groups(config);
    const std::uint32_t substep_count = std::max(1U, config.substeps);
    const float frame_dt =
        std::min(static_cast<float>(frame.delta_seconds), config.fixed_delta_seconds);
    const float substep_dt = frame_dt / static_cast<float>(substep_count);
    Water3DDispatchPushConstants push_constants = dispatch_push_constants(
        frame, substep_dt, 0.0F, water_3d_runtime_particle_scan_count(config, runtime_state));

    if (reset_requested) {
        runtime_state = {};
        begin_pass("reset");
        record_dispatch(recorder, resources.reset_pipeline_resource(), descriptor_set,
                        reset_dispatch_groups(config), push_constants);
        record_compute_barrier(command_buffer);
        end_pass();
        runtime_state.particle_scan_count = config.active_particle_count;
        push_constants.dispatch_options[3] = water_3d_shader_count_float(
            water_3d_runtime_particle_scan_count(config, runtime_state),
            "water 3D particle scan count exceeds exact shader integer range");
        reset_requested = false;
    }
    if (paused) {
        record_refresh_bins(recorder, command_buffer, resources, descriptor_set, config,
                            runtime_state, push_constants, frame_slot, profiler, "refresh bins");
        if (include_render_visibility_barrier) {
            record_final_barrier(command_buffer);
        }
        return;
    }

    for (std::uint32_t substep = 0; substep < substep_count; ++substep) {
        const float substep_time =
            static_cast<float>(frame.elapsed_seconds) + (static_cast<float>(substep) * substep_dt);
        push_constants = dispatch_push_constants(
            frame, substep_dt, 0.0F, water_3d_runtime_particle_scan_count(config, runtime_state));
        push_constants.dispatch_options[1] = substep_time;

        begin_pass("clear grid");
        record_dispatch(recorder, resources.clear_grid_pipeline_resource(), descriptor_set,
                        face_groups, push_constants);
        record_compute_barrier(command_buffer);
        end_pass();

        record_refresh_bins(recorder, command_buffer, resources, descriptor_set, config,
                            runtime_state, push_constants, frame_slot, profiler,
                            "refresh bins pre-p2g");

        begin_pass("particle to grid");
        record_dispatch(recorder, resources.particle_to_grid_pipeline_resource(), descriptor_set,
                        face_groups, push_constants);
        record_compute_barrier(command_buffer);
        end_pass();

        begin_pass("force");
        record_dispatch(recorder, resources.force_pipeline_resource(), descriptor_set, face_groups,
                        push_constants);
        record_compute_barrier(command_buffer);
        end_pass();

        begin_pass("divergence");
        record_dispatch(recorder, resources.divergence_pipeline_resource(), descriptor_set,
                        cell_groups, push_constants);
        record_compute_barrier(command_buffer);
        end_pass();

        const cubey::render::ComputePipelineResource& pressure_pipeline =
            resources.pressure_pipeline_resource();
        begin_pass("pressure");
        for (std::uint32_t iteration = 0; iteration < config.pressure_iterations; ++iteration) {
            push_constants.dispatch_options[2] = (iteration % 2U == 1U) ? 1.0F : 0.0F;
            record_dispatch(recorder, pressure_pipeline, descriptor_set, cell_groups,
                            push_constants);
            record_compute_barrier(command_buffer);
        }
        end_pass();

        const bool final_pressure_is_b = (config.pressure_iterations % 2U) == 1U;
        runtime_state.pressure_read_b = final_pressure_is_b;
        push_constants.dispatch_options[2] = final_pressure_is_b ? 1.0F : 0.0F;
        begin_pass("projection");
        record_dispatch(recorder, resources.projection_pipeline_resource(), descriptor_set,
                        face_groups, push_constants);
        record_compute_barrier(command_buffer);
        end_pass();

        const cubey::render::ComputePipelineResource& extrapolate_pipeline =
            resources.extrapolate_velocity_pipeline_resource();
        begin_pass("extrapolate velocity");
        for (std::uint32_t iteration = 0; iteration < kWater3DVelocityExtrapolationIterations;
             ++iteration) {
            push_constants.dispatch_options[2] = (iteration % 2U == 1U) ? 1.0F : 0.0F;
            record_dispatch(recorder, extrapolate_pipeline, descriptor_set, face_groups,
                            push_constants);
            record_compute_barrier(command_buffer);
        }
        end_pass();
        push_constants.dispatch_options[2] = 0.0F;

        begin_pass("grid to particle");
        record_dispatch(recorder, resources.grid_to_particle_pipeline_resource(), descriptor_set,
                        particle_scan_dispatch_groups(config, runtime_state), push_constants);
        record_compute_barrier(command_buffer);
        end_pass();

        begin_pass("advect particles");
        record_dispatch(recorder, resources.advect_particles_pipeline_resource(), descriptor_set,
                        particle_scan_dispatch_groups(config, runtime_state), push_constants);
        record_compute_barrier(command_buffer);
        end_pass();

        record_refresh_bins(recorder, command_buffer, resources, descriptor_set, config,
                            runtime_state, push_constants, frame_slot, profiler,
                            "refresh bins post-advect");
        record_whitewater_compute(recorder, command_buffer, resources, descriptor_set, config,
                                  runtime_state, push_constants, frame_slot, profiler);
    }

    if (include_render_visibility_barrier) {
        record_final_barrier(command_buffer);
    }
}

void record_water_3d_draw(VkCommandBuffer command_buffer, const Water3DGpuResources& resources,
                          const Water3DConfig& config, cubey::render::FrameSlot frame_slot,
                          const Water3DRuntimeState& runtime_state, Water3DRenderView render_view,
                          const Water3DRenderCamera& camera,
                          cubey::render::ColorTargetView color_target,
                          cubey::vulkan::GpuTimestampProfiler* profiler) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    if (profiler != nullptr) {
        profiler->begin_pass(command_buffer, frame_slot.index, "debug draw");
    }
    const cubey::render::DepthTargetView depth_target =
        cubey::render::depth_target_view(resources.depth_attachment());
    recorder.transition_image_layout(
        cubey::vulkan::begin_depth_attachment_transition(depth_target.image));
    const std::uint32_t particle_scan_count =
        water_3d_runtime_particle_scan_count(config, runtime_state);
    const RenderPushConstants push_constants{
        .view_projection = camera.view_projection,
        .camera_right_radius =
            {
                camera.right.x,
                camera.right.y,
                camera.right.z,
                config.particle_radius,
            },
        .camera_up_debug =
            {
                camera.up.x,
                camera.up.y,
                camera.up.z,
                render_view_push_value(render_view),
            },
        .grid_slice =
            {
                water_3d_shader_count_float(
                    config.grid_width, "water 3D grid width exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.grid_height, "water 3D grid height exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.grid_depth, "water 3D grid depth exceeds exact shader integer range"),
                config.slice_depth,
            },
        .color_options =
            {
                runtime_state.pressure_read_b ? 1.0F : 0.0F,
                water_3d_shader_count_float(
                    particle_scan_count,
                    "water 3D particle scan count exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.max_particles_per_cell,
                    "water 3D max particles per cell exceeds exact shader integer range"),
                water_3d_shader_count_float(
                    config.particles_per_cell,
                    "water 3D particles per cell exceeds exact shader integer range"),
            },
    };

    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(color_target, depth_target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.006F, 0.008F, 0.013F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        },
        [&resources, frame_slot, push_constants,
         render_view](const cubey::vulkan::CommandRecorder& pass_recorder) {
            const cubey::render::GraphicsPipelineResource& pipeline =
                resources.render_pipeline_resource();
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0,
                                              resources.field_descriptor_set(frame_slot));
            pass_recorder.push_constants(pipeline.layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, push_constants);
            const std::uint32_t instance_count =
                render_view == Water3DRenderView::Particles
                    ? static_cast<std::uint32_t>(push_constants.color_options[1])
                    : 1U;
            pass_recorder.draw(6, std::max(1U, instance_count));
        });
    if (profiler != nullptr) {
        profiler->end_pass(command_buffer, frame_slot.index);
    }
}

void record_water_3d_surface_draw(VkCommandBuffer command_buffer,
                                  const cubey::vulkan::Device& device,
                                  cubey::render::RenderGraphFrameExecutor& graph_executor,
                                  Water3DGpuResources& resources, const Water3DConfig& config,
                                  cubey::render::FrameSlot frame_slot,
                                  const Water3DRuntimeState& runtime_state,
                                  Water3DRenderView render_view, const Water3DRenderCamera& camera,
                                  cubey::render::ColorTargetView color_target,
                                  Water3DRenderTargetMode target_mode,
                                  const cubey::render::GeneratedPbrEnvironment& environment,
                                  cubey::vulkan::GpuTimestampProfiler* profiler) {
    if (!is_water_3d_surface_view(render_view)) {
        throw std::runtime_error("water 3D surface draw requires a surface render view");
    }

    const SurfaceRenderGraph render_graph =
        build_water_3d_surface_graph(color_target, resources, config, frame_slot, runtime_state,
                                     render_view, camera, target_mode);
    graph_executor.record(
        cubey::render::RenderGraphFrameRecordInfo{
            .device = &device,
            .command_buffer = command_buffer,
            .frame_slot = frame_slot,
            .label = "vkEndCommandBuffer water_3d surface",
            .command_buffer_mode = cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            .profiler = profiler,
        },
        render_graph.graph,
        [&resources, &device, frame_slot, &environment,
         &render_graph](const cubey::render::RenderGraphResourceSet& graph_resources) {
            resources.update_surface_descriptors(
                device, frame_slot,
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.raw_depth),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.raw_thickness),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.packed_a),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.packed_b),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.final_surface),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.scene_color),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.scene_depth),
                cubey::render::resolved_sampled_texture_view(render_graph.graph, graph_resources,
                                                             render_graph.whitewater),
                environment);
        });
}

} // namespace cubey::projects::fluid::water_3d
