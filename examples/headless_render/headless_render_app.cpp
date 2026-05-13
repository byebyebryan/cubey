#include "headless_render_app.h"

#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

namespace cubey::examples::headless_render {
namespace {

void record_clear(VkCommandBuffer command_buffer, const cubey::host::HeadlessRenderTarget& target) {
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    cubey::render::record_render_target_pass(
        recorder, cubey::render::render_target_view(target),
        cubey::render::RenderClearValues{
            .color = cubey::render::color_clear_value(0.12F, 0.18F, 0.26F, 1.0F),
        },
        [](const cubey::vulkan::CommandRecorder&) {});
}

} // namespace

int run_headless_render(const RunConfig& config) {
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.record_capture = [](cubey::host::HeadlessPngContext&, VkCommandBuffer command_buffer,
                                  const cubey::host::HeadlessRenderTarget& target) {
        record_clear(command_buffer, target);
    };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

} // namespace cubey::examples::headless_render
