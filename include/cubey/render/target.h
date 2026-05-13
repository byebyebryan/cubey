#pragma once

#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace cubey::render {

class DepthTexture;

struct ColorTargetView {
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct DepthTargetView {
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct RenderTargetView {
    ColorTargetView color;
    std::optional<DepthTargetView> depth;
};

struct RenderClearValues {
    VkClearValue color{};
    VkClearValue depth{};
};

[[nodiscard]] VkClearValue color_clear_value(float red, float green, float blue, float alpha);
[[nodiscard]] VkClearValue depth_clear_value(float depth = 1.0F, std::uint32_t stencil = 0);
[[nodiscard]] constexpr std::uint32_t fullscreen_triangle_vertex_count() noexcept {
    return 3;
}

[[nodiscard]] ColorTargetView color_target_view(VkExtent2D extent, VkFormat format, VkImage image,
                                                VkImageView view);
[[nodiscard]] ColorTargetView swapchain_color_target_view(const cubey::vulkan::Swapchain& swapchain,
                                                          std::uint32_t image_index);
[[nodiscard]] DepthTargetView depth_target_view(VkExtent2D extent, VkFormat format, VkImage image,
                                                VkImageView view);
[[nodiscard]] DepthTargetView depth_target_view(const cubey::vulkan::DepthAttachment& attachment);
[[nodiscard]] DepthTargetView depth_target_view(const DepthTexture& texture);
[[nodiscard]] RenderTargetView render_target_view(ColorTargetView color);
[[nodiscard]] RenderTargetView render_target_view(ColorTargetView color, DepthTargetView depth);

class RenderTargetRenderingInfo {
  public:
    RenderTargetRenderingInfo(const RenderTargetView& target, const RenderClearValues& clear);

    RenderTargetRenderingInfo(const RenderTargetRenderingInfo&) = delete;
    RenderTargetRenderingInfo& operator=(const RenderTargetRenderingInfo&) = delete;
    RenderTargetRenderingInfo(RenderTargetRenderingInfo&&) = delete;
    RenderTargetRenderingInfo& operator=(RenderTargetRenderingInfo&&) = delete;

    [[nodiscard]] const VkRenderingInfo& info() const {
        return info_;
    }
    [[nodiscard]] const VkRenderingAttachmentInfo& color_attachment() const {
        return color_attachment_;
    }
    [[nodiscard]] const VkRenderingAttachmentInfo& depth_attachment() const;

  private:
    VkRenderingAttachmentInfo color_attachment_{};
    std::optional<VkRenderingAttachmentInfo> depth_attachment_;
    VkRenderingInfo info_{};
};

class DepthOnlyRenderingInfo {
  public:
    DepthOnlyRenderingInfo(const DepthTargetView& target, VkClearValue clear);

    DepthOnlyRenderingInfo(const DepthOnlyRenderingInfo&) = delete;
    DepthOnlyRenderingInfo& operator=(const DepthOnlyRenderingInfo&) = delete;
    DepthOnlyRenderingInfo(DepthOnlyRenderingInfo&&) = delete;
    DepthOnlyRenderingInfo& operator=(DepthOnlyRenderingInfo&&) = delete;

    [[nodiscard]] const VkRenderingInfo& info() const {
        return info_;
    }
    [[nodiscard]] const VkRenderingAttachmentInfo& depth_attachment() const {
        return depth_attachment_;
    }

  private:
    VkRenderingAttachmentInfo depth_attachment_{};
    VkRenderingInfo info_{};
};

void record_fullscreen_triangle(const cubey::vulkan::CommandRecorder& recorder);

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
