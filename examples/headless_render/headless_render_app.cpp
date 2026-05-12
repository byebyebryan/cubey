#include "headless_render_app.h"

#include <cubey/runtime/headless_png_host.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

namespace cubey::examples::headless_render {
namespace {

using cubey::vulkan::vk_struct;

void record_clear(VkCommandBuffer command_buffer, const cubey::HeadlessRenderTarget& target) {
    VkClearValue clear{};
    clear.color = {{0.12F, 0.18F, 0.26F, 1.0F}};
    const VkRenderingAttachmentInfo color_attachment =
        cubey::vulkan::color_rendering_attachment(target.view, clear);

    auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea.offset = {0, 0};
    rendering.renderArea.extent = target.extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color_attachment;

    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    recorder.begin_rendering(rendering);
    recorder.end_rendering();
}

} // namespace

int run_headless_render(const RunConfig& config) {
    cubey::HeadlessPngHostConfig host_config;
    host_config.run_config = config;

    cubey::HeadlessPngHostCallbacks callbacks;
    callbacks.record_capture = [](cubey::HeadlessPngContext&, VkCommandBuffer command_buffer,
                                  const cubey::HeadlessRenderTarget& target) {
        record_clear(command_buffer, target);
    };

    cubey::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

} // namespace cubey::examples::headless_render
