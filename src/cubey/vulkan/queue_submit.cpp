#include <cubey/vulkan/queue_submit.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

QueueSubmit::QueueSubmit(const QueueSubmitInfo& config)
    : command_buffers_(config.command_buffers), fence_(config.fence) {
    if (command_buffers_.empty()) {
        throw std::runtime_error("queue submit requires at least one command buffer");
    }
    for (const VkCommandBuffer command_buffer : command_buffers_) {
        if (command_buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("queue submit requires non-null command buffers");
        }
    }

    wait_semaphores_.reserve(config.waits.size());
    wait_stages_.reserve(config.waits.size());
    for (const QueueWait& wait : config.waits) {
        if (wait.semaphore == VK_NULL_HANDLE) {
            throw std::runtime_error("queue submit wait requires a semaphore");
        }
        wait_semaphores_.push_back(wait.semaphore);
        wait_stages_.push_back(wait.stage_mask);
    }

    signal_semaphores_.reserve(config.signals.size());
    for (const QueueSignal& signal : config.signals) {
        if (signal.semaphore == VK_NULL_HANDLE) {
            throw std::runtime_error("queue submit signal requires a semaphore");
        }
        signal_semaphores_.push_back(signal.semaphore);
    }

    info_ = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    info_.waitSemaphoreCount = static_cast<std::uint32_t>(wait_semaphores_.size());
    info_.pWaitSemaphores = wait_semaphores_.empty() ? nullptr : wait_semaphores_.data();
    info_.pWaitDstStageMask = wait_stages_.empty() ? nullptr : wait_stages_.data();
    info_.commandBufferCount = static_cast<std::uint32_t>(command_buffers_.size());
    info_.pCommandBuffers = command_buffers_.data();
    info_.signalSemaphoreCount = static_cast<std::uint32_t>(signal_semaphores_.size());
    info_.pSignalSemaphores = signal_semaphores_.empty() ? nullptr : signal_semaphores_.data();
}

void submit_to_queue(VkQueue queue, const QueueSubmitInfo& submit_info, const char* label) {
    if (queue == VK_NULL_HANDLE) {
        throw std::runtime_error("queue submit requires a queue");
    }

    const QueueSubmit submit(submit_info);
    check(vkQueueSubmit(queue, 1, &submit.info(), submit.fence()), label);
}

void submit_to_device_queue(const Device& device, const QueueSubmitInfo& submit_info,
                            const char* label) {
    submit_to_queue(device.queue(), submit_info, label);
}

} // namespace cubey::vulkan
