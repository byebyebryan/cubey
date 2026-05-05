#include <cubey/vulkan/dynamic_rendering.h>

#include <vulkan/vulkan.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_dynamic_rendering_describes_attachment_setup() {
    VkClearValue clear{};
    clear.color = {{0.1F, 0.2F, 0.3F, 1.0F}};
    const VkImageView view = reinterpret_cast<VkImageView>(0x02);
    const VkRenderingAttachmentInfo color_attachment =
        cubey::vulkan::color_rendering_attachment(view, clear);
    require(color_attachment.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            "color attachment should use rendering attachment info");
    require(color_attachment.imageView == view, "color attachment should preserve image view");
    require(color_attachment.imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            "color attachment should use color attachment layout");
    require(color_attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR,
            "color attachment should clear on load");
    require(color_attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE,
            "color attachment should store for presentation");

    VkClearValue depth_clear{};
    depth_clear.depthStencil = {1.0F, 0};
    const VkRenderingAttachmentInfo depth_attachment =
        cubey::vulkan::depth_rendering_attachment(view, depth_clear);
    require(depth_attachment.imageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            "depth attachment should use depth attachment layout");
    require(depth_attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR,
            "depth attachment should clear on load");
    require(depth_attachment.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE,
            "depth attachment does not need to store");
}
