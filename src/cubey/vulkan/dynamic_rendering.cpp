#include <cubey/vulkan/dynamic_rendering.h>

#include <cubey/vulkan/vk_check.h>

namespace cubey::vulkan {

VkRenderingAttachmentInfo color_rendering_attachment(VkImageView image_view, VkClearValue clear) {
    auto attachment =
        vk_struct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    attachment.imageView = image_view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue = clear;
    return attachment;
}

VkRenderingAttachmentInfo depth_rendering_attachment(VkImageView image_view, VkClearValue clear) {
    auto attachment =
        vk_struct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    attachment.imageView = image_view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.clearValue = clear;
    return attachment;
}

} // namespace cubey::vulkan
