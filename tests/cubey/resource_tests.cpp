#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_resource_helpers_describe_device_local_upload_and_depth_setup() {
    constexpr VkDeviceSize byte_size = 96;

    const cubey::vulkan::BufferConfig staging = cubey::vulkan::staging_buffer_config(byte_size);
    require(staging.size == byte_size, "staging buffer should preserve byte size");
    require(staging.usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            "staging buffer should be a transfer source");
    require(staging.memory_properties ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            "staging buffer should use host-visible coherent memory");

    const cubey::vulkan::BufferConfig vertex =
        cubey::vulkan::device_local_buffer_config(byte_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    require(vertex.size == byte_size, "device buffer should preserve byte size");
    require((vertex.usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0,
            "device buffer should preserve requested usage");
    require((vertex.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0,
            "device buffer should accept staging copies");
    require(vertex.memory_properties == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            "device buffer should use device-local memory");

    const VkExtent2D extent{1280, 720};
    const VkFormat format = VK_FORMAT_D32_SFLOAT;
    const cubey::vulkan::ImageConfig depth = cubey::vulkan::depth_image_config(extent, format);
    require(depth.extent.width == extent.width, "depth image should preserve width");
    require(depth.extent.height == extent.height, "depth image should preserve height");
    require(depth.extent.depth == 1, "depth image should be 2D");
    require(depth.format == format, "depth image should preserve format");
    require(depth.usage == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            "depth image should be a depth attachment");
    require(depth.aspect == VK_IMAGE_ASPECT_DEPTH_BIT, "depth image should use depth aspect");

    const cubey::vulkan::ImageConfig color =
        cubey::vulkan::color_render_target_image_config(extent, VK_FORMAT_R8G8B8A8_UNORM);
    require(color.extent.width == extent.width, "color render target should preserve width");
    require(color.extent.height == extent.height, "color render target should preserve height");
    require(color.extent.depth == 1, "color render target should be 2D");
    require(color.format == VK_FORMAT_R8G8B8A8_UNORM, "color render target should preserve format");
    require((color.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0,
            "color render target should support color attachment rendering");
    require((color.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0,
            "color render target should support readback copies");
    require(color.aspect == VK_IMAGE_ASPECT_COLOR_BIT,
            "color render target should use color aspect");

    static_assert(!std::is_copy_constructible_v<cubey::vulkan::Buffer>);
    static_assert(std::is_move_constructible_v<cubey::vulkan::Buffer>);
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::Image>);
    static_assert(std::is_move_constructible_v<cubey::vulkan::Image>);
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::Sampler>);
    static_assert(std::is_move_constructible_v<cubey::vulkan::Sampler>);
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::DepthAttachment>);
    static_assert(std::is_move_constructible_v<cubey::vulkan::DepthAttachment>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::copy_buffer),
                                 void (*)(cubey::vulkan::GpuOwnerContext&, VkBuffer, VkBuffer,
                                          VkDeviceSize)>);
    using UploadDeviceBufferRuntime = cubey::vulkan::Buffer (*)(
        cubey::vulkan::GpuRuntime&, const void*, VkDeviceSize, VkBufferUsageFlags);
    static_assert(std::is_same_v<decltype(static_cast<UploadDeviceBufferRuntime>(
                                     &cubey::vulkan::upload_device_buffer)),
                                 UploadDeviceBufferRuntime>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::choose_depth_format),
                                 VkFormat (*)(const cubey::vulkan::Device&)>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::Device::supports_texture_compression_bc),
                                 bool (cubey::vulkan::Device::*)() const>);
    static_assert(std::is_constructible_v<cubey::vulkan::DepthAttachment,
                                          const cubey::vulkan::Device&, VkExtent2D>);
}

void test_transfer_helpers_describe_texture_and_readback_paths() {
    constexpr VkDeviceSize byte_size = 256;

    const cubey::vulkan::BufferConfig readback = cubey::vulkan::readback_buffer_config(byte_size);
    require(readback.size == byte_size, "readback buffer should preserve byte size");
    require(readback.usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            "readback buffer should be a transfer destination");
    require(readback.memory_properties ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            "readback buffer should use host-visible coherent memory");

    const VkExtent2D extent{64, 32};
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    const cubey::vulkan::ImageConfig generated =
        cubey::vulkan::storage_sampled_image_config(extent, format);
    require(generated.extent.width == extent.width,
            "generated texture config should preserve width");
    require(generated.extent.height == extent.height,
            "generated texture config should preserve height");
    require((generated.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0,
            "generated texture config should support storage writes");
    require((generated.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0,
            "generated texture config should support sampling");
    require((generated.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0,
            "generated texture config should support readback copies");
    require(generated.aspect == VK_IMAGE_ASPECT_COLOR_BIT,
            "generated texture config should use color aspect");

    const cubey::vulkan::ImageConfig uploaded =
        cubey::vulkan::transfer_sampled_image_config(extent, format);
    require((uploaded.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0,
            "uploaded texture config should support transfer writes");
    require((uploaded.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0,
            "uploaded texture config should support sampling");

    const cubey::vulkan::ImageConfig cube =
        cubey::vulkan::transfer_sampled_cube_image_config(64, 5, format);
    require(cube.extent.width == 64 && cube.extent.height == 64,
            "cube texture config should preserve square extent");
    require(cube.mip_levels == 5, "cube texture config should preserve mip count");
    require(cube.array_layers == 6, "cube texture config should use six faces");
    require((cube.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0,
            "cube texture config should be cube compatible");
    require(cube.view_type == VK_IMAGE_VIEW_TYPE_CUBE,
            "cube texture config should create cube views");

    const VkExtent3D copy_extent{extent.width, extent.height, 1};
    const VkBufferImageCopy copy = cubey::vulkan::buffer_image_copy(copy_extent);
    require(copy.imageExtent.width == copy_extent.width, "copy region should preserve width");
    require(copy.imageExtent.height == copy_extent.height, "copy region should preserve height");
    require(copy.imageExtent.depth == copy_extent.depth, "copy region should preserve depth");
    require(copy.imageSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT,
            "copy region should target color aspect");
    require(copy.imageSubresource.layerCount == 1, "copy region should target one layer");

    const VkBufferImageCopy cube_copy = cubey::vulkan::buffer_image_copy(
        cubey::vulkan::BufferImageCopyConfig{
            .extent = {16, 16, 1},
            .buffer_offset = 4096,
            .mip_level = 2,
            .base_array_layer = 4,
            .layer_count = 1,
        });
    require(cube_copy.bufferOffset == 4096, "copy region should preserve buffer offset");
    require(cube_copy.imageSubresource.mipLevel == 2,
            "copy region should preserve mip level");
    require(cube_copy.imageSubresource.baseArrayLayer == 4,
            "copy region should preserve base array layer");

    using CopyBufferToImageRuntime =
        void (*)(cubey::vulkan::GpuRuntime&, VkBuffer, VkImage, VkExtent3D);
    static_assert(std::is_same_v<decltype(static_cast<CopyBufferToImageRuntime>(
                                     &cubey::vulkan::copy_buffer_to_image)),
                                 CopyBufferToImageRuntime>);
    using CopyImageToBufferRuntime =
        void (*)(cubey::vulkan::GpuRuntime&, VkImage, VkBuffer, VkExtent3D);
    static_assert(std::is_same_v<decltype(static_cast<CopyImageToBufferRuntime>(
                                     &cubey::vulkan::copy_image_to_buffer)),
                                 CopyImageToBufferRuntime>);
    static_assert(
        std::is_same_v<decltype(&cubey::vulkan::Buffer::download),
                       void (cubey::vulkan::Buffer::*)(void*, VkDeviceSize, VkDeviceSize) const>);
}
