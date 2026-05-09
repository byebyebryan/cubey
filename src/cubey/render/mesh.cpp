#include <cubey/render/mesh.h>

#include <limits>
#include <stdexcept>

namespace cubey::render {

VkDeviceSize mesh_byte_size(std::size_t element_count, std::size_t element_size) {
    if (element_count == 0 || element_size == 0) {
        throw std::runtime_error("mesh data must be nonempty");
    }
    if (element_count > std::numeric_limits<VkDeviceSize>::max() / element_size) {
        throw std::runtime_error("mesh data is too large");
    }
    return static_cast<VkDeviceSize>(element_count * element_size);
}

std::uint32_t mesh_index_count(std::size_t index_count) {
    if (index_count == 0) {
        throw std::runtime_error("mesh indices must be nonempty");
    }
    if (index_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("mesh index count is too large");
    }
    return static_cast<std::uint32_t>(index_count);
}

Mesh::Mesh(const cubey::vulkan::Device& device, const MeshConfig& config)
    : vertex_buffer_(cubey::vulkan::upload_device_buffer(
          device, config.vertex_data, config.vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
      index_buffer_(cubey::vulkan::upload_device_buffer(
          device, config.index_data, config.index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),
      index_type_(config.index_type), index_count_(config.index_count) {
    if (index_count_ == 0) {
        throw std::runtime_error("mesh index count must be positive");
    }
}

void record_draw_item(VkCommandBuffer command_buffer, const DrawItem& item) {
    if (command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("draw item recording requires a command buffer");
    }
    if (item.mesh == nullptr) {
        throw std::runtime_error("draw item requires a mesh");
    }
    if (item.instance_count == 0) {
        throw std::runtime_error("draw item instance count must be positive");
    }

    const VkBuffer vertex_buffer = item.mesh->vertex_buffer().handle();
    constexpr VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);
    vkCmdBindIndexBuffer(command_buffer, item.mesh->index_buffer().handle(), 0,
                         item.mesh->index_type());
    vkCmdDrawIndexed(command_buffer, item.mesh->index_count(), item.instance_count,
                     item.first_index, item.vertex_offset, item.first_instance);
}

} // namespace cubey::render
