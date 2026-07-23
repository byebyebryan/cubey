#include <cubey/render/mesh.h>

#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::render {
namespace {

VkDeviceSize mesh_index_element_size(VkIndexType type) {
    switch (type) {
    case VK_INDEX_TYPE_UINT16:
        return sizeof(std::uint16_t);
    case VK_INDEX_TYPE_UINT32:
        return sizeof(std::uint32_t);
    default:
        throw std::runtime_error("mesh index type must be uint16 or uint32");
    }
}

void validate_mesh_config(const MeshConfig& config) {
    if (config.vertex_data == nullptr || config.vertex_bytes == 0) {
        throw std::runtime_error("mesh vertex data must be nonempty");
    }
    if (config.index_data == nullptr || config.index_bytes == 0) {
        throw std::runtime_error("mesh index data must be nonempty");
    }
    if (config.index_count == 0) {
        throw std::runtime_error("mesh index count must be positive");
    }

    const VkDeviceSize index_element_size = mesh_index_element_size(config.index_type);
    if (config.index_count > std::numeric_limits<VkDeviceSize>::max() / index_element_size ||
        config.index_bytes != static_cast<VkDeviceSize>(config.index_count) * index_element_size) {
        throw std::runtime_error("mesh index byte size does not match its index count and type");
    }
}

} // namespace

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

Mesh::Mesh(cubey::vulkan::GpuRuntime& gpu, const MeshConfig& config)
    : Mesh([&gpu, &config] {
          MeshUploadBatch batch = upload_meshes(gpu, std::span{&config, 1U}, "upload mesh");
          return std::move(batch.meshes.front());
      }()) {}

Mesh::Mesh(cubey::vulkan::Buffer vertex_buffer, cubey::vulkan::Buffer index_buffer,
           VkIndexType index_type, std::uint32_t index_count)
    : vertex_buffer_(std::move(vertex_buffer)), index_buffer_(std::move(index_buffer)),
      index_type_(index_type), index_count_(index_count) {}

MeshUploadBatch upload_meshes(cubey::vulkan::GpuRuntime& gpu, std::span<const MeshConfig> configs,
                              std::string label) {
    if (configs.size() > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::runtime_error("mesh upload batch is too large");
    }
    std::vector<cubey::vulkan::DeviceBufferUpload> uploads;
    uploads.reserve(configs.size() * 2);
    for (const MeshConfig& config : configs) {
        validate_mesh_config(config);
        uploads.push_back({
            .data = config.vertex_data,
            .byte_size = config.vertex_bytes,
            .usage = config.vertex_usage | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        });
        uploads.push_back({
            .data = config.index_data,
            .byte_size = config.index_bytes,
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        });
    }

    cubey::vulkan::DeviceBufferUploadBatch buffer_batch =
        cubey::vulkan::upload_device_buffers(gpu, uploads, std::move(label));
    MeshUploadBatch result{
        .meshes = {},
        .uploaded_byte_count = buffer_batch.uploaded_byte_count,
        .transfer_submission_count = buffer_batch.transfer_submission_count,
    };
    result.meshes.reserve(configs.size());
    for (std::size_t mesh_index = 0; mesh_index < configs.size(); ++mesh_index) {
        const MeshConfig& config = configs[mesh_index];
        result.meshes.push_back(Mesh(std::move(buffer_batch.buffers[mesh_index * 2]),
                                     std::move(buffer_batch.buffers[mesh_index * 2 + 1]),
                                     config.index_type, config.index_count));
    }
    return result;
}

MeshUploadBatch upload_meshes(cubey::vulkan::GpuOwnerContext& context,
                              std::span<const MeshConfig> configs) {
    if (configs.size() > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::runtime_error("mesh upload batch is too large");
    }
    std::vector<cubey::vulkan::DeviceBufferUpload> uploads;
    uploads.reserve(configs.size() * 2);
    for (const MeshConfig& config : configs) {
        validate_mesh_config(config);
        uploads.push_back({
            .data = config.vertex_data,
            .byte_size = config.vertex_bytes,
            .usage = config.vertex_usage | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        });
        uploads.push_back({
            .data = config.index_data,
            .byte_size = config.index_bytes,
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        });
    }

    cubey::vulkan::DeviceBufferUploadBatch buffer_batch =
        cubey::vulkan::upload_device_buffers(context, uploads);
    MeshUploadBatch result{
        .meshes = {},
        .uploaded_byte_count = buffer_batch.uploaded_byte_count,
        .transfer_submission_count = buffer_batch.transfer_submission_count,
    };
    result.meshes.reserve(configs.size());
    for (std::size_t mesh_index = 0; mesh_index < configs.size(); ++mesh_index) {
        const MeshConfig& config = configs[mesh_index];
        result.meshes.push_back(Mesh(std::move(buffer_batch.buffers[mesh_index * 2]),
                                     std::move(buffer_batch.buffers[mesh_index * 2 + 1]),
                                     config.index_type, config.index_count));
    }
    return result;
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
