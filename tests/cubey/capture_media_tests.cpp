#include <cubey/engine/video_encoder.h>
#include <cubey/host/headless_png_host.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Binary RGB corners survive the SDR conversion and lossy H.264 encode with
// unambiguous frame identities. Seven lengths cover every three-slot remainder.
constexpr std::array<std::array<std::uint8_t, 3>, 7> colors{
    {{{255, 0, 0}}, {{0, 255, 0}}, {{0, 0, 255}}, {{255, 255, 255}},
     {{0, 255, 255}}, {{255, 255, 0}}, {{0, 0, 0}}}};

void direct(const std::filesystem::path& path, unsigned count, unsigned fps) {
    auto encoder = cubey::create_video_encoder(
        {.output_path = path, .width = 32, .height = 24, .fps = fps});
    for (unsigned index = 0; index < count; ++index) {
        std::vector<std::uint8_t> rgba(32 * 24 * 4, 255);
        for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4)
            for (unsigned component = 0; component < 3; ++component)
                rgba[pixel + component] = colors[index][component];
        encoder->write_frame(rgba);
    }
    encoder->finish();
}

void headless(const std::filesystem::path& path, unsigned count, unsigned fps,
              cubey::vulkan::GpuRuntimeExecutionMode mode) {
    cubey::host::CommonRunConfig config;
    config.headless = true;
    config.require_validation = true;
    config.width = 32;
    config.height = 24;
    config.frames = count;
    config.fps = fps;
    config.capture_mode = cubey::CaptureMode::Video;
    config.output_path = path;
    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.record_frame = [](auto&, const auto& frame, VkCommandBuffer commands,
                                const auto& target) {
        cubey::render::RenderClearValues values;
        for (unsigned component = 0; component < 3; ++component)
            values.color.color.float32[component] =
                static_cast<float>(colors[frame.index][component]) / 255.0F;
        values.color.color.float32[3] = 1.0F;
        const cubey::render::RenderTargetRenderingInfo info(
            cubey::render::render_target_view(target), values);
        vkCmdBeginRendering(commands, &info.info());
        vkCmdEndRendering(commands);
    };
    cubey::host::HeadlessPngHost host(
        {.run_config = config, .gpu_execution_mode = mode}, callbacks);
    if (host.run() != 0)
        throw std::runtime_error("headless media fixture failed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected direct/inline/threaded and an output directory");
        const std::string mode = argv[1];
        if (mode != "direct" && mode != "inline" && mode != "threaded")
            throw std::runtime_error("unknown capture fixture mode");
        if (!cubey::video_encoding_available()) {
            std::cout << "capture media backend unavailable\n";
            return 77;
        }
        const std::filesystem::path directory(argv[2]);
        std::filesystem::create_directories(directory);
        for (const unsigned fps : {24U, 60U})
            for (unsigned count = 1; count <= colors.size(); ++count) {
                const auto path = directory / (std::to_string(count) + "-" +
                                                std::to_string(fps) + ".mp4");
                if (mode == "direct")
                    direct(path, count, fps);
                else
                    headless(path, count, fps,
                             mode == "inline" ? cubey::vulkan::GpuRuntimeExecutionMode::Inline
                                              : cubey::vulkan::GpuRuntimeExecutionMode::Threaded);
            }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
