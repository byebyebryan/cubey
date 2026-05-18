#include <cubey/vulkan/render_context.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_render_context_exposes_explicit_frame_boundary() {
    cubey::vulkan::RenderFrame frame;
    if (frame.command_buffer != VK_NULL_HANDLE) {
        throw std::runtime_error("frame command buffer should default to null");
    }
    if (frame.image_index != 0) {
        throw std::runtime_error("frame image index should default to zero");
    }
    if (frame.frame_slot_index != 0) {
        throw std::runtime_error("frame slot index should default to zero");
    }
    if (frame.frame_slot_count != 1) {
        throw std::runtime_error("frame slot count should default to one");
    }
    if (frame.suboptimal) {
        throw std::runtime_error("frame should not default to suboptimal");
    }

    cubey::vulkan::RenderContextConfig config;
    if (config.device != nullptr || config.swapchain != nullptr ||
        config.frame_resources != nullptr || config.gpu != nullptr) {
        throw std::runtime_error("render context config should default to null handles");
    }

    static_assert(std::is_same_v<decltype(frame.command_buffer), VkCommandBuffer>);
    static_assert(std::is_same_v<decltype(frame.image_index), std::uint32_t>);
    static_assert(std::is_same_v<decltype(frame.frame_slot_index), std::uint32_t>);
    static_assert(std::is_same_v<decltype(frame.frame_slot_count), std::uint32_t>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::RenderContext::begin_frame),
                                 cubey::vulkan::RenderFrameResult (cubey::vulkan::RenderContext::*)(
                                     cubey::vulkan::RenderFrame*) const>);
    using EndFrame = cubey::vulkan::RenderFrameResult (cubey::vulkan::RenderContext::*)(
        const cubey::vulkan::RenderFrame&) const;
    using EndFrameWithAdditionalCommands =
        cubey::vulkan::RenderFrameResult (cubey::vulkan::RenderContext::*)(
            const cubey::vulkan::RenderFrame&, std::span<const VkCommandBuffer>) const;
    static_assert(
        std::is_same_v<decltype(static_cast<EndFrame>(&cubey::vulkan::RenderContext::end_frame)),
                       EndFrame>);
    static_assert(std::is_same_v<decltype(static_cast<EndFrameWithAdditionalCommands>(
                                     &cubey::vulkan::RenderContext::end_frame)),
                                 EndFrameWithAdditionalCommands>);
    static_assert(cubey::vulkan::RenderFrameResult::Rendered !=
                  cubey::vulkan::RenderFrameResult::RecreateSwapchain);

    cubey::vulkan::SwapchainRecreateTracker tracker;
    require(tracker.consecutive_recreates() == 0,
            "recreate tracker should start with no recreate attempts");
    for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
        tracker.record_recreate_request();
    }
    require(tracker.consecutive_recreates() == 8,
            "recreate tracker should count consecutive recreate attempts");

    bool threw = false;
    try {
        tracker.record_recreate_request();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "recreate tracker should reject too many consecutive recreates");

    tracker.reset();
    require(tracker.consecutive_recreates() == 0, "recreate tracker reset should clear count");
}
