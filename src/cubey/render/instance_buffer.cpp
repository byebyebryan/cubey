#include <cubey/render/instance_buffer.h>

#include <limits>

namespace cubey::render {

VkDeviceSize instance_buffer_byte_size(std::size_t instance_count, std::size_t instance_size) {
    if (instance_count == 0 || instance_size == 0) {
        throw std::runtime_error("instance buffer data must be nonempty");
    }
    if (instance_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("instance buffer instance count is too large");
    }
    if (instance_count > std::numeric_limits<VkDeviceSize>::max() / instance_size) {
        throw std::runtime_error("instance buffer data is too large");
    }
    return static_cast<VkDeviceSize>(instance_count * instance_size);
}

cubey::vulkan::BufferConfig instance_buffer_config(VkDeviceSize byte_size) {
    return cubey::vulkan::device_local_buffer_config(byte_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

} // namespace cubey::render
