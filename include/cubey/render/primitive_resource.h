#pragma once

#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/resource_registry.h>
#include <cubey/render/resource_table.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <string>
#include <utility>

namespace cubey::render {

template <typename Vertex>
[[nodiscard]] MeshHandle
create_primitive_mesh_resource(RenderResourceRegistry& registry, MeshResourceTable<Mesh>& meshes,
                               cubey::vulkan::GpuRuntime& gpu, std::string label,
                               const PrimitiveMeshData<Vertex>& data) {
    const MeshHandle handle = registry.create_mesh(std::move(label));
    try {
        meshes.emplace(handle, gpu, data.mesh_config());
    } catch (...) {
        registry.destroy_mesh(handle);
        throw;
    }
    return handle;
}

inline void destroy_mesh_resource(RenderResourceRegistry& registry, MeshResourceTable<Mesh>& meshes,
                                  MeshHandle& handle) {
    if (!handle) {
        return;
    }
    if (meshes.contains(handle)) {
        meshes.erase(handle);
    }
    if (registry.is_alive(handle)) {
        registry.destroy_mesh(handle);
    }
    handle = {};
}

} // namespace cubey::render
