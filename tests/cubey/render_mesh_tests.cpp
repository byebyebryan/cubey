#include <cubey/render/mesh.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
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

cubey::vulkan::Device* fake_device() {
    return reinterpret_cast<cubey::vulkan::Device*>(0x55);
}

cubey::vulkan::SubmissionCoordinator fake_submission() {
    return cubey::vulkan::SubmissionCoordinator(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) {});
}

void require_mesh_rejected(cubey::vulkan::GpuRuntime& runtime,
                           const cubey::render::MeshConfig& config, const char* expected_message) {
    bool rejected = false;
    try {
        static_cast<void>(cubey::render::upload_meshes(runtime, {&config, 1}));
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()) == expected_message;
    }
    require(rejected, "mesh upload validation should reject the config");
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

void test_mesh_upload_batch_empty_is_a_noop() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    cubey::render::MeshUploadBatch batch = cubey::render::upload_meshes(runtime, {});

    require(batch.meshes.empty(), "empty mesh upload should return no meshes");
    require(batch.uploaded_byte_count == 0, "empty mesh upload should report zero bytes");
    require(batch.transfer_submission_count == 0,
            "empty mesh upload should report zero transfer submissions");
    require(runtime.empty(), "empty mesh upload should not enqueue GPU work");
}

void test_mesh_upload_batch_validates_all_configs_before_gpu_work() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    const std::array<TestVertex, 3> vertices{};
    const std::array<std::uint16_t, 3> indices{};

    cubey::render::MeshConfig missing_vertices =
        cubey::render::indexed_mesh_config(vertices, indices);
    missing_vertices.vertex_data = nullptr;
    require_mesh_rejected(runtime, missing_vertices, "mesh vertex data must be nonempty");

    cubey::render::MeshConfig missing_indices =
        cubey::render::indexed_mesh_config(vertices, indices);
    missing_indices.index_data = nullptr;
    require_mesh_rejected(runtime, missing_indices, "mesh index data must be nonempty");

    cubey::render::MeshConfig wrong_index_size =
        cubey::render::indexed_mesh_config(vertices, indices);
    wrong_index_size.index_bytes -= 1;
    require_mesh_rejected(runtime, wrong_index_size,
                          "mesh index byte size does not match its index count and type");

    cubey::render::MeshConfig second_invalid =
        cubey::render::indexed_mesh_config(vertices, indices);
    second_invalid.index_count = 0;
    const std::array configs{
        cubey::render::indexed_mesh_config(vertices, indices),
        second_invalid,
    };
    bool rejected_batch = false;
    try {
        static_cast<void>(cubey::render::upload_meshes(runtime, configs));
    } catch (const std::runtime_error& error) {
        rejected_batch = std::string(error.what()) == "mesh index count must be positive";
    }
    require(rejected_batch, "mesh upload should validate later configs before GPU work");
    require(runtime.empty(), "invalid mesh batches should not enqueue GPU work");
}
