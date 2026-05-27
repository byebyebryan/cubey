#pragma once

#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/image_transitions.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace cubey::render {

[[nodiscard]] VkClearValue color_clear_value(float red, float green, float blue, float alpha);
[[nodiscard]] VkClearValue depth_clear_value(float depth = 1.0F, std::uint32_t stencil = 0);
[[nodiscard]] constexpr std::uint32_t fullscreen_triangle_vertex_count() noexcept {
    return 3;
}

void record_fullscreen_triangle(const cubey::vulkan::CommandRecorder& recorder);

struct FullscreenPipelineDrawInfo {
    const GraphicsPipelineResource* pipeline = nullptr;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    std::uint32_t descriptor_set_index = 0;
};

struct ComputePipelineDispatchInfo {
    const ComputePipelineResource* pipeline = nullptr;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    std::uint32_t descriptor_set_index = 0;
    std::uint32_t group_count_x = 1;
    std::uint32_t group_count_y = 1;
    std::uint32_t group_count_z = 1;
};

struct ComputeDispatchGroups {
    std::uint32_t x = 1;
    std::uint32_t y = 1;
    std::uint32_t z = 1;
};

[[nodiscard]] inline std::uint32_t ceil_dispatch_group_count(std::size_t item_count,
                                                             std::uint32_t group_size) {
    if (item_count == 0 || group_size == 0) {
        throw std::runtime_error("compute dispatch group sizing requires positive values");
    }
    const std::size_t group_count = (item_count + group_size - 1U) / group_size;
    if (group_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("compute dispatch group count exceeds Vulkan uint32 range");
    }
    return static_cast<std::uint32_t>(group_count);
}

[[nodiscard]] inline ComputeDispatchGroups linear_dispatch_groups(std::size_t item_count,
                                                                  std::uint32_t group_size) {
    return {
        .x = ceil_dispatch_group_count(item_count, group_size),
        .y = 1U,
        .z = 1U,
    };
}

[[nodiscard]] inline ComputeDispatchGroups
ceil_dispatch_groups(std::uint32_t width, std::uint32_t height, std::uint32_t group_size) {
    return {
        .x = ceil_dispatch_group_count(width, group_size),
        .y = ceil_dispatch_group_count(height, group_size),
        .z = 1U,
    };
}

[[nodiscard]] inline ComputeDispatchGroups ceil_dispatch_groups(std::uint32_t width,
                                                                std::uint32_t height,
                                                                std::uint32_t depth,
                                                                std::uint32_t group_size) {
    return {
        .x = ceil_dispatch_group_count(width, group_size),
        .y = ceil_dispatch_group_count(height, group_size),
        .z = ceil_dispatch_group_count(depth, group_size),
    };
}

[[nodiscard]] inline ComputePipelineDispatchInfo
compute_pipeline_dispatch_info(const ComputePipelineResource& pipeline,
                               VkDescriptorSet descriptor_set, ComputeDispatchGroups groups,
                               std::uint32_t descriptor_set_index = 0) noexcept {
    return {
        .pipeline = &pipeline,
        .descriptor_set = descriptor_set,
        .descriptor_set_index = descriptor_set_index,
        .group_count_x = groups.x,
        .group_count_y = groups.y,
        .group_count_z = groups.z,
    };
}

inline void validate_compute_dispatch_info(const ComputePipelineDispatchInfo& info) {
    if (info.pipeline == nullptr) {
        throw std::runtime_error("compute pipeline dispatch requires a pipeline");
    }
    if (info.group_count_x == 0 || info.group_count_y == 0 || info.group_count_z == 0) {
        throw std::runtime_error("compute pipeline dispatch requires positive group counts");
    }
}

inline void record_fullscreen_pipeline_draw(const cubey::vulkan::CommandRecorder& recorder,
                                            const FullscreenPipelineDrawInfo& info) {
    if (info.pipeline == nullptr) {
        throw std::runtime_error("fullscreen pipeline draw requires a pipeline");
    }
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline->pipeline());
    if (info.descriptor_set != VK_NULL_HANDLE) {
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline->layout(),
                                     info.descriptor_set_index, info.descriptor_set);
    }
    record_fullscreen_triangle(recorder);
}

inline void record_compute_pipeline_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                             const ComputePipelineDispatchInfo& info) {
    validate_compute_dispatch_info(info);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, info.pipeline->pipeline());
    if (info.descriptor_set != VK_NULL_HANDLE) {
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, info.pipeline->layout(),
                                     info.descriptor_set_index, info.descriptor_set);
    }
    recorder.dispatch(info.group_count_x, info.group_count_y, info.group_count_z);
}

template <typename PushConstants>
void record_compute_pipeline_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                      const ComputePipelineDispatchInfo& info,
                                      VkShaderStageFlags stage_flags,
                                      const PushConstants& push_constants) {
    validate_compute_dispatch_info(info);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, info.pipeline->pipeline());
    if (info.descriptor_set != VK_NULL_HANDLE) {
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, info.pipeline->layout(),
                                     info.descriptor_set_index, info.descriptor_set);
    }
    recorder.push_constants(info.pipeline->layout(), stage_flags, 0, push_constants);
    recorder.dispatch(info.group_count_x, info.group_count_y, info.group_count_z);
}

template <typename PushConstants>
void record_profiled_compute_pipeline_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                               const ComputePipelineDispatchInfo& info,
                                               VkShaderStageFlags stage_flags,
                                               const PushConstants& push_constants,
                                               cubey::vulkan::GpuTimestampProfiler* profiler,
                                               std::uint32_t frame_slot_index, const char* label) {
    const std::string_view profile_label = label == nullptr ? std::string_view{} : label;
    cubey::vulkan::GpuTimestampScope profile_scope(profiler, recorder.handle(), frame_slot_index,
                                                   profile_label);
    record_compute_pipeline_dispatch(recorder, info, stage_flags, push_constants);
}

template <typename PushConstants>
void record_fullscreen_pipeline_draw(const cubey::vulkan::CommandRecorder& recorder,
                                     const FullscreenPipelineDrawInfo& info,
                                     VkShaderStageFlags stage_flags,
                                     const PushConstants& push_constants) {
    if (info.pipeline == nullptr) {
        throw std::runtime_error("fullscreen pipeline draw requires a pipeline");
    }
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline->pipeline());
    if (info.descriptor_set != VK_NULL_HANDLE) {
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline->layout(),
                                     info.descriptor_set_index, info.descriptor_set);
    }
    recorder.push_constants(info.pipeline->layout(), stage_flags, 0, push_constants);
    record_fullscreen_triangle(recorder);
}

template <typename RecordCallback>
void record_render_target_pass(const cubey::vulkan::CommandRecorder& recorder,
                               const RenderTargetView& target, const RenderClearValues& clear,
                               RecordCallback&& record_callback) {
    const RenderTargetRenderingInfo rendering(target, clear);
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(target.color.extent);
    std::forward<RecordCallback>(record_callback)(recorder);
    recorder.end_rendering();
}

template <typename RecordCallback>
void record_present_render_target(const cubey::vulkan::CommandRecorder& recorder,
                                  const RenderTargetView& target,
                                  RecordCallback&& record_callback) {
    recorder.transition_image_layout(
        cubey::vulkan::begin_color_attachment_transition(target.color.image));
    if (target.depth.has_value()) {
        recorder.transition_image_layout(
            cubey::vulkan::begin_depth_attachment_transition(target.depth->image));
    }
    std::forward<RecordCallback>(record_callback)(recorder);
    recorder.transition_image_layout(
        cubey::vulkan::finish_color_attachment_for_present_transition(target.color.image));
}

template <typename RecordCallback>
void record_present_render_target_pass(const cubey::vulkan::CommandRecorder& recorder,
                                       const RenderTargetView& target,
                                       const RenderClearValues& clear,
                                       RecordCallback&& record_callback) {
    record_present_render_target(
        recorder, target, [&](const cubey::vulkan::CommandRecorder& present_recorder) {
            record_render_target_pass(present_recorder, target, clear, record_callback);
        });
}

template <typename RecordCallback>
void record_depth_only_pass(const cubey::vulkan::CommandRecorder& recorder,
                            const DepthTargetView& target, VkClearValue clear,
                            RecordCallback&& record_callback) {
    const DepthOnlyRenderingInfo rendering(target, clear);
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(target.extent);
    std::forward<RecordCallback>(record_callback)(recorder);
    recorder.end_rendering();
}

} // namespace cubey::render
