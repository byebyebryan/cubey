#include <cubey/host/headless_png_host.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void run(bool fail_during_record) {
    bool shutdown = false, observed = false;
    cubey::host::CommonRunConfig config;
    config.headless = config.headless_render_only = true;
    config.width = config.height = 1024;
    config.frames = 4;
    config.render_fps = 0;
    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.before_frame = [fail_during_record](cubey::host::HeadlessPngContext&,
                                                  const cubey::host::HeadlessCaptureFrame& frame) {
        if (!fail_during_record && frame.index == 1)
            throw std::runtime_error("intentional offscreen failure");
    };
    callbacks.record_frame = [fail_during_record](cubey::host::HeadlessPngContext&,
                                                  const cubey::host::HeadlessCaptureFrame& frame,
                                                  VkCommandBuffer commands,
                                                  const cubey::host::HeadlessRenderTarget& target) {
        if (fail_during_record && frame.index == 1)
            throw std::runtime_error("intentional offscreen failure");
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = target.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{.2F, .3F, .4F, 1.F}};
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = target.extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(commands, &rendering);
        vkCmdEndRendering(commands);
    };
    callbacks.shutdown = [&shutdown](cubey::host::HeadlessPngContext&) { shutdown = true; };
    cubey::host::HeadlessPngHost host({.run_config = config}, std::move(callbacks));
    try {
        static_cast<void>(host.run());
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()) != "intentional offscreen failure")
            throw;
        observed = true;
    }
    if (!observed || !shutdown)
        throw std::runtime_error("offscreen failure did not reach controlled shutdown");
}
} // namespace

int main() {
    try {
        run(false);
        run(true);
        std::cout << "offscreen exception cleanup PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        const std::string message = error.what();
        if (message.find("no Vulkan") != std::string::npos ||
            message.find("vkEnumeratePhysicalDevices") != std::string::npos)
            return 77;
        return 1;
    }
}
