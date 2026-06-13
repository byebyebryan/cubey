#include <cubey/render/target.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <array>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_render_target_views_describe_color_only_targets() {
    const VkExtent2D extent{640, 360};
    const VkImage image = reinterpret_cast<VkImage>(0x01);
    const VkImageView view = reinterpret_cast<VkImageView>(0x02);

    const cubey::render::ColorTargetView color =
        cubey::render::color_target_view(extent, VK_FORMAT_R8G8B8A8_UNORM, image, view);
    const cubey::render::RenderTargetView target = cubey::render::render_target_view(color);

    require(target.color.extent.width == extent.width, "color target should preserve width");
    require(target.color.extent.height == extent.height, "color target should preserve height");
    require(target.color.format == VK_FORMAT_R8G8B8A8_UNORM, "color target should preserve format");
    require(target.color.image == image, "color target should preserve image handle");
    require(target.color.view == view, "color target should preserve view handle");
    require(!target.depth.has_value(), "color-only target should not report depth");
}

void test_render_target_rendering_info_describes_dynamic_rendering() {
    const VkExtent2D extent{320, 180};
    const cubey::render::ColorTargetView color = cubey::render::color_target_view(
        extent, VK_FORMAT_B8G8R8A8_UNORM, reinterpret_cast<VkImage>(0x03),
        reinterpret_cast<VkImageView>(0x04));
    const cubey::render::DepthTargetView depth = {
        .extent = extent,
        .format = VK_FORMAT_D32_SFLOAT,
        .image = reinterpret_cast<VkImage>(0x05),
        .view = reinterpret_cast<VkImageView>(0x06),
    };
    const cubey::render::RenderTargetView target = cubey::render::render_target_view(color, depth);

    VkClearValue color_clear{};
    color_clear.color = {{0.1F, 0.2F, 0.3F, 1.0F}};
    VkClearValue depth_clear{};
    depth_clear.depthStencil = {1.0F, 0};
    const cubey::render::RenderClearValues clear_values{
        .color = color_clear,
        .depth = depth_clear,
    };

    const cubey::render::RenderTargetRenderingInfo rendering(target, clear_values);
    const VkRenderingInfo& info = rendering.info();

    require(info.sType == VK_STRUCTURE_TYPE_RENDERING_INFO,
            "rendering info should use VkRenderingInfo");
    require(info.renderArea.extent.width == extent.width, "rendering info should preserve width");
    require(info.renderArea.extent.height == extent.height,
            "rendering info should preserve height");
    require(info.colorAttachmentCount == 1, "rendering info should expose one color attachment");
    require(info.pColorAttachments == &rendering.color_attachment(),
            "rendering info should point at owned color attachment");
    require(info.pDepthAttachment == &rendering.depth_attachment(),
            "rendering info should point at owned depth attachment");
    require(rendering.color_attachment().imageView == color.view,
            "color attachment should preserve color view");
    require(rendering.depth_attachment().imageView == depth.view,
            "depth attachment should preserve depth view");
}

void test_render_target_rendering_info_describes_multiple_color_attachments() {
    const VkExtent2D extent{320, 180};
    const std::array<cubey::render::ColorTargetView, 2> colors{
        cubey::render::color_target_view(extent, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         reinterpret_cast<VkImage>(0x10),
                                         reinterpret_cast<VkImageView>(0x11)),
        cubey::render::color_target_view(extent, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         reinterpret_cast<VkImage>(0x12),
                                         reinterpret_cast<VkImageView>(0x13)),
    };
    const cubey::render::RenderTargetView target = cubey::render::render_target_view(colors);
    VkClearValue color_clear{};
    color_clear.color = {{0.0F, 0.0F, 0.0F, 1.0F}};
    const cubey::render::RenderClearValues clear_values{
        .color = color_clear,
    };

    const cubey::render::RenderTargetRenderingInfo rendering(target, clear_values);
    const VkRenderingInfo& info = rendering.info();

    require(target.color.view == colors[0].view, "multi target should preserve primary color");
    require(target.colors.size() == colors.size(), "multi target should preserve color count");
    require(info.colorAttachmentCount == colors.size(),
            "rendering info should expose multiple color attachments");
    require(info.pColorAttachments == rendering.color_attachments().data(),
            "rendering info should point at owned color attachments");
    require(rendering.color_attachments()[0].imageView == colors[0].view,
            "first color attachment should preserve color view");
    require(rendering.color_attachments()[1].imageView == colors[1].view,
            "second color attachment should preserve color view");
}

void test_depth_only_rendering_info_describes_sampled_depth_target() {
    const VkExtent2D extent{1024, 1024};
    const VkImage image = reinterpret_cast<VkImage>(0x07);
    const VkImageView view = reinterpret_cast<VkImageView>(0x08);
    const cubey::render::DepthTargetView depth =
        cubey::render::depth_target_view(extent, VK_FORMAT_D32_SFLOAT, image, view);

    require(depth.extent.width == extent.width, "depth target should preserve width");
    require(depth.extent.height == extent.height, "depth target should preserve height");
    require(depth.format == VK_FORMAT_D32_SFLOAT, "depth target should preserve format");
    require(depth.image == image, "depth target should preserve image handle");
    require(depth.view == view, "depth target should preserve view handle");

    VkClearValue depth_clear{};
    depth_clear.depthStencil = {1.0F, 0};
    const cubey::render::DepthOnlyRenderingInfo rendering(depth, depth_clear);
    const VkRenderingInfo& info = rendering.info();

    require(info.sType == VK_STRUCTURE_TYPE_RENDERING_INFO,
            "depth-only rendering should use VkRenderingInfo");
    require(info.renderArea.extent.width == extent.width,
            "depth-only rendering should preserve width");
    require(info.renderArea.extent.height == extent.height,
            "depth-only rendering should preserve height");
    require(info.colorAttachmentCount == 0, "depth-only rendering should not expose color");
    require(info.pColorAttachments == nullptr, "depth-only rendering should not point at color");
    require(info.pDepthAttachment == &rendering.depth_attachment(),
            "depth-only rendering should point at owned depth attachment");
    require(rendering.depth_attachment().imageView == depth.view,
            "depth-only attachment should preserve depth view");
    require(rendering.depth_attachment().storeOp == VK_ATTACHMENT_STORE_OP_STORE,
            "sampled depth target should store rendered depth");
}
