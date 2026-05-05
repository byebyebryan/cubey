#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

struct BufferConfig {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags memory_properties = 0;
};

class Buffer {
  public:
    Buffer(const Device& device, const BufferConfig& config);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    VkBuffer handle() const {
        return buffer_;
    }
    VkDeviceSize size() const {
        return size_;
    }

    void upload(const void* data, VkDeviceSize byte_size, VkDeviceSize offset = 0) const;

  private:
    void create(const BufferConfig& config);
    void destroy();
    void move_from(Buffer& other) noexcept;

    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkMemoryPropertyFlags memory_properties_ = 0;
};

[[nodiscard]] BufferConfig staging_buffer_config(VkDeviceSize byte_size);
[[nodiscard]] BufferConfig device_local_buffer_config(VkDeviceSize byte_size,
                                                      VkBufferUsageFlags usage);
void copy_buffer(const Device& device, VkBuffer source, VkBuffer destination,
                 VkDeviceSize byte_size);
[[nodiscard]] Buffer upload_device_buffer(const Device& device, const void* data,
                                          VkDeviceSize byte_size, VkBufferUsageFlags usage);

} // namespace cubey::vulkan
