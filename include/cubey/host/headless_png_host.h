#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/profiling.h>
#include <cubey/core/run_config.h>
#include <cubey/engine/capture_queue.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace cubey::host {

using HeadlessRenderTarget = cubey::render::ColorTargetView;

struct HeadlessCaptureFrame {
    std::uint32_t index = 0;
    std::uint32_t count = 1;
    cubey::render::FrameSlot frame_slot{};
    FrameTiming timing{};
};

class HeadlessPngContext {
  public:
    HeadlessPngContext(const RunConfig& config, cubey::vulkan::Instance& instance,
                       cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                       const HeadlessRenderTarget& target,
                       cubey::profiling::ProfileRecorder* profile_recorder);

    [[nodiscard]] const RunConfig& config() const {
        return config_;
    }
    [[nodiscard]] cubey::vulkan::Instance& instance() const {
        return instance_;
    }
    [[nodiscard]] cubey::vulkan::Device& device() const {
        return device_;
    }
    [[nodiscard]] cubey::vulkan::GpuRuntime& gpu() const {
        return gpu_;
    }
    [[nodiscard]] const HeadlessRenderTarget& render_target() const {
        return target_;
    }
    [[nodiscard]] cubey::profiling::ProfileRecorder* profile_recorder() const {
        return profile_recorder_;
    }

  private:
    const RunConfig& config_;
    cubey::vulkan::Instance& instance_;
    cubey::vulkan::Device& device_;
    cubey::vulkan::GpuRuntime& gpu_;
    const HeadlessRenderTarget& target_;
    cubey::profiling::ProfileRecorder* profile_recorder_ = nullptr;
};

struct HeadlessPngHostConfig {
    RunConfig run_config;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    VkFormat output_format = VK_FORMAT_R8G8B8A8_SRGB;
    bool require_dynamic_rendering = true;
    bool require_tessellation_shader = false;
    cubey::vulkan::GpuRuntimeExecutionMode gpu_execution_mode =
        cubey::vulkan::GpuRuntimeExecutionMode::Threaded;
};

struct HeadlessPngHostCallbacks {
    std::function<void(HeadlessPngContext&)> create_resources;
    std::function<void(HeadlessPngContext&)> before_capture;
    std::function<void(HeadlessPngContext&, const HeadlessCaptureFrame&)> before_frame;
    std::function<void(HeadlessPngContext&, VkCommandBuffer, const HeadlessRenderTarget&)>
        record_capture;
    std::function<void(HeadlessPngContext&, const HeadlessCaptureFrame&, VkCommandBuffer,
                       const HeadlessRenderTarget&)>
        record_frame;
    std::function<void(HeadlessPngContext&)> shutdown;
};

struct HeadlessSimulationDriver {
    std::uint32_t png_frame_count = 0;
    std::function<FrameTiming(std::uint64_t simulation_frame)> png_timing;
    std::function<void(HeadlessPngContext&, const HeadlessCaptureFrame&)> simulate_frame;
};

[[nodiscard]] std::size_t headless_png_byte_size(std::uint32_t width, std::uint32_t height);
[[nodiscard]] inline std::size_t headless_capture_rgba8_byte_size(std::uint32_t width,
                                                                  std::uint32_t height) {
    return headless_png_byte_size(width, height);
}
[[nodiscard]] std::uint32_t headless_capture_frame_slot_count(const RunConfig& config);
[[nodiscard]] std::uint32_t headless_capture_frame_count(const RunConfig& config);
[[nodiscard]] HeadlessCaptureFrame headless_capture_frame(const RunConfig& config,
                                                          std::uint32_t frame_index);
[[nodiscard]] FrameTiming headless_video_simulation_timing(const HeadlessCaptureFrame& frame);
[[nodiscard]] HeadlessCaptureFrame headless_simulation_frame(const RunConfig& config,
                                                             std::uint32_t frame_index,
                                                             std::uint32_t frame_count,
                                                             FrameTiming timing);
void install_headless_simulation_driver(HeadlessPngHostCallbacks& callbacks, RunConfig config,
                                        HeadlessSimulationDriver driver);

class HeadlessPngHost {
  public:
    HeadlessPngHost(HeadlessPngHostConfig config, HeadlessPngHostCallbacks callbacks);
    ~HeadlessPngHost();

    HeadlessPngHost(const HeadlessPngHost&) = delete;
    HeadlessPngHost& operator=(const HeadlessPngHost&) = delete;

    int run();

  private:
    void create_instance();
    void create_device();
    void create_submission_coordinator();
    void create_gpu_runtime();
    void create_project_gpu_services();
    void create_profile_recorder();
    void write_profile_outputs();
    void drain_gpu_work();
    [[nodiscard]] cubey::profiling::ProfileRecorder* profile_recorder();
    [[nodiscard]] cubey::profiling::ScopedCpuProfileSpan profile_span(std::uint64_t frame_index,
                                                                      std::string_view label);
    void record_profile_frame(const HeadlessCaptureFrame& frame,
                              const HeadlessRenderTarget& target);
    void record_capture(HeadlessPngContext& context, const HeadlessRenderTarget& target,
                        const HeadlessCaptureFrame& frame);
    [[nodiscard]] ProjectGpuReadbackResult readback_target(const HeadlessRenderTarget& target,
                                                           const char* label);
    void write_png(const HeadlessRenderTarget& target);
    void write_video(HeadlessPngContext& context, const HeadlessRenderTarget& target);
    void shutdown_resources(HeadlessPngContext& context);

    [[nodiscard]] cubey::vulkan::Instance& instance();
    [[nodiscard]] cubey::vulkan::Device& device();
    [[nodiscard]] cubey::vulkan::SubmissionCoordinator& submission();
    [[nodiscard]] cubey::vulkan::GpuRuntime& gpu();
    [[nodiscard]] ProjectGpuServices& project_gpu();

    HeadlessPngHostConfig config_;
    HeadlessPngHostCallbacks callbacks_;
    bool shutdown_called_ = false;
    jobs::JobSystem encoding_jobs_;
    CaptureQueue captures_;
    UploadQueue uploads_;
    vulkan::DeferredGpuDestructionQueue deferred_destruction_;
    std::optional<cubey::vulkan::Instance> instance_;
    std::optional<cubey::vulkan::Device> device_;
    std::optional<cubey::vulkan::SubmissionCoordinator> submission_;
    std::optional<cubey::vulkan::GpuRuntime> gpu_;
    std::optional<ProjectGpuServices> project_gpu_;
    std::optional<cubey::profiling::ProfileRecorder> profile_recorder_;
};

using HeadlessCaptureContext = HeadlessPngContext;
using HeadlessCaptureHostConfig = HeadlessPngHostConfig;
using HeadlessCaptureHostCallbacks = HeadlessPngHostCallbacks;
using HeadlessCaptureHost = HeadlessPngHost;

} // namespace cubey::host
