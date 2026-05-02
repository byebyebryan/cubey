#include <cubey/vulkan/visible_frame.h>

#include <cubey/vulkan/vk_check.h>

#include <cstddef>
#include <stdexcept>

namespace cubey::vulkan {
namespace {

void validate_config(const VisibleFrameConfig& config) {
    if (config.device == nullptr) {
        throw std::runtime_error("visible frame requires a device");
    }
    if (config.swapchain == nullptr) {
        throw std::runtime_error("visible frame requires a swapchain");
    }
    if (config.frame_resources == nullptr) {
        throw std::runtime_error("visible frame requires frame resources");
    }
    if (config.recorder == nullptr) {
        throw std::runtime_error("visible frame requires a recorder callback");
    }
}

} // namespace

VisibleFrameResult draw_visible_frame(const VisibleFrameConfig& config) {
    validate_config(config);

    Device& device = *config.device;
    Swapchain& active_swapchain = *config.swapchain;
    FrameResources& frame = *config.frame_resources;
    frame.wait_for_frame();

    std::uint32_t image_index = 0;
    VkResult acquired =
        vkAcquireNextImageKHR(device.handle(), active_swapchain.handle(), UINT64_MAX,
                              frame.image_available(), VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        return VisibleFrameResult::RecreateSwapchain;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        check(acquired, "vkAcquireNextImageKHR");
    }
    bool recreate_after_present = acquired == VK_SUBOPTIMAL_KHR;

    frame.reset_fence();
    frame.reset_command_buffer();
    config.recorder(config.user_data, {
                                          .command_buffer = frame.command_buffer(),
                                          .image_index = image_index,
                                      });

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.waitSemaphoreCount = 1;
    VkSemaphore image_available = frame.image_available();
    submit.pWaitSemaphores = &image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    VkCommandBuffer command_buffer = frame.command_buffer();
    submit.pCommandBuffers = &command_buffer;
    submit.signalSemaphoreCount = 1;
    VkSemaphore present_ready = frame.present_ready(static_cast<std::size_t>(image_index));
    submit.pSignalSemaphores = &present_ready;
    check(vkQueueSubmit(device.queue(), 1, &submit, frame.fence()), "vkQueueSubmit visible frame");

    auto present = vk_struct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &present_ready;
    present.swapchainCount = 1;
    VkSwapchainKHR swapchain_handle = active_swapchain.handle();
    present.pSwapchains = &swapchain_handle;
    present.pImageIndices = &image_index;
    VkResult presented = vkQueuePresentKHR(device.queue(), &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreate_after_present = true;
    } else if (presented != VK_SUCCESS) {
        check(presented, "vkQueuePresentKHR");
    }

    return recreate_after_present ? VisibleFrameResult::RecreateSwapchain
                                  : VisibleFrameResult::Rendered;
}

} // namespace cubey::vulkan
