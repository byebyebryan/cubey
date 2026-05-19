#include <cubey/vulkan/image.h>

#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/vk_check.h>

#include <array>
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

Image::Image(Image&& other) noexcept {
    move_from(other);
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        destroy();
        move_from(other);
    }
    return *this;
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
    if (config.mip_levels == 0) {
        throw std::runtime_error("image mip level count must be nonzero");
    }
    if (config.array_layers == 0) {
        throw std::runtime_error("image array layer count must be nonzero");
    }
    if (config.view_type == VK_IMAGE_VIEW_TYPE_CUBE && config.array_layers != 6) {
        throw std::runtime_error("cube image views require exactly six array layers");
    }
    if (config.image_type == VK_IMAGE_TYPE_3D) {
        if (config.array_layers != 1) {
            throw std::runtime_error("3D images require exactly one array layer");
        }
        if (config.view_type != VK_IMAGE_VIEW_TYPE_3D) {
            throw std::runtime_error("3D images require 3D image views");
        }
    }

    format_ = config.format;
    extent_ = config.extent;
    mip_levels_ = config.mip_levels;
    array_layers_ = config.array_layers;

    auto image_info = vk_struct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image_info.flags = config.flags;
    image_info.imageType = config.image_type;
    image_info.extent = extent_;
    image_info.mipLevels = mip_levels_;
    image_info.arrayLayers = array_layers_;
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
    view_info.viewType = config.view_type;
    view_info.format = format_;
    view_info.subresourceRange.aspectMask = config.aspect;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels_;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = array_layers_;
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

void Image::move_from(Image& other) noexcept {
    device_ = other.device_;
    image_ = other.image_;
    memory_ = other.memory_;
    view_ = other.view_;
    format_ = other.format_;
    extent_ = other.extent_;
    mip_levels_ = other.mip_levels_;
    array_layers_ = other.array_layers_;

    other.device_ = VK_NULL_HANDLE;
    other.image_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.view_ = VK_NULL_HANDLE;
    other.format_ = VK_FORMAT_UNDEFINED;
    other.extent_ = {};
    other.mip_levels_ = 1;
    other.array_layers_ = 1;
}

VkFormat choose_depth_format(const Device& device) {
    constexpr std::array<VkFormat, 2> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM,
    };

    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device.physical_device(), format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) !=
            0) {
            return format;
        }
    }

    throw std::runtime_error("no supported depth format found");
}

ImageConfig color_render_target_image_config(VkExtent2D extent, VkFormat format) {
    return {
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

ImageConfig depth_image_config(VkExtent2D extent, VkFormat format) {
    return {
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

ImageConfig storage_sampled_image_config(VkExtent2D extent, VkFormat format) {
    return {
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

ImageConfig storage_sampled_volume_image_config(VkExtent3D extent, VkFormat format) {
    return {
        .extent = extent,
        .format = format,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .image_type = VK_IMAGE_TYPE_3D,
        .view_type = VK_IMAGE_VIEW_TYPE_3D,
    };
}

ImageConfig transfer_sampled_image_config(VkExtent2D extent, VkFormat format) {
    return {
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

ImageConfig transfer_sampled_cube_image_config(std::uint32_t extent, std::uint32_t mip_levels,
                                               VkFormat format) {
    return {
        .extent = {extent, extent, 1},
        .format = format,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .mip_levels = mip_levels,
        .array_layers = 6,
        .view_type = VK_IMAGE_VIEW_TYPE_CUBE,
    };
}

VkBufferImageCopy buffer_image_copy(VkExtent3D extent) {
    return buffer_image_copy(BufferImageCopyConfig{.extent = extent});
}

VkBufferImageCopy buffer_image_copy(const BufferImageCopyConfig& config) {
    const VkExtent3D extent = config.extent;
    if (extent.width == 0 || extent.height == 0 || extent.depth == 0) {
        throw std::runtime_error("buffer image copy extent must be nonzero");
    }
    if (config.layer_count == 0) {
        throw std::runtime_error("buffer image copy layer count must be nonzero");
    }

    VkBufferImageCopy copy{};
    copy.bufferOffset = config.buffer_offset;
    copy.imageSubresource.aspectMask = config.aspect;
    copy.imageSubresource.mipLevel = config.mip_level;
    copy.imageSubresource.baseArrayLayer = config.base_array_layer;
    copy.imageSubresource.layerCount = config.layer_count;
    copy.imageExtent = extent;
    return copy;
}

void copy_buffer_to_image(GpuOwnerContext& context, VkBuffer source, VkImage destination,
                          VkExtent3D extent) {
    if (source == VK_NULL_HANDLE || destination == VK_NULL_HANDLE) {
        throw std::runtime_error("buffer-to-image copy requires valid source and destination");
    }

    ImmediateCommands commands(context);
    const VkBufferImageCopy copy = buffer_image_copy(extent);
    vkCmdCopyBufferToImage(commands.command_buffer(), source, destination,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    commands.submit_and_wait();
}

void copy_buffer_to_image(GpuRuntime& gpu, VkBuffer source, VkImage destination,
                          VkExtent3D extent) {
    static_cast<void>(gpu.submit_and_wait({
        .label = "copy buffer to image",
        .work =
            [source, destination, extent](GpuOwnerContext& context) {
                copy_buffer_to_image(context, source, destination, extent);
            },
    }));
}

void copy_image_to_buffer(GpuOwnerContext& context, VkImage source, VkBuffer destination,
                          VkExtent3D extent) {
    if (source == VK_NULL_HANDLE || destination == VK_NULL_HANDLE) {
        throw std::runtime_error("image-to-buffer copy requires valid source and destination");
    }

    ImmediateCommands commands(context);
    const VkBufferImageCopy copy = buffer_image_copy(extent);
    vkCmdCopyImageToBuffer(commands.command_buffer(), source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           destination, 1, &copy);
    commands.submit_and_wait();
}

void copy_image_to_buffer(GpuRuntime& gpu, VkImage source, VkBuffer destination,
                          VkExtent3D extent) {
    static_cast<void>(gpu.submit_and_wait({
        .label = "copy image to buffer",
        .work =
            [source, destination, extent](GpuOwnerContext& context) {
                copy_image_to_buffer(context, source, destination, extent);
            },
    }));
}

DepthAttachment::DepthAttachment(const Device& device, VkExtent2D extent)
    : image_(device, depth_image_config(extent, choose_depth_format(device))) {}

} // namespace cubey::vulkan
