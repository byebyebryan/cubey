#include <cubey/render/mesh.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace {

struct TestVertex {
    float position[3];
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_indexed_mesh_config_describes_u16_geometry() {
    const std::array<TestVertex, 3> vertices{};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};

    const cubey::render::MeshConfig config = cubey::render::indexed_mesh_config(vertices, indices);

    require(config.vertex_data == vertices.data(), "mesh config should preserve vertex pointer");
    require(config.vertex_bytes == sizeof(TestVertex) * vertices.size(),
            "mesh config should compute vertex byte size");
    require(config.index_data == indices.data(), "mesh config should preserve index pointer");
    require(config.index_bytes == sizeof(std::uint16_t) * indices.size(),
            "mesh config should compute index byte size");
    require(config.index_type == VK_INDEX_TYPE_UINT16, "u16 indices should map to Vulkan u16");
    require(config.index_count == indices.size(), "mesh config should preserve index count");
}

void test_indexed_mesh_config_describes_u32_geometry() {
    const std::array<TestVertex, 4> vertices{};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 2, 3, 0};

    const cubey::render::MeshConfig config = cubey::render::indexed_mesh_config(vertices, indices);

    require(config.index_type == VK_INDEX_TYPE_UINT32, "u32 indices should map to Vulkan u32");
    require(config.index_count == indices.size(), "mesh config should preserve u32 index count");

    static_assert(!std::is_copy_constructible_v<cubey::render::Mesh>);
    static_assert(std::is_move_constructible_v<cubey::render::Mesh>);
    static_assert(std::is_constructible_v<cubey::render::Mesh, cubey::vulkan::GpuRuntime&,
                                          const cubey::render::MeshConfig&>);
    static_assert(!std::is_constructible_v<cubey::render::Mesh, const cubey::vulkan::Device&,
                                           const cubey::render::MeshConfig&>);
}

void test_indexed_mesh_config_allows_storage_capable_vertex_buffers() {
    const std::array<TestVertex, 3> vertices{};
    const std::array<std::uint32_t, 3> indices{0, 1, 2};

    const cubey::render::MeshConfig config = cubey::render::indexed_mesh_config(
        vertices, indices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    require((config.vertex_usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0,
            "mesh config should preserve vertex-buffer usage");
    require((config.vertex_usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) != 0,
            "mesh config should allow storage-capable deformation output");
}
