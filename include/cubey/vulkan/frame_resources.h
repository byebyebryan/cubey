#pragma once

#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/submission_tickets.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::vulkan {

struct FrameResourcesConfig {
    std::size_t present_ready_count = 0;
    std::uint32_t frame_slot_count = 1;
};

struct FrameResourceSlot {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    GpuSubmissionTicket submitted_ticket;
};

class FrameResources {
  public:
    explicit FrameResources(const Device& device, std::size_t present_ready_count);
    FrameResources(const Device& device, const FrameResourcesConfig& config);
    ~FrameResources();

    FrameResources(const FrameResources&) = delete;
    FrameResources& operator=(const FrameResources&) = delete;

    VkCommandPool command_pool() const {
        return command_pool_.handle();
    }
    const FrameResourceSlot& slot(std::uint32_t frame_slot_index) const {
        return frame_slots_.at(frame_slot_index);
    }
    std::uint32_t frame_slot_count() const {
        return static_cast<std::uint32_t>(frame_slots_.size());
    }
    std::uint32_t current_frame_slot_index() const {
        return current_frame_slot_index_;
    }
    void advance_frame_slot();
    VkSemaphore present_ready(std::size_t image_index) const {
        return present_ready_.at(image_index);
    }
    std::size_t present_ready_count() const {
        return present_ready_.size();
    }
    VkFence fence() const {
        return slot(0).fence;
    }
    VkFence image_in_flight(std::size_t image_index) const;
    void mark_image_in_flight(std::size_t image_index, VkFence fence);
    GpuSubmissionTicket submitted_ticket(std::uint32_t frame_slot_index) const;
    void mark_submitted(std::uint32_t frame_slot_index, GpuSubmissionTicket ticket);

    void wait_for_frame(std::uint32_t frame_slot_index) const;
    void reset_fence(std::uint32_t frame_slot_index) const;
    void reset_command_buffer(std::uint32_t frame_slot_index) const;

  private:
    void create();
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    CommandPool command_pool_;
    std::vector<FrameResourceSlot> frame_slots_;
    std::vector<VkSemaphore> present_ready_;
    std::vector<VkFence> images_in_flight_;
    std::uint32_t current_frame_slot_index_ = 0;
};

} // namespace cubey::vulkan
