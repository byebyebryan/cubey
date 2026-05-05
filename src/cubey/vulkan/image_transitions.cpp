#include <cubey/vulkan/image_transitions.h>

#include <cubey/vulkan/vk_check.h>

namespace cubey::vulkan {

ImageLayoutTransition begin_color_attachment_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .src_access_mask = 0,
        .dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
}

ImageLayoutTransition finish_color_attachment_for_present_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dst_access_mask = 0,
        .src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };
}

ImageLayoutTransition finish_color_attachment_for_readback_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dst_access_mask = VK_ACCESS_TRANSFER_READ_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
}

ImageLayoutTransition begin_depth_attachment_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .src_access_mask = 0,
        .dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    };
}

ImageLayoutTransition begin_storage_image_write_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = VK_IMAGE_LAYOUT_GENERAL,
        .src_access_mask = 0,
        .dst_access_mask = VK_ACCESS_SHADER_WRITE_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    };
}

ImageLayoutTransition finish_storage_image_write_for_sampling_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_GENERAL,
        .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .src_access_mask = VK_ACCESS_SHADER_WRITE_BIT,
        .dst_access_mask = VK_ACCESS_SHADER_READ_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
}

ImageLayoutTransition begin_transfer_dst_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .src_access_mask = 0,
        .dst_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
}

ImageLayoutTransition finish_transfer_dst_for_sampling_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dst_access_mask = VK_ACCESS_SHADER_READ_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
}

ImageLayoutTransition begin_sampled_image_readback_transition(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .src_access_mask = VK_ACCESS_SHADER_READ_BIT,
        .dst_access_mask = VK_ACCESS_TRANSFER_READ_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
}

VkImageMemoryBarrier image_memory_barrier(const ImageLayoutTransition& transition) {
    auto barrier = vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.oldLayout = transition.old_layout;
    barrier.newLayout = transition.new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = transition.image;
    barrier.srcAccessMask = transition.src_access_mask;
    barrier.dstAccessMask = transition.dst_access_mask;
    barrier.subresourceRange.aspectMask = transition.aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    return barrier;
}

void transition_image_layout(VkCommandBuffer command_buffer,
                             const ImageLayoutTransition& transition) {
    const VkImageMemoryBarrier barrier = image_memory_barrier(transition);
    vkCmdPipelineBarrier(command_buffer, transition.src_stage_mask, transition.dst_stage_mask, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
}

} // namespace cubey::vulkan
