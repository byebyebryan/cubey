#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image.h>

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

    static_assert(!std::is_copy_constructible_v<cubey::vulkan::Buffer>);
    static_assert(std::is_move_constructible_v<cubey::vulkan::Buffer>);
    static_assert(
        std::is_same_v<decltype(&cubey::vulkan::copy_buffer),
                       void (*)(const cubey::vulkan::Device&, VkBuffer, VkBuffer, VkDeviceSize)>);
    static_assert(
        std::is_same_v<decltype(&cubey::vulkan::upload_device_buffer),
                       cubey::vulkan::Buffer (*)(const cubey::vulkan::Device&, const void*,
                                                 VkDeviceSize, VkBufferUsageFlags)>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::choose_depth_format),
                                 VkFormat (*)(const cubey::vulkan::Device&)>);
    static_assert(std::is_constructible_v<cubey::vulkan::DepthAttachment,
                                          const cubey::vulkan::Device&, VkExtent2D>);
}
