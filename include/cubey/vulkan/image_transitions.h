#pragma once

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

struct ImageLayoutTransition {
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect_mask = 0;
    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags src_access_mask = 0;
    VkAccessFlags dst_access_mask = 0;
    VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
};

[[nodiscard]] ImageLayoutTransition begin_color_attachment_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition finish_color_attachment_for_present_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition finish_color_attachment_for_readback_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition begin_depth_attachment_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition begin_storage_image_write_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition
finish_storage_image_write_for_sampling_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition begin_transfer_dst_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition finish_transfer_dst_for_sampling_transition(VkImage image);
[[nodiscard]] ImageLayoutTransition begin_sampled_image_readback_transition(VkImage image);
[[nodiscard]] VkImageMemoryBarrier image_memory_barrier(const ImageLayoutTransition& transition);
void transition_image_layout(VkCommandBuffer command_buffer,
                             const ImageLayoutTransition& transition);

} // namespace cubey::vulkan
