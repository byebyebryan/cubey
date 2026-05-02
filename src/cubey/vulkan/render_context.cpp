#include <cubey/vulkan/render_context.h>

#include <cubey/vulkan/vk_check.h>

#include <cstddef>
#include <stdexcept>

namespace cubey::vulkan {
namespace {

void validate_config(const RenderContextConfig& config) {
    if (config.device == nullptr) {
        throw std::runtime_error("render context requires a device");
    }
    if (config.swapchain == nullptr) {
        throw std::runtime_error("render context requires a swapchain");
    }
    if (config.frame_resources == nullptr) {
        throw std::runtime_error("render context requires frame resources");
    }
}

} // namespace

RenderContext::RenderContext(RenderContextConfig config) : config_(config) {
    validate_config(config_);
}

RenderFrameResult RenderContext::begin_frame(RenderFrame* frame) const {
    if (frame == nullptr) {
        throw std::runtime_error("begin_frame requires a frame output");
    }
    *frame = {};

    Device& device = *config_.device;
    Swapchain& active_swapchain = *config_.swapchain;
    FrameResources& frame_resources = *config_.frame_resources;
    frame_resources.wait_for_frame();

    std::uint32_t image_index = 0;
    VkResult acquired =
        vkAcquireNextImageKHR(device.handle(), active_swapchain.handle(), UINT64_MAX,
                              frame_resources.image_available(), VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        return RenderFrameResult::RecreateSwapchain;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        check(acquired, "vkAcquireNextImageKHR");
    }

    frame_resources.reset_fence();
    frame_resources.reset_command_buffer();
    *frame = {
        .command_buffer = frame_resources.command_buffer(),
        .image_index = image_index,
        .suboptimal = acquired == VK_SUBOPTIMAL_KHR,
    };
    return RenderFrameResult::Rendered;
}

RenderFrameResult RenderContext::end_frame(const RenderFrame& frame) const {
    if (frame.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("end_frame requires a recorded command buffer");
    }

    Device& device = *config_.device;
    Swapchain& active_swapchain = *config_.swapchain;
    FrameResources& frame_resources = *config_.frame_resources;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.waitSemaphoreCount = 1;
    VkSemaphore image_available = frame_resources.image_available();
    submit.pWaitSemaphores = &image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    VkCommandBuffer command_buffer = frame.command_buffer;
    submit.pCommandBuffers = &command_buffer;
    submit.signalSemaphoreCount = 1;
    VkSemaphore present_ready =
        frame_resources.present_ready(static_cast<std::size_t>(frame.image_index));
    submit.pSignalSemaphores = &present_ready;
    check(vkQueueSubmit(device.queue(), 1, &submit, frame_resources.fence()),
          "vkQueueSubmit frame");

    auto present = vk_struct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &present_ready;
    present.swapchainCount = 1;
    VkSwapchainKHR swapchain_handle = active_swapchain.handle();
    present.pSwapchains = &swapchain_handle;
    present.pImageIndices = &frame.image_index;
    VkResult presented = vkQueuePresentKHR(device.queue(), &present);

    bool recreate_after_present = frame.suboptimal;
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreate_after_present = true;
    } else if (presented != VK_SUCCESS) {
        check(presented, "vkQueuePresentKHR");
    }

    return recreate_after_present ? RenderFrameResult::RecreateSwapchain
                                  : RenderFrameResult::Rendered;
}

} // namespace cubey::vulkan
