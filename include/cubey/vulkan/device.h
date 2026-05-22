#pragma once

#include <cubey/vulkan/instance.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

struct DeviceConfig {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    bool require_present = true;
    bool require_dynamic_rendering = false;
};

struct DeviceMemoryBudgetInfo {
    bool available = false;
    VkDeviceSize device_local_usage = 0;
    VkDeviceSize device_local_budget = 0;
    VkDeviceSize device_local_heap_size = 0;
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
    bool supports_texture_compression_bc() const {
        return enabled_features_.textureCompressionBC == VK_TRUE;
    }
    bool supports_shader_storage_image_extended_formats() const {
        return supported_features_.shaderStorageImageExtendedFormats == VK_TRUE;
    }
    bool supports_timestamp_queries() const {
        return queue_timestamp_valid_bits_ != 0;
    }
    std::uint32_t queue_timestamp_valid_bits() const {
        return queue_timestamp_valid_bits_;
    }
    float timestamp_period_nanoseconds() const {
        return properties_.limits.timestampPeriod;
    }
    bool supports_memory_budget() const {
        return memory_budget_supported_;
    }
    [[nodiscard]] DeviceMemoryBudgetInfo device_memory_budget() const;

    [[nodiscard]] std::uint32_t find_memory_type(std::uint32_t type_bits,
                                                 VkMemoryPropertyFlags required) const;

    void wait_idle() const;

  private:
    void select_physical_device(const Instance& instance, const DeviceConfig& config);
    void create_device(const DeviceConfig& config);

    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceFeatures supported_features_{};
    VkPhysicalDeviceFeatures enabled_features_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    std::uint32_t queue_timestamp_valid_bits_ = 0;
    bool memory_budget_supported_ = false;
};

} // namespace cubey::vulkan
