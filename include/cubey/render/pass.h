#pragma once

#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
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
    std::forward<RecordCallback>(record_callback)(recorder);
    recorder.end_rendering();
}

} // namespace cubey::render
