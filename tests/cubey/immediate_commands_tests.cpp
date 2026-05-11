#include <cubey/vulkan/immediate_commands.h>

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/submission_coordinator.h>

#include <type_traits>

void test_immediate_commands_accepts_submission_coordinator() {
    static_assert(
        std::is_constructible_v<cubey::vulkan::ImmediateCommands, const cubey::vulkan::Device&>);
    static_assert(
        std::is_constructible_v<cubey::vulkan::ImmediateCommands, const cubey::vulkan::Device&,
                                cubey::vulkan::SubmissionCoordinator&>);
}
