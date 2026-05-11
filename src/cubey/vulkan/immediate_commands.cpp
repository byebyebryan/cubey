#include <cubey/vulkan/immediate_commands.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

ImmediateCommands::ImmediateCommands(const Device& device)
    : device_(device.handle()), owned_submission_(std::in_place, device),
      submission_(&owned_submission_.value()),
      command_pool_(device, {.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT}) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("immediate commands require a valid Vulkan device");
    }

    try {
        create();
    } catch (...) {
        destroy();
        throw;
    }
}

ImmediateCommands::ImmediateCommands(const Device& device, SubmissionCoordinator& submission)
    : device_(device.handle()), submission_(&submission),
      command_pool_(device, {.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT}) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("immediate commands require a valid Vulkan device");
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
    static_cast<void>(submission_->submit_and_wait({.command_buffers = {command_buffer_}},
                                                   "vkQueueSubmit immediate",
                                                   "vkQueueWaitIdle immediate"));
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
