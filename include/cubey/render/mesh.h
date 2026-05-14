#pragma once

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace cubey::render {

struct MeshConfig {
    const void* vertex_data = nullptr;
    VkDeviceSize vertex_bytes = 0;
    VkBufferUsageFlags vertex_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    const void* index_data = nullptr;
    VkDeviceSize index_bytes = 0;
    VkIndexType index_type = VK_INDEX_TYPE_UINT16;
    std::uint32_t index_count = 0;
};

[[nodiscard]] VkDeviceSize mesh_byte_size(std::size_t element_count, std::size_t element_size);
[[nodiscard]] std::uint32_t mesh_index_count(std::size_t index_count);

namespace detail {

template <typename> inline constexpr bool always_false = false;

template <typename Index> [[nodiscard]] constexpr VkIndexType mesh_index_type() {
    if constexpr (std::is_same_v<Index, std::uint16_t>) {
        return VK_INDEX_TYPE_UINT16;
    } else if constexpr (std::is_same_v<Index, std::uint32_t>) {
        return VK_INDEX_TYPE_UINT32;
    } else {
        static_assert(always_false<Index>, "mesh indices must be uint16_t or uint32_t");
    }
}

} // namespace detail

template <typename Vertex, typename Index>
[[nodiscard]] MeshConfig indexed_mesh_config(std::span<const Vertex> vertices,
                                             std::span<const Index> indices,
                                             VkBufferUsageFlags vertex_usage =
                                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
    return {
        .vertex_data = vertices.data(),
        .vertex_bytes = mesh_byte_size(vertices.size(), sizeof(Vertex)),
        .vertex_usage = vertex_usage,
        .index_data = indices.data(),
        .index_bytes = mesh_byte_size(indices.size(), sizeof(Index)),
        .index_type = detail::mesh_index_type<Index>(),
        .index_count = mesh_index_count(indices.size()),
    };
}

template <typename Vertex, std::size_t VertexCount, typename Index, std::size_t IndexCount>
[[nodiscard]] MeshConfig indexed_mesh_config(const std::array<Vertex, VertexCount>& vertices,
                                             const std::array<Index, IndexCount>& indices,
                                             VkBufferUsageFlags vertex_usage =
                                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
    return indexed_mesh_config(std::span<const Vertex>(vertices), std::span<const Index>(indices),
                               vertex_usage);
}

class Mesh {
  public:
    Mesh(cubey::vulkan::GpuRuntime& gpu, const MeshConfig& config);
    ~Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept = default;
    Mesh& operator=(Mesh&& other) noexcept = default;

    [[nodiscard]] const cubey::vulkan::Buffer& vertex_buffer() const {
        return vertex_buffer_;
    }
    [[nodiscard]] const cubey::vulkan::Buffer& index_buffer() const {
        return index_buffer_;
    }
    [[nodiscard]] VkIndexType index_type() const {
        return index_type_;
    }
    [[nodiscard]] std::uint32_t index_count() const {
        return index_count_;
    }

  private:
    cubey::vulkan::Buffer vertex_buffer_;
    cubey::vulkan::Buffer index_buffer_;
    VkIndexType index_type_ = VK_INDEX_TYPE_UINT16;
    std::uint32_t index_count_ = 0;
};

struct DrawItem {
    const Mesh* mesh = nullptr;
    std::uint32_t instance_count = 1;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};

void record_draw_item(VkCommandBuffer command_buffer, const DrawItem& item);

} // namespace cubey::render
