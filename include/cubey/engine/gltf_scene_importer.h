#pragma once

#include <cubey/animation/gltf_animation.h>
#include <cubey/asset/gltf_asset.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/renderable_manager.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cubey {

class Engine;
class SceneEditQueue;
class SceneTransaction;

namespace vulkan {
class Device;
class GpuRuntime;
} // namespace vulkan

struct GltfImportedPrimitive3D {
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    Bounds3D local_bounds{};
};

struct GltfSceneImportConfig {
    std::uint32_t scene_index = asset::kInvalidAssetIndex;
    std::uint32_t frame_slot_count = 1;
    std::string label_prefix = "gltf";
};

struct GltfSceneImportResult {
    std::vector<Entity> root_entities{};
    std::vector<Entity> node_entities{};
    Bounds3D bounds{};
    std::uint32_t triangle_count = 0;
    std::vector<render::MeshHandle> mesh_handles{};
    std::vector<render::MaterialHandle> material_handles{};
    render::MaterialHandle first_material_handle{};
};

struct GltfSceneImportResources {
    render::MeshResourceTable<render::Mesh> meshes{};
    render::MaterialResourceTable<render::FrameUniformMaterialInstance<render::PbrMaterialUniforms>>
        material_instances{};
    std::unordered_map<render::MaterialHandle, render::PbrMaterialFactors,
                       render::MaterialHandleHash>
        material_factors{};
    std::vector<std::vector<GltfImportedPrimitive3D>> mesh_primitives{};
    std::vector<render::Texture2D> textures{};
    std::optional<render::Texture2D> base_color_default{};
    std::optional<render::Texture2D> metallic_roughness_default{};
    std::optional<render::Texture2D> normal_default{};
    std::optional<render::Texture2D> occlusion_default{};
    std::optional<render::Texture2D> emissive_default{};
    bool active = false;
};

[[nodiscard]] GltfSceneImportResult
import_gltf_scene(Engine& engine, SceneTransaction& transaction, const asset::GltfAsset& asset,
                  const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                  GltfSceneImportResources& resources, GltfSceneImportConfig config = {});

void destroy_gltf_scene_import(Engine& engine, GltfSceneImportResources& resources,
                               GltfSceneImportResult& result);

void apply_gltf_rigid_animation_sample(SceneEditQueue& edits, const asset::GltfAsset& asset,
                                       const GltfSceneImportResult& result,
                                       const animation::GltfAnimationSample& sample);

} // namespace cubey
