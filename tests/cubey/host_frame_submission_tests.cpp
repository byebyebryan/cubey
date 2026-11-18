#include <cubey/engine/video_encoder.h>
#include <cubey/host/headless_png_host.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

cubey::host::HeadlessPngHostConfig render_only_config(std::uint32_t frames) {
    cubey::host::HeadlessPngHostConfig config;
    config.run_config.headless = true;
    config.run_config.headless_render_only = true;
    config.run_config.width = 16;
    config.run_config.height = 16;
    config.run_config.frames = frames;
    config.gpu_execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline;
    return config;
}

std::filesystem::path temporary_capture_path(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("cubey-frame-submit-" + std::to_string(stamp) + suffix);
}

} // namespace

void test_headless_frame_submission_callbacks() {
    {
        cubey::host::HeadlessPngHostConfig config = render_only_config(3);
        std::vector<std::uint32_t> recorded;
        std::vector<cubey::vulkan::GpuSubmissionTicket> submitted;
        std::vector<std::uint32_t> discarded;
        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.record_frame = [&recorded](cubey::host::HeadlessPngContext&,
                                             const cubey::host::HeadlessCaptureFrame& frame,
                                             VkCommandBuffer,
                                             const cubey::host::HeadlessRenderTarget&) {
            recorded.push_back(frame.index);
        };
        callbacks.frame_submitted = [&submitted](cubey::host::HeadlessPngContext&,
                                                 const cubey::host::HeadlessCaptureFrame& frame,
                                                 cubey::vulkan::GpuSubmissionTicket ticket) {
            require(ticket.value != 0U, "submitted callback should receive a nonzero ticket");
            require(frame.index == submitted.size(),
                    "render-only submission callbacks should preserve frame order");
            submitted.push_back(ticket);
        };
        callbacks.frame_discarded = [&discarded](cubey::host::HeadlessPngContext&,
                                                 const cubey::host::HeadlessCaptureFrame& frame) {
            discarded.push_back(frame.index);
        };
        cubey::host::HeadlessPngHost host(config, std::move(callbacks));
        require(host.run() == 0, "render-only notification capture should succeed");
        require(recorded == std::vector<std::uint32_t>{0, 1, 2},
                "render-only host should record every application frame");
        require(submitted.size() == recorded.size(),
                "render-only host should notify once for each accepted submission");
        require(discarded.empty(), "accepted render-only frames should not be discarded");
        require(submitted[0].value < submitted[1].value && submitted[1].value < submitted[2].value,
                "render-only callbacks should receive the actual ordered tickets");
    }

    {
        cubey::host::HeadlessPngHostConfig config = render_only_config(1);
        std::uint32_t submitted = 0;
        std::uint32_t discarded = 0;
        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.record_frame = [](cubey::host::HeadlessPngContext&,
                                    const cubey::host::HeadlessCaptureFrame&, VkCommandBuffer,
                                    const cubey::host::HeadlessRenderTarget&) {
            throw std::runtime_error("host frame recording failure");
        };
        callbacks.frame_submitted =
            [&submitted](cubey::host::HeadlessPngContext&, const cubey::host::HeadlessCaptureFrame&,
                         cubey::vulkan::GpuSubmissionTicket) { ++submitted; };
        callbacks.frame_discarded = [&discarded](cubey::host::HeadlessPngContext&,
                                                 const cubey::host::HeadlessCaptureFrame&) {
            ++discarded;
        };
        cubey::host::HeadlessPngHost host(config, std::move(callbacks));
        bool original_error_preserved = false;
        try {
            static_cast<void>(host.run());
        } catch (const std::runtime_error& error) {
            original_error_preserved = std::string(error.what()) == "host frame recording failure";
        }
        require(original_error_preserved, "host should preserve the recording exception");
        require(submitted == 0, "a failed recording should not be reported as submitted");
        require(discarded == 1, "a failed recording should be discarded exactly once");
    }

    {
        cubey::host::HeadlessPngHostConfig config = render_only_config(1);
        std::uint32_t submitted = 0;
        std::uint32_t discarded = 0;
        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.record_frame = [](cubey::host::HeadlessPngContext&,
                                    const cubey::host::HeadlessCaptureFrame&, VkCommandBuffer,
                                    const cubey::host::HeadlessRenderTarget&) {};
        callbacks.frame_submitted = [&submitted](cubey::host::HeadlessPngContext&,
                                                 const cubey::host::HeadlessCaptureFrame&,
                                                 cubey::vulkan::GpuSubmissionTicket ticket) {
            require(ticket.value != 0U, "throwing observer should still receive a real ticket");
            ++submitted;
            throw std::runtime_error("host submitted observer failure");
        };
        callbacks.frame_discarded = [&discarded](cubey::host::HeadlessPngContext&,
                                                 const cubey::host::HeadlessCaptureFrame&) {
            ++discarded;
        };
        cubey::host::HeadlessPngHost host(config, std::move(callbacks));
        bool observer_error_preserved = false;
        try {
            static_cast<void>(host.run());
        } catch (const std::runtime_error& error) {
            observer_error_preserved =
                std::string(error.what()) == "host submitted observer failure";
        }
        require(observer_error_preserved, "host should preserve submitted observer exceptions");
        require(submitted == 1, "throwing submitted observer should not be retried");
        require(discarded == 0, "an accepted frame must not be discarded after observer failure");
    }

    {
        cubey::host::HeadlessPngHostConfig config;
        config.run_config.headless = true;
        config.run_config.width = 16;
        config.run_config.height = 16;
        config.run_config.capture_mode = cubey::CaptureMode::Png;
        config.run_config.output_path = temporary_capture_path(".png");
        config.gpu_execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline;
        std::uint32_t simulation_steps = 0;
        std::vector<cubey::vulkan::GpuSubmissionTicket> submitted;
        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.record_frame = [](cubey::host::HeadlessPngContext&,
                                    const cubey::host::HeadlessCaptureFrame&, VkCommandBuffer,
                                    const cubey::host::HeadlessRenderTarget&) {};
        callbacks.frame_submitted = [&submitted](cubey::host::HeadlessPngContext&,
                                                 const cubey::host::HeadlessCaptureFrame&,
                                                 cubey::vulkan::GpuSubmissionTicket ticket) {
            submitted.push_back(ticket);
        };
        cubey::host::install_headless_simulation_driver(
            callbacks, config.run_config,
            {.png_frame_count = 3,
             .png_timing =
                 [](std::uint64_t frame) {
                     return cubey::FrameTiming{.delta_seconds = 1.0 / 30.0,
                                               .elapsed_seconds = static_cast<double>(frame) / 30.0,
                                               .frame_index = frame};
                 },
             .simulate_frame =
                 [&simulation_steps](cubey::host::HeadlessPngContext&,
                                     const cubey::host::HeadlessCaptureFrame&) {
                     ++simulation_steps;
                 }});
        cubey::host::HeadlessPngHost host(config, std::move(callbacks));
        require(host.run() == 0, "PNG capture with simulation should succeed");
        require(simulation_steps == 3, "PNG simulation should retain its authored frame count");
        require(submitted.size() == 1U && submitted.front().value != 0U,
                "PNG simulation updates should not be reported as application submissions");
        std::filesystem::remove(config.run_config.output_path);
    }

    if (cubey::video_encoding_available()) {
        cubey::host::HeadlessPngHostConfig config;
        config.run_config.headless = true;
        config.run_config.width = 16;
        config.run_config.height = 16;
        config.run_config.capture_mode = cubey::CaptureMode::Video;
        config.run_config.frames = 2;
        config.run_config.fps = 30;
        config.run_config.output_path = temporary_capture_path(".mp4");
        config.gpu_execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline;
        std::vector<cubey::vulkan::GpuSubmissionTicket> submitted;
        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.record_frame = [](cubey::host::HeadlessPngContext&,
                                    const cubey::host::HeadlessCaptureFrame&, VkCommandBuffer,
                                    const cubey::host::HeadlessRenderTarget&) {};
        callbacks.frame_submitted = [&submitted](cubey::host::HeadlessPngContext&,
                                                 const cubey::host::HeadlessCaptureFrame&,
                                                 cubey::vulkan::GpuSubmissionTicket ticket) {
            submitted.push_back(ticket);
        };
        cubey::host::HeadlessPngHost host(config, std::move(callbacks));
        require(host.run() == 0, "video capture notification path should succeed");
        require(submitted.size() == 2U && submitted[0].value != 0U &&
                    submitted[0].value < submitted[1].value,
                "video should notify once with each accepted frame ticket");
        std::filesystem::remove(config.run_config.output_path);
    }
}
