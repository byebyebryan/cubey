#pragma once

#include <cubey/vulkan/instance.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

struct DeviceConfig {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    bool require_present = true;
};

class Device {
  public:
    Device(const Instance& instance, const DeviceConfig& config);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    VkPhysicalDevice physical_device() const {
        return physical_device_;
    }
    VkDevice handle() const {
        return device_;
    }
    VkQueue queue() const {
        return queue_;
    }
    std::uint32_t queue_family() const {
        return queue_family_;
    }
    const VkPhysicalDeviceProperties& properties() const {
        return properties_;
    }
    const char* device_name() const {
        return properties_.deviceName;
    }

    void wait_idle() const;

  private:
    void select_physical_device(const Instance& instance, const DeviceConfig& config);
    void create_device(const DeviceConfig& config);

    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
};

} // namespace cubey::vulkan
