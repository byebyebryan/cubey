#include <cubey/render/pass.h>

namespace cubey::render {

VkClearValue color_clear_value(float red, float green, float blue, float alpha) {
    VkClearValue clear{};
    clear.color = {{red, green, blue, alpha}};
    return clear;
}

VkClearValue depth_clear_value(float depth, std::uint32_t stencil) {
    VkClearValue clear{};
    clear.depthStencil = {depth, stencil};
    return clear;
}

void record_fullscreen_triangle(const cubey::vulkan::CommandRecorder& recorder) {
    recorder.draw(fullscreen_triangle_vertex_count());
}

} // namespace cubey::render
