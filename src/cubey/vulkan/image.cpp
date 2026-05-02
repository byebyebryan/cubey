#include <cubey/vulkan/image.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

Image::Image(const Device& device, const ImageConfig& config) : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE || device.physical_device() == VK_NULL_HANDLE) {
        throw std::runtime_error("image creation requires a valid Vulkan device");
    }

    try {
        create(device, config);
    } catch (...) {
        destroy();
        throw;
    }
}

Image::~Image() {
    destroy();
}

void Image::create(const Device& device, const ImageConfig& config) {
    if (config.extent.width == 0 || config.extent.height == 0 || config.extent.depth == 0) {
        throw std::runtime_error("image extent must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("image format must be defined");
    }
    if (config.usage == 0) {
        throw std::runtime_error("image usage must be nonzero");
    }
    if (config.aspect == 0) {
        throw std::runtime_error("image aspect must be nonzero");
    }

    format_ = config.format;
    extent_ = config.extent;

    auto image_info = vk_struct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = extent_;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format_;
    image_info.tiling = config.tiling;
    image_info.initialLayout = config.initial_layout;
    image_info.usage = config.usage;
    image_info.samples = config.samples;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device_, &image_info, nullptr, &image_), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image_, &requirements);

    auto alloc = vk_struct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex =
        device.find_memory_type(requirements.memoryTypeBits, config.memory_properties);
    check(vkAllocateMemory(device_, &alloc, nullptr, &memory_), "vkAllocateMemory image");
    check(vkBindImageMemory(device_, image_, memory_, 0), "vkBindImageMemory");

    auto view_info = vk_struct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format_;
    view_info.subresourceRange.aspectMask = config.aspect;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    check(vkCreateImageView(device_, &view_info, nullptr, &view_), "vkCreateImageView");
}

void Image::destroy() {
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

} // namespace cubey::vulkan
