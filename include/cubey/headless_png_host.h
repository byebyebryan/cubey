#pragma once

#include <cubey/run_config.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/instance.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace cubey {

struct HeadlessRenderTarget {
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

class HeadlessPngContext {
  public:
    HeadlessPngContext(const RunConfig& config, cubey::vulkan::Instance& instance,
                       cubey::vulkan::Device& device, const HeadlessRenderTarget& target);

    [[nodiscard]] const RunConfig& config() const {
        return config_;
    }
    [[nodiscard]] cubey::vulkan::Instance& instance() const {
        return instance_;
    }
    [[nodiscard]] cubey::vulkan::Device& device() const {
        return device_;
    }
    [[nodiscard]] const HeadlessRenderTarget& render_target() const {
        return target_;
    }

  private:
    const RunConfig& config_;
    cubey::vulkan::Instance& instance_;
    cubey::vulkan::Device& device_;
    const HeadlessRenderTarget& target_;
};

struct HeadlessPngHostConfig {
    RunConfig run_config;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    VkFormat output_format = VK_FORMAT_R8G8B8A8_UNORM;
    bool require_dynamic_rendering = true;
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
    void record_capture(HeadlessPngContext& context, const HeadlessRenderTarget& target);
    void write_png(const HeadlessRenderTarget& target);

    [[nodiscard]] cubey::vulkan::Instance& instance();
    [[nodiscard]] cubey::vulkan::Device& device();

    HeadlessPngHostConfig config_;
    HeadlessPngHostCallbacks callbacks_;
    std::optional<cubey::vulkan::Instance> instance_;
    std::optional<cubey::vulkan::Device> device_;
};

} // namespace cubey
