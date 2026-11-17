#pragma once
#include "raytracing_environment.h"
#include <cubey/core/profiling.h>
#include <cubey/engine/engine.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/render/resource_table.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <nlohmann/json.hpp>

namespace cubey::projects::raytracing {
// The untextured RT vertex has no tangent attribute. Supply a valid frame for
// forward PBR rather than uploading zero tangent/handedness values.
inline render::PbrVertex forward_vertex(const Vertex& vertex) {
    const math::Vec3 normal(vertex.normal);
    const auto axis = std::abs(normal.y) < .9F ? math::Vec3(0, 1, 0) : math::Vec3(1, 0, 0);
    const auto tangent = glm::normalize(glm::cross(normal, axis));
    return {.position = math::Vec3(vertex.position), .normal = normal, .tangent = {tangent, 1}};
}
// Narrow adapter for the existing certified dragon scene. No acceleration
// structures, RT pipeline, or ray-tracing device features are used here.
class ForwardComparison {
  public:
    ForwardComparison(vulkan::Device&, vulkan::GpuRuntime&, const Scene&, std::uint32_t slots,
                      const Config&);
    ~ForwardComparison();
    void resize(VkExtent2D, VkFormat);
    void render(VkCommandBuffer, render::ColorTargetView, render::FrameSlot, const Config&,
                bool present, profiling::ProfileRecorder*, std::uint64_t);
    void finish();
    void save_hdr(const std::filesystem::path&) const;
    nlohmann::json metrics() const;

  private:
    void collect(std::uint32_t);
    vulkan::Device& device_;
    const Scene& source_;
    Engine engine_;
    cubey::Scene* scene_ = nullptr;
    Entity camera_{}, light_{}, dragon_{};
    std::unique_ptr<Environment> environment_;
    std::unique_ptr<ForwardPbrRenderer3D> renderer_;
    render::MeshResourceTable<render::Mesh> meshes_;
    std::vector<render::MeshHandle> mesh_handles_;
    std::vector<render::MaterialHandle> material_handles_;
    std::vector<render::PbrMaterialTable> materials_;
    std::optional<render::PbrDefaultTextureSet> textures_;
    std::optional<vulkan::GpuTimestampProfiler> timer_;
    std::optional<vulkan::Buffer> hdr_readback_;
    bool diagnostic_ = false, recorded_hdr_ = false;
    std::string transmission_source_, debug_view_;
    std::vector<std::optional<std::uint64_t>> recorded_;
    nlohmann::json timings_ = nlohmann::json::array();
    nlohmann::json material_identity_;
    VkExtent2D extent_{};
    VkFormat format_{};
    float thickness_ = 0;
    std::uint64_t triangles_ = 0;
};
} // namespace cubey::projects::raytracing
