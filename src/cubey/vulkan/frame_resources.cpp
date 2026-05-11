#include <cubey/vulkan/frame_resources.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

FrameResources::FrameResources(const Device& device, std::size_t present_ready_count)
    : FrameResources(device, FrameResourcesConfig{
                                 .present_ready_count = present_ready_count,
                                 .frame_slot_count = 1,
                             }) {}

FrameResources::FrameResources(const Device& device, const FrameResourcesConfig& config)
    : device_(device.handle()),
      command_pool_(device, {.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT}),
      frame_slots_(config.frame_slot_count),
      present_ready_(config.present_ready_count, VK_NULL_HANDLE),
      images_in_flight_(config.present_ready_count, VK_NULL_HANDLE) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("frame resources require a valid Vulkan device");
    }
    if (frame_slots_.empty()) {
        throw std::runtime_error("frame resources require at least one frame slot");
    }
    if (present_ready_.empty()) {
        throw std::runtime_error("frame resources require at least one present-ready semaphore");
    }

    try {
        create();
    } catch (...) {
        destroy();
        throw;
    }
}

FrameResources::~FrameResources() {
    destroy();
}

void FrameResources::advance_frame_slot() {
    current_frame_slot_index_ = (current_frame_slot_index_ + 1U) % frame_slot_count();
}

VkFence FrameResources::image_in_flight(std::size_t image_index) const {
    return images_in_flight_.at(image_index);
}

void FrameResources::mark_image_in_flight(std::size_t image_index, VkFence fence) {
    images_in_flight_.at(image_index) = fence;
}

FrameTicket FrameResources::submitted_ticket(std::uint32_t frame_slot_index) const {
    return slot(frame_slot_index).submitted_ticket;
}

void FrameResources::mark_submitted(std::uint32_t frame_slot_index, FrameTicket ticket) {
    frame_slots_.at(frame_slot_index).submitted_ticket = ticket;
}

void FrameResources::wait_for_frame(std::uint32_t frame_slot_index) const {
    const VkFence frame_fence = slot(frame_slot_index).fence;
    check(vkWaitForFences(device_, 1, &frame_fence, VK_TRUE, UINT64_MAX), "vkWaitForFences frame");
}

void FrameResources::reset_fence(std::uint32_t frame_slot_index) const {
    const VkFence frame_fence = slot(frame_slot_index).fence;
    check(vkResetFences(device_, 1, &frame_fence), "vkResetFences frame");
}

void FrameResources::reset_command_buffer(std::uint32_t frame_slot_index) const {
    check(vkResetCommandBuffer(slot(frame_slot_index).command_buffer, 0),
          "vkResetCommandBuffer frame");
}

void FrameResources::create() {
    for (FrameResourceSlot& frame_slot : frame_slots_) {
        frame_slot.command_buffer = command_pool_.allocate_primary();
    }

    auto semaphore_info = vk_struct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    for (FrameResourceSlot& frame_slot : frame_slots_) {
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &frame_slot.image_available),
              "vkCreateSemaphore image_available");
    }
    for (VkSemaphore& semaphore : present_ready_) {
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &semaphore),
              "vkCreateSemaphore present_ready");
    }

    auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (FrameResourceSlot& frame_slot : frame_slots_) {
        check(vkCreateFence(device_, &fence_info, nullptr, &frame_slot.fence),
              "vkCreateFence frame");
    }
}

void FrameResources::destroy() {
    for (FrameResourceSlot& frame_slot : frame_slots_) {
        if (frame_slot.fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frame_slot.fence, nullptr);
            frame_slot.fence = VK_NULL_HANDLE;
        }
    }
    for (VkSemaphore& semaphore : present_ready_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    present_ready_.clear();
    for (FrameResourceSlot& frame_slot : frame_slots_) {
        if (frame_slot.image_available != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, frame_slot.image_available, nullptr);
            frame_slot.image_available = VK_NULL_HANDLE;
        }
        frame_slot.command_buffer = VK_NULL_HANDLE;
    }
    frame_slots_.clear();
    images_in_flight_.clear();
    current_frame_slot_index_ = 0;
}

} // namespace cubey::vulkan
