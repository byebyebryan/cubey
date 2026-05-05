#pragma once

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

[[nodiscard]] VkRenderingAttachmentInfo color_rendering_attachment(VkImageView image_view,
                                                                   VkClearValue clear);
[[nodiscard]] VkRenderingAttachmentInfo depth_rendering_attachment(VkImageView image_view,
                                                                   VkClearValue clear);

} // namespace cubey::vulkan
