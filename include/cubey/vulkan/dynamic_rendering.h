#pragma once

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

struct RenderingAttachmentOps {
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
};

[[nodiscard]] constexpr RenderingAttachmentOps clear_store_attachment_ops() noexcept {
    return {
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
    };
}

[[nodiscard]] constexpr RenderingAttachmentOps load_store_attachment_ops() noexcept {
    return {
        .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
    };
}

[[nodiscard]] constexpr RenderingAttachmentOps clear_discard_attachment_ops() noexcept {
    return {
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    };
}

[[nodiscard]] VkRenderingAttachmentInfo color_rendering_attachment(VkImageView image_view,
                                                                   VkClearValue clear,
                                                                   RenderingAttachmentOps ops =
                                                                       clear_store_attachment_ops());
[[nodiscard]] VkRenderingAttachmentInfo depth_rendering_attachment(VkImageView image_view,
                                                                   VkClearValue clear,
                                                                   RenderingAttachmentOps ops =
                                                                       clear_discard_attachment_ops());

} // namespace cubey::vulkan
