#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

struct ImageConfig {
    VkExtent3D extent{1, 1, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    VkImageAspectFlags aspect = 0;
    VkImageCreateFlags flags = 0;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    std::uint32_t mip_levels = 1;
    std::uint32_t array_layers = 1;
    VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
};

class Image {
  public:
    Image(const Device& device, const ImageConfig& config);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

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
    std::uint32_t mip_levels() const {
        return mip_levels_;
    }
    std::uint32_t array_layers() const {
        return array_layers_;
    }

  private:
    void create(const Device& device, const ImageConfig& config);
    void destroy();
    void move_from(Image& other) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent3D extent_{};
    std::uint32_t mip_levels_ = 1;
    std::uint32_t array_layers_ = 1;
};

[[nodiscard]] VkFormat choose_depth_format(const Device& device);
[[nodiscard]] ImageConfig color_render_target_image_config(VkExtent2D extent, VkFormat format);
[[nodiscard]] ImageConfig depth_image_config(VkExtent2D extent, VkFormat format);
[[nodiscard]] ImageConfig storage_sampled_image_config(VkExtent2D extent, VkFormat format);
[[nodiscard]] ImageConfig transfer_sampled_image_config(VkExtent2D extent, VkFormat format);
[[nodiscard]] ImageConfig transfer_sampled_cube_image_config(std::uint32_t extent,
                                                            std::uint32_t mip_levels,
                                                            VkFormat format);
struct BufferImageCopyConfig {
    VkExtent3D extent{1, 1, 1};
    VkDeviceSize buffer_offset = 0;
    std::uint32_t mip_level = 0;
    std::uint32_t base_array_layer = 0;
    std::uint32_t layer_count = 1;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

[[nodiscard]] VkBufferImageCopy buffer_image_copy(VkExtent3D extent);
[[nodiscard]] VkBufferImageCopy buffer_image_copy(const BufferImageCopyConfig& config);
void copy_buffer_to_image(GpuOwnerContext& context, VkBuffer source, VkImage destination,
                          VkExtent3D extent);
void copy_buffer_to_image(GpuRuntime& gpu, VkBuffer source, VkImage destination, VkExtent3D extent);
void copy_image_to_buffer(GpuOwnerContext& context, VkImage source, VkBuffer destination,
                          VkExtent3D extent);
void copy_image_to_buffer(GpuRuntime& gpu, VkImage source, VkBuffer destination, VkExtent3D extent);

class DepthAttachment {
  public:
    DepthAttachment(const Device& device, VkExtent2D extent);
    ~DepthAttachment() = default;

    DepthAttachment(const DepthAttachment&) = delete;
    DepthAttachment& operator=(const DepthAttachment&) = delete;
    DepthAttachment(DepthAttachment&& other) noexcept = default;
    DepthAttachment& operator=(DepthAttachment&& other) noexcept = default;

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
