#include <cubey/vulkan/immediate_commands.h>

#include <cubey/vulkan/queue_submit.h>
#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

ImmediateCommands::ImmediateCommands(const Device& device)
    : device_(device.handle()), queue_(device.queue()),
      command_pool_(device, {.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT}) {
    if (device_ == VK_NULL_HANDLE || queue_ == VK_NULL_HANDLE) {
        throw std::runtime_error("immediate commands require a valid Vulkan device and queue");
    }

    try {
        create();
    } catch (...) {
        destroy();
        throw;
    }
}

ImmediateCommands::~ImmediateCommands() {
    destroy();
}

void ImmediateCommands::submit_and_wait() {
    if (submitted_) {
        throw std::runtime_error("immediate commands were already submitted");
    }

    end_command_buffer(command_buffer_, "vkEndCommandBuffer immediate");
    submit_to_queue(queue_, {.command_buffers = {command_buffer_}}, "vkQueueSubmit immediate");
    check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle immediate");
    submitted_ = true;
}

void ImmediateCommands::create() {
    command_buffer_ = command_pool_.allocate_primary();
    begin_command_buffer(command_buffer_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void ImmediateCommands::destroy() {
    command_buffer_ = VK_NULL_HANDLE;
}

} // namespace cubey::vulkan
