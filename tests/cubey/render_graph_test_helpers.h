#pragma once

#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <exception>
#include <stdexcept>

namespace cubey::tests::render_graph {

inline void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] inline VkImage image(std::uintptr_t value) {
    return reinterpret_cast<VkImage>(value);
}

[[nodiscard]] inline VkImageView view(std::uintptr_t value) {
    return reinterpret_cast<VkImageView>(value);
}

[[nodiscard]] inline VkBuffer buffer(std::uintptr_t value) {
    return reinterpret_cast<VkBuffer>(value);
}

[[nodiscard]] inline cubey::render::RenderGraphTextureDesc color_texture_desc(const char* label) {
    return {
        .label = label,
        .extent = {640, 360},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

[[nodiscard]] inline cubey::render::RenderGraphTextureDesc depth_texture_desc(const char* label) {
    return {
        .label = label,
        .extent = {1024, 1024},
        .format = VK_FORMAT_D32_SFLOAT,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

[[nodiscard]] inline cubey::render::RenderGraphBufferDesc buffer_desc(const char* label) {
    return {
        .label = label,
        .byte_size = 4096,
    };
}

[[nodiscard]] inline cubey::render::RenderGraphBufferState host_written_buffer_state() {
    return {
        .access_mask = VK_ACCESS_HOST_WRITE_BIT,
        .stage_mask = VK_PIPELINE_STAGE_HOST_BIT,
    };
}

} // namespace cubey::tests::render_graph
