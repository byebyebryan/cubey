#include <cubey/vulkan/rendering.h>

#include <vulkan/vulkan.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_rendering_helpers_describe_dynamic_rendering_setup() {
    const VkImage image = reinterpret_cast<VkImage>(0x01);
    const cubey::vulkan::ImageLayoutTransition begin_color =
        cubey::vulkan::begin_color_attachment_transition(image);
    require(begin_color.image == image, "color transition should preserve image handle");
    require(begin_color.aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT,
            "color transition should target color aspect");
    require(begin_color.old_layout == VK_IMAGE_LAYOUT_UNDEFINED,
            "begin color transition should discard previous contents");
    require(begin_color.new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            "begin color transition should target color attachment layout");
    require(begin_color.dst_access_mask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            "begin color transition should allow color writes");
    require(begin_color.src_stage_mask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            "begin color transition should start at top of pipe");
    require(begin_color.dst_stage_mask == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            "begin color transition should end at color output");

    const cubey::vulkan::ImageLayoutTransition present =
        cubey::vulkan::finish_color_attachment_for_present_transition(image);
    require(present.old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            "present transition should start from color attachment layout");
    require(present.new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            "present transition should target present layout");
    require(present.src_access_mask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            "present transition should wait on color writes");
    require(present.dst_stage_mask == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            "present transition should end at bottom of pipe");

    const cubey::vulkan::ImageLayoutTransition begin_depth =
        cubey::vulkan::begin_depth_attachment_transition(image);
    require(begin_depth.aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT,
            "depth transition should target depth aspect");
    require(begin_depth.new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            "depth transition should target depth attachment layout");
    require((begin_depth.dst_access_mask & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) != 0,
            "depth transition should allow depth writes");
    require(begin_depth.dst_stage_mask == VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            "depth transition should end at early fragment tests");

    const cubey::vulkan::ImageLayoutTransition storage_write =
        cubey::vulkan::begin_storage_image_write_transition(image);
    require(storage_write.old_layout == VK_IMAGE_LAYOUT_UNDEFINED,
            "storage write transition should discard previous contents");
    require(storage_write.new_layout == VK_IMAGE_LAYOUT_GENERAL,
            "storage write transition should target general layout");
    require(storage_write.dst_access_mask == VK_ACCESS_SHADER_WRITE_BIT,
            "storage write transition should allow shader writes");
    require(storage_write.dst_stage_mask == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            "storage write transition should target compute shader stage");

    const cubey::vulkan::ImageLayoutTransition sampled =
        cubey::vulkan::finish_storage_image_write_for_sampling_transition(image);
    require(sampled.old_layout == VK_IMAGE_LAYOUT_GENERAL,
            "sample transition should start from storage-image layout");
    require(sampled.new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            "sample transition should target shader-read layout");
    require(sampled.src_access_mask == VK_ACCESS_SHADER_WRITE_BIT,
            "sample transition should wait on shader writes");
    require(sampled.dst_access_mask == VK_ACCESS_SHADER_READ_BIT,
            "sample transition should allow shader reads");
    require(sampled.src_stage_mask == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            "sample transition should start at compute shader stage");
    require(sampled.dst_stage_mask == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            "sample transition should target fragment shader stage");

    const cubey::vulkan::ImageLayoutTransition transfer_dst =
        cubey::vulkan::begin_transfer_dst_transition(image);
    require(transfer_dst.new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            "transfer dst transition should target transfer dst layout");
    require(transfer_dst.dst_access_mask == VK_ACCESS_TRANSFER_WRITE_BIT,
            "transfer dst transition should allow transfer writes");
    require(transfer_dst.dst_stage_mask == VK_PIPELINE_STAGE_TRANSFER_BIT,
            "transfer dst transition should target transfer stage");

    const cubey::vulkan::ImageLayoutTransition transfer_sampled =
        cubey::vulkan::finish_transfer_dst_for_sampling_transition(image);
    require(transfer_sampled.old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            "transfer sampled transition should start from transfer dst layout");
    require(transfer_sampled.new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            "transfer sampled transition should target shader-read layout");
    require(transfer_sampled.src_access_mask == VK_ACCESS_TRANSFER_WRITE_BIT,
            "transfer sampled transition should wait on transfer writes");

    const cubey::vulkan::ImageLayoutTransition transfer_src =
        cubey::vulkan::begin_transfer_src_transition(image);
    require(transfer_src.new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            "transfer src transition should target transfer src layout");
    require(transfer_src.dst_access_mask == VK_ACCESS_TRANSFER_READ_BIT,
            "transfer src transition should allow transfer reads");

    const VkImageMemoryBarrier barrier = cubey::vulkan::image_memory_barrier(begin_depth);
    require(barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            "barrier should use the image memory barrier structure type");
    require(barrier.image == image, "barrier should preserve image handle");
    require(barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT,
            "barrier should preserve aspect mask");
    require(barrier.subresourceRange.levelCount == 1, "barrier should target one mip level");
    require(barrier.subresourceRange.layerCount == 1, "barrier should target one array layer");

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
