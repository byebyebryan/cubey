#include <array>
#include <cstdio>
#include <cubey/engine/terrain_surface_context_resources.h>
#include <cubey/render/material_instance.h>
#include <cubey/vulkan/buffer.h>
#include <stdexcept>

namespace cubey {
struct TerrainSurfaceContextResources::State {
    vulkan::Buffer vertices, indices;
    vulkan::DescriptorSetBundle fields;
    std::uint32_t index_count;
    math::Vec3 minimum, maximum;
    State(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
          const terrain::TerrainSurfaceContextProduct& p)
        : vertices(vulkan::upload_device_buffer(gpu, p.vertices.data(),
                                                p.vertices.size() *
                                                    sizeof(terrain::TerrainSurfaceContextVertex),
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)),
          indices(vulkan::upload_device_buffer(gpu, p.indices.data(),
                                               p.indices.size() * sizeof(std::uint32_t),
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),
          fields(device, vulkan::DescriptorSetInfo(std::array{vulkan::DescriptorSetBindingConfig{
                             .binding = 0,
                             .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .stage_flags = VK_SHADER_STAGE_VERTEX_BIT}})),
          index_count(std::uint32_t(p.indices.size())), minimum(p.minimum), maximum(p.maximum) {
        render::MaterialDescriptorWriter(fields.set())
            .storage_buffer(0, vertices.handle(), vertices.size())
            .update(device);
    }
};
TerrainSurfaceContextResources::TerrainSurfaceContextResources(
    const vulkan::Device& device, vulkan::GpuRuntime& gpu,
    const terrain::TerrainSurfaceContextProduct& product) {
    static_assert(sizeof(terrain::TerrainSurfaceContextVertex) == 48U);
    if (product.vertices.empty() || product.indices.empty())
        throw std::runtime_error("empty terrain context product");
    state_ = std::make_unique<State>(device, gpu, product);
    std::printf("terrain_context_upload: once; bytes=%llu vertices=%zu triangles=%zu\n",
                static_cast<unsigned long long>(state_->vertices.size() + state_->indices.size()),
                product.vertices.size(), product.indices.size() / 3);
}
TerrainSurfaceContextResources::~TerrainSurfaceContextResources() = default;
render::TerrainSurfaceContextDraw TerrainSurfaceContextResources::draw() const {
    const auto& s = *state_;
    return {&s.vertices,   &s.indices, s.fields.set(), s.fields.layout(),
            s.index_count, s.minimum,  s.maximum};
}
} // namespace cubey
