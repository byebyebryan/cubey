#include <cubey/render/target.h>

#include <cubey/render/texture.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/vk_check.h>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace cubey::render {
namespace {

void validate_color_target(const ColorTargetView& target) {
    if (target.extent.width == 0 || target.extent.height == 0) {
        throw std::runtime_error("color target extent must be nonzero");
    }
    if (target.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("color target format must be defined");
    }
    if (target.image == VK_NULL_HANDLE || target.view == VK_NULL_HANDLE) {
        throw std::runtime_error("color target requires valid image and view handles");
    }
}

void validate_depth_target(const DepthTargetView& target) {
    if (target.extent.width == 0 || target.extent.height == 0) {
        throw std::runtime_error("depth target extent must be nonzero");
    }
    if (target.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("depth target format must be defined");
    }
    if (target.image == VK_NULL_HANDLE || target.view == VK_NULL_HANDLE) {
        throw std::runtime_error("depth target requires valid image and view handles");
    }
}

void validate_color_targets(std::span<const ColorTargetView> colors) {
    if (colors.empty()) {
        throw std::runtime_error("render target requires at least one color attachment");
    }

    const VkExtent2D extent = colors.front().extent;
    for (const ColorTargetView& color : colors) {
        validate_color_target(color);
        if (color.extent.width != extent.width || color.extent.height != extent.height) {
            throw std::runtime_error("render target color attachment extents must match");
        }
    }
}

std::vector<ColorTargetView> copy_color_targets(std::span<const ColorTargetView> colors) {
    validate_color_targets(colors);
    return {colors.begin(), colors.end()};
}

} // namespace

ColorTargetView color_target_view(VkExtent2D extent, VkFormat format, VkImage image,
                                  VkImageView view) {
    ColorTargetView target{
        .extent = extent,
        .format = format,
        .image = image,
        .view = view,
    };
    validate_color_target(target);
    return target;
}

ColorTargetView swapchain_color_target_view(const cubey::vulkan::Swapchain& swapchain,
                                            std::uint32_t image_index) {
    const std::size_t index = static_cast<std::size_t>(image_index);
    return color_target_view(swapchain.extent(), swapchain.format(), swapchain.images().at(index),
                             swapchain.image_views().at(index));
}

DepthTargetView depth_target_view(const cubey::vulkan::DepthAttachment& attachment) {
    const VkExtent3D extent = attachment.extent();
    return depth_target_view({extent.width, extent.height}, attachment.format(),
                             attachment.handle(), attachment.view());
}

DepthTargetView depth_target_view(VkExtent2D extent, VkFormat format, VkImage image,
                                  VkImageView view) {
    DepthTargetView target{
        .extent = extent,
        .format = format,
        .image = image,
        .view = view,
    };
    validate_depth_target(target);
    return target;
}

DepthTargetView depth_target_view(const DepthTexture& texture) {
    return depth_target_view(texture.extent(), texture.format(), texture.handle(), texture.view());
}

RenderTargetView render_target_view(ColorTargetView color) {
    validate_color_target(color);
    return {
        .color = color,
        .colors = {color},
        .depth = std::nullopt,
    };
}

RenderTargetView render_target_view(ColorTargetView color, DepthTargetView depth) {
    validate_color_target(color);
    validate_depth_target(depth);
    if (color.extent.width != depth.extent.width || color.extent.height != depth.extent.height) {
        throw std::runtime_error("render target color and depth extents must match");
    }
    return {
        .color = color,
        .colors = {color},
        .depth = depth,
    };
}

RenderTargetView render_target_view(std::span<const ColorTargetView> colors) {
    std::vector<ColorTargetView> copied_colors = copy_color_targets(colors);
    const ColorTargetView first_color = copied_colors.front();
    return {
        .color = first_color,
        .colors = std::move(copied_colors),
        .depth = std::nullopt,
    };
}

RenderTargetView render_target_view(std::span<const ColorTargetView> colors,
                                    DepthTargetView depth) {
    std::vector<ColorTargetView> copied_colors = copy_color_targets(colors);
    validate_depth_target(depth);
    const ColorTargetView first_color = copied_colors.front();
    if (first_color.extent.width != depth.extent.width ||
        first_color.extent.height != depth.extent.height) {
        throw std::runtime_error("render target color and depth extents must match");
    }
    return {
        .color = first_color,
        .colors = std::move(copied_colors),
        .depth = depth,
    };
}

RenderTargetRenderingInfo::RenderTargetRenderingInfo(const RenderTargetView& target,
                                                     const RenderClearValues& clear,
                                                     RenderTargetAttachmentOps ops) {
    const std::span<const ColorTargetView> colors =
        target.colors.empty() ? std::span<const ColorTargetView>(&target.color, 1) : target.colors;
    validate_color_targets(colors);
    color_attachments_.reserve(colors.size());
    for (const ColorTargetView& color : colors) {
        color_attachments_.push_back(
            cubey::vulkan::color_rendering_attachment(color.view, clear.color, ops.color));
    }
    if (target.depth.has_value()) {
        validate_depth_target(target.depth.value());
        if (colors.front().extent.width != target.depth->extent.width ||
            colors.front().extent.height != target.depth->extent.height) {
            throw std::runtime_error("render target color and depth extents must match");
        }
        depth_attachment_ =
            cubey::vulkan::depth_rendering_attachment(target.depth->view, clear.depth, ops.depth);
    }

    info_ = cubey::vulkan::vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    info_.renderArea.offset = {0, 0};
    info_.renderArea.extent = colors.front().extent;
    info_.layerCount = 1;
    info_.colorAttachmentCount = static_cast<std::uint32_t>(color_attachments_.size());
    info_.pColorAttachments = color_attachments_.data();
    if (depth_attachment_.has_value()) {
        info_.pDepthAttachment = &depth_attachment_.value();
    }
}

const VkRenderingAttachmentInfo& RenderTargetRenderingInfo::depth_attachment() const {
    if (!depth_attachment_.has_value()) {
        throw std::runtime_error("render target has no depth attachment");
    }
    return depth_attachment_.value();
}

DepthOnlyRenderingInfo::DepthOnlyRenderingInfo(const DepthTargetView& target, VkClearValue clear,
                                               cubey::vulkan::RenderingAttachmentOps ops) {
    validate_depth_target(target);

    depth_attachment_ = cubey::vulkan::depth_rendering_attachment(target.view, clear, ops);

    info_ = cubey::vulkan::vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    info_.renderArea.offset = {0, 0};
    info_.renderArea.extent = target.extent;
    info_.layerCount = 1;
    info_.colorAttachmentCount = 0;
    info_.pColorAttachments = nullptr;
    info_.pDepthAttachment = &depth_attachment_;
}

} // namespace cubey::render
