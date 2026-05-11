#pragma once

#include <cubey/render/target.h>
#include <cubey/run_config.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace cubey {

using HeadlessRenderTarget = cubey::render::ColorTargetView;

class HeadlessPngContext {
  public:
    HeadlessPngContext(const RunConfig& config, cubey::vulkan::Instance& instance,
                       cubey::vulkan::Device& device,
                       cubey::vulkan::SubmissionCoordinator& submission,
                       cubey::vulkan::GpuRuntime& gpu, const HeadlessRenderTarget& target);

    [[nodiscard]] const RunConfig& config() const {
        return config_;
    }
    [[nodiscard]] cubey::vulkan::Instance& instance() const {
        return instance_;
    }
    [[nodiscard]] cubey::vulkan::Device& device() const {
        return device_;
    }
    [[nodiscard]] cubey::vulkan::SubmissionCoordinator& submission() const {
        return submission_;
    }
    [[nodiscard]] cubey::vulkan::GpuRuntime& gpu() const {
        return gpu_;
    }
    [[nodiscard]] const HeadlessRenderTarget& render_target() const {
        return target_;
    }

  private:
    const RunConfig& config_;
    cubey::vulkan::Instance& instance_;
    cubey::vulkan::Device& device_;
    cubey::vulkan::SubmissionCoordinator& submission_;
    cubey::vulkan::GpuRuntime& gpu_;
    const HeadlessRenderTarget& target_;
};

struct HeadlessPngHostConfig {
    RunConfig run_config;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    VkFormat output_format = VK_FORMAT_R8G8B8A8_UNORM;
    bool require_dynamic_rendering = true;
    cubey::vulkan::GpuRuntimeExecutionMode gpu_execution_mode =
        cubey::vulkan::GpuRuntimeExecutionMode::Threaded;
};

struct HeadlessPngHostCallbacks {
    std::function<void(HeadlessPngContext&)> create_resources;
    std::function<void(HeadlessPngContext&)> before_capture;
    std::function<void(HeadlessPngContext&, VkCommandBuffer, const HeadlessRenderTarget&)>
        record_capture;
    std::function<void(HeadlessPngContext&)> shutdown;
};

[[nodiscard]] std::size_t headless_png_byte_size(std::uint32_t width, std::uint32_t height);

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
    void drain_gpu_work();
    void record_capture(HeadlessPngContext& context, const HeadlessRenderTarget& target);
    void write_png(const HeadlessRenderTarget& target);

    [[nodiscard]] cubey::vulkan::Instance& instance();
    [[nodiscard]] cubey::vulkan::Device& device();
    [[nodiscard]] cubey::vulkan::SubmissionCoordinator& submission();
    [[nodiscard]] cubey::vulkan::GpuRuntime& gpu();

    HeadlessPngHostConfig config_;
    HeadlessPngHostCallbacks callbacks_;
    std::optional<cubey::vulkan::Instance> instance_;
    std::optional<cubey::vulkan::Device> device_;
    std::optional<cubey::vulkan::SubmissionCoordinator> submission_;
    std::optional<cubey::vulkan::GpuRuntime> gpu_;
};

} // namespace cubey
