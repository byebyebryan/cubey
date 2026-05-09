#include <cubey/vulkan/frame_resources.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_frame_resources_expose_slot_based_contract() {
    cubey::vulkan::FrameResourcesConfig config;
    require(config.present_ready_count == 0,
            "frame resources config should require caller-provided present count");
    require(config.frame_slot_count == 1,
            "frame resources config should default to one frame slot");

    cubey::vulkan::FrameResourceSlot slot;
    require(slot.command_buffer == VK_NULL_HANDLE,
            "frame resource slot should default command buffer to null");
    require(slot.image_available == VK_NULL_HANDLE,
            "frame resource slot should default acquire semaphore to null");
    require(slot.fence == VK_NULL_HANDLE, "frame resource slot should default fence to null");

    static_assert(std::is_constructible_v<cubey::vulkan::FrameResources,
                                          const cubey::vulkan::Device&,
                                          const cubey::vulkan::FrameResourcesConfig&>);
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::FrameResources>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::FrameResources>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::frame_slot_count),
                                 std::uint32_t (cubey::vulkan::FrameResources::*)() const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::slot),
                                 const cubey::vulkan::FrameResourceSlot& (
                                     cubey::vulkan::FrameResources::*)(std::uint32_t) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::present_ready),
                                 VkSemaphore (cubey::vulkan::FrameResources::*)(
                                     std::size_t) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::image_in_flight),
                                 VkFence (cubey::vulkan::FrameResources::*)(std::size_t) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::mark_image_in_flight),
                                 void (cubey::vulkan::FrameResources::*)(std::size_t, VkFence)>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::wait_for_frame),
                                 void (cubey::vulkan::FrameResources::*)(std::uint32_t) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::reset_fence),
                                 void (cubey::vulkan::FrameResources::*)(std::uint32_t) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::FrameResources::reset_command_buffer),
                                 void (cubey::vulkan::FrameResources::*)(
                                     std::uint32_t) const>);
}
