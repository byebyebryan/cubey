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
    if (config.gpu == nullptr) {
        throw std::runtime_error("render context requires a GPU runtime");
    }
}

} // namespace

RenderContext::RenderContext(RenderContextConfig config) : config_(config) {
    validate_config(config_);
}

SwapchainRecreateTracker::SwapchainRecreateTracker(std::uint32_t max_consecutive_recreates)
    : max_consecutive_recreates_(max_consecutive_recreates) {
    if (max_consecutive_recreates_ == 0) {
        throw std::runtime_error("swapchain recreate tracker limit must be positive");
    }
}

void SwapchainRecreateTracker::record_recreate_request() {
    ++consecutive_recreates_;
    if (consecutive_recreates_ > max_consecutive_recreates_) {
        throw std::runtime_error("swapchain stayed out of date after repeated recreation attempts");
    }
}

void SwapchainRecreateTracker::reset() {
    consecutive_recreates_ = 0;
}

RenderFrameResult RenderContext::begin_frame(RenderFrame* frame) const {
    if (frame == nullptr) {
        throw std::runtime_error("begin_frame requires a frame output");
    }
    *frame = {};

    Device& device = *config_.device;
    Swapchain& active_swapchain = *config_.swapchain;
    FrameResources& frame_resources = *config_.frame_resources;
    const std::uint32_t frame_slot_index = frame_resources.current_frame_slot_index();
    const FrameResourceSlot& frame_slot = frame_resources.slot(frame_slot_index);
    frame_resources.wait_for_frame(frame_slot_index);
    config_.gpu->mark_submission_completed(frame_resources.submitted_ticket(frame_slot_index));

    std::uint32_t image_index = 0;
    VkResult acquired =
        vkAcquireNextImageKHR(device.handle(), active_swapchain.handle(), UINT64_MAX,
                              frame_slot.image_available, VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        return RenderFrameResult::RecreateSwapchain;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        check(acquired, "vkAcquireNextImageKHR");
    }

    const std::size_t image_slot = static_cast<std::size_t>(image_index);
    const VkFence image_fence = frame_resources.image_in_flight(image_slot);
    if (image_fence != VK_NULL_HANDLE) {
        check(vkWaitForFences(device.handle(), 1, &image_fence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences swapchain image");
    }

    frame_resources.reset_fence(frame_slot_index);
    frame_resources.reset_command_buffer(frame_slot_index);
    frame_resources.mark_image_in_flight(image_slot, frame_slot.fence);
    *frame = {
        .command_buffer = frame_slot.command_buffer,
        .image_index = image_index,
        .frame_slot_index = frame_slot_index,
        .frame_slot_count = frame_resources.frame_slot_count(),
        .suboptimal = acquired == VK_SUBOPTIMAL_KHR,
    };
    return RenderFrameResult::Rendered;
}

RenderFrameResult RenderContext::end_frame(const RenderFrame& frame) const {
    if (frame.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("end_frame requires a recorded command buffer");
    }

    Swapchain& active_swapchain = *config_.swapchain;
    FrameResources& frame_resources = *config_.frame_resources;
    const FrameResourceSlot& frame_slot = frame_resources.slot(frame.frame_slot_index);

    VkSemaphore present_ready =
        frame_resources.present_ready(static_cast<std::size_t>(frame.image_index));
    FrameTicket submitted{};
    VkResult presented = VK_SUCCESS;
    static_cast<void>(config_.gpu->submit_and_wait({
        .label = "submit and present frame",
        .work =
            [&](GpuOwnerContext& gpu_context) {
                submitted = gpu_context.submission().submit(
                    {
                        .waits =
                            {
                                QueueWait{
                                    .semaphore = frame_slot.image_available,
                                    .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                },
                            },
                        .command_buffers = {frame.command_buffer},
                        .signals = {QueueSignal{.semaphore = present_ready}},
                        .fence = frame_slot.fence,
                    },
                    "vkQueueSubmit frame");

                auto present = vk_struct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
                present.waitSemaphoreCount = 1;
                present.pWaitSemaphores = &present_ready;
                present.swapchainCount = 1;
                VkSwapchainKHR swapchain_handle = active_swapchain.handle();
                present.pSwapchains = &swapchain_handle;
                present.pImageIndices = &frame.image_index;
                presented = vkQueuePresentKHR(gpu_context.device().queue(), &present);
            },
    }));
    frame_resources.mark_submitted(frame.frame_slot_index, submitted);

    bool recreate_after_present = frame.suboptimal;
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreate_after_present = true;
    } else if (presented != VK_SUCCESS) {
        check(presented, "vkQueuePresentKHR");
    }

    frame_resources.advance_frame_slot();
    return recreate_after_present ? RenderFrameResult::RecreateSwapchain
                                  : RenderFrameResult::Rendered;
}

} // namespace cubey::vulkan
