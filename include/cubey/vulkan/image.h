#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

struct ImageConfig {
    VkExtent3D extent{1, 1, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    VkImageAspectFlags aspect = 0;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};

class Image {
  public:
    Image(const Device& device, const ImageConfig& config);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    VkImage handle() const {
        return image_;
    }
    VkImageView view() const {
        return view_;
    }
    VkFormat format() const {
        return format_;
    }
    VkExtent3D extent() const {
        return extent_;
    }

  private:
    void create(const Device& device, const ImageConfig& config);
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent3D extent_{};
};

[[nodiscard]] VkFormat choose_depth_format(const Device& device);
[[nodiscard]] ImageConfig depth_image_config(VkExtent2D extent, VkFormat format);
[[nodiscard]] ImageConfig storage_sampled_image_config(VkExtent2D extent, VkFormat format);
[[nodiscard]] ImageConfig transfer_sampled_image_config(VkExtent2D extent, VkFormat format);
[[nodiscard]] VkBufferImageCopy buffer_image_copy(VkExtent3D extent);
void copy_buffer_to_image(const Device& device, VkBuffer source, VkImage destination,
                          VkExtent3D extent);
void copy_image_to_buffer(const Device& device, VkImage source, VkBuffer destination,
                          VkExtent3D extent);

class DepthAttachment {
  public:
    DepthAttachment(const Device& device, VkExtent2D extent);
    ~DepthAttachment() = default;

    DepthAttachment(const DepthAttachment&) = delete;
    DepthAttachment& operator=(const DepthAttachment&) = delete;

    [[nodiscard]] VkImage handle() const {
        return image_.handle();
    }
    [[nodiscard]] VkImageView view() const {
        return image_.view();
    }
    [[nodiscard]] VkFormat format() const {
        return image_.format();
    }
    [[nodiscard]] VkExtent3D extent() const {
        return image_.extent();
    }

  private:
    Image image_;
};

} // namespace cubey::vulkan
