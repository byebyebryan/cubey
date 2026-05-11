#include <cubey/vulkan/queue_submit.h>

#include <vulkan/vulkan.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_queue_submit_info_describes_waits_commands_signals_and_fence() {
    const VkSemaphore wait = reinterpret_cast<VkSemaphore>(0x1);
    const VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(0x2);
    const VkSemaphore signal = reinterpret_cast<VkSemaphore>(0x3);
    const VkFence fence = reinterpret_cast<VkFence>(0x4);

    const cubey::vulkan::QueueSubmit submit({
        .waits =
            {
                cubey::vulkan::QueueWait{
                    .semaphore = wait,
                    .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                },
            },
        .command_buffers = {command_buffer},
        .signals = {cubey::vulkan::QueueSignal{.semaphore = signal}},
        .fence = fence,
    });

    const VkSubmitInfo& info = submit.info();
    require(info.sType == VK_STRUCTURE_TYPE_SUBMIT_INFO, "submit info should use submit sType");
    require(info.waitSemaphoreCount == 1, "submit info should expose wait semaphore count");
    require(info.pWaitSemaphores[0] == wait, "submit info should expose wait semaphore handle");
    require(info.pWaitDstStageMask[0] == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            "submit info should expose wait stage mask");
    require(info.commandBufferCount == 1, "submit info should expose command buffer count");
    require(info.pCommandBuffers[0] == command_buffer,
            "submit info should expose command buffer handle");
    require(info.signalSemaphoreCount == 1, "submit info should expose signal semaphore count");
    require(info.pSignalSemaphores[0] == signal,
            "submit info should expose signal semaphore handle");
    require(submit.fence() == fence, "queue submit should retain the submit fence");
}

void test_queue_submit_rejects_empty_command_buffer_list() {
    bool threw = false;
    try {
        (void)cubey::vulkan::QueueSubmit({});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "queue submit should require at least one command buffer");
}
