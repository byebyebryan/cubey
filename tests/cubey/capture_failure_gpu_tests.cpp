#include <cubey/host/headless_png_host.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void run(cubey::vulkan::GpuRuntimeExecutionMode mode, bool record, unsigned failed_frame) {
    bool observed = false;
    unsigned shutdowns = 0;
    cubey::host::CommonRunConfig config;
    config.headless = true;
    config.require_validation = true;
    config.capture_mode = cubey::CaptureMode::Video;
    config.width = 64;
    config.height = 48;
    config.frames = 5;
    config.output_path = std::filesystem::temp_directory_path() /
                         ("cubey-capture-failure-" + std::to_string(record) + "-" +
                          std::to_string(failed_frame) + ".mp4");
    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.before_frame = [=](auto&, const auto& frame) {
        if (!record && frame.index == failed_frame)
            throw std::runtime_error("intentional capture failure");
    };
    callbacks.record_frame = [=](auto&, const auto& frame, VkCommandBuffer commands,
                                 const auto& target) {
        if (record && frame.index == failed_frame)
            throw std::runtime_error("intentional capture failure");
        cubey::render::RenderClearValues values;
        values.color.color = {{0.2F, 0.3F, 0.4F, 1.0F}};
        const cubey::render::RenderTargetRenderingInfo info(
            cubey::render::render_target_view(target), values);
        vkCmdBeginRendering(commands, &info.info());
        vkCmdEndRendering(commands);
    };
    callbacks.shutdown = [&](auto&) { ++shutdowns; };
    {
        cubey::host::HeadlessPngHost host({.run_config = config, .gpu_execution_mode = mode},
                                          callbacks);
        try {
            static_cast<void>(host.run());
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()) != "intentional capture failure")
                throw;
            observed = true;
        }
    }
    std::filesystem::remove(config.output_path);
    if (!observed || shutdowns != 1)
        throw std::runtime_error("capture failure lost its original error or shutdown");
}
} // namespace

int main() {
    try {
        if (!cubey::video_encoding_available()) {
            std::cout << "video capture backend unavailable\n";
            return 77;
        }
        for (const auto mode : {cubey::vulkan::GpuRuntimeExecutionMode::Inline,
                                cubey::vulkan::GpuRuntimeExecutionMode::Threaded})
            for (const bool record : {false, true})
                for (const unsigned frame : {1U, 3U})
                    run(mode, record, frame);
        std::cout
            << "capture failure PASS: pending slots, producer/record failure, inline/threaded\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
