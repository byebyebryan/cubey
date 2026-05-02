#include <cubey/vulkan/render_context.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <type_traits>

void test_render_context_exposes_explicit_frame_boundary() {
    cubey::vulkan::Frame frame;
    if (frame.command_buffer != VK_NULL_HANDLE) {
        throw std::runtime_error("frame command buffer should default to null");
    }
    if (frame.image_index != 0) {
        throw std::runtime_error("frame image index should default to zero");
    }
    if (frame.suboptimal) {
        throw std::runtime_error("frame should not default to suboptimal");
    }

    cubey::vulkan::RenderContextConfig config;
    if (config.device != nullptr || config.swapchain != nullptr ||
        config.frame_resources != nullptr) {
        throw std::runtime_error("render context config should default to null handles");
    }

    static_assert(std::is_same_v<decltype(frame.command_buffer), VkCommandBuffer>);
    static_assert(std::is_same_v<decltype(frame.image_index), std::uint32_t>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::RenderContext::begin_frame),
                                 cubey::vulkan::FrameResult (cubey::vulkan::RenderContext::*)(
                                     cubey::vulkan::Frame*) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::RenderContext::end_frame),
                                 cubey::vulkan::FrameResult (cubey::vulkan::RenderContext::*)(
                                     const cubey::vulkan::Frame&) const>);
    static_assert(cubey::vulkan::FrameResult::Rendered !=
                  cubey::vulkan::FrameResult::RecreateSwapchain);
}
