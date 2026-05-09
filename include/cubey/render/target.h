#pragma once

#include <cubey/vulkan/image.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>

namespace cubey::render {

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

[[nodiscard]] ColorTargetView color_target_view(VkExtent2D extent, VkFormat format, VkImage image,
                                                VkImageView view);
[[nodiscard]] ColorTargetView swapchain_color_target_view(const cubey::vulkan::Swapchain& swapchain,
                                                          std::uint32_t image_index);
[[nodiscard]] DepthTargetView depth_target_view(const cubey::vulkan::DepthAttachment& attachment);
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

} // namespace cubey::render
