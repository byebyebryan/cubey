#include <cubey/render/uniform_buffer.h>

#include <stdexcept>

namespace cubey::render {

cubey::vulkan::BufferConfig frame_uniform_buffer_config(VkDeviceSize byte_size) {
    if (byte_size == 0) {
        throw std::runtime_error("frame uniform buffer size must be positive");
    }

    return {
        .size = byte_size,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .memory_properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
}

} // namespace cubey::render
