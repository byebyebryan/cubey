#pragma once

#include <cubey/animation/gltf_animation.h>
#include <cubey/asset/gltf_asset.h>
#include <cubey/render/deformation.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/renderable_manager.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cubey {

class Engine;
class SceneEditQueue;
class SceneReadView;
class SceneTransaction;

namespace vulkan {
class Device;
class GpuRuntime;
} // namespace vulkan

enum class GltfPrimitiveDeformationKind : std::uint8_t {
    Static,
    Morph,
    Skin,
    MorphSkin,
};

struct GltfImportedPrimitive3D {
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    Bounds3D local_bounds{};
    std::uint32_t mesh_index = asset::kInvalidAssetIndex;
    std::uint32_t primitive_index = asset::kInvalidAssetIndex;
    GltfPrimitiveDeformationKind deformation = GltfPrimitiveDeformationKind::Static;
};

struct GltfDeformablePrimitive3D {
    Entity entity{};
    std::uint32_t node_index = asset::kInvalidAssetIndex;
    std::uint32_t mesh_index = asset::kInvalidAssetIndex;
    std::uint32_t primitive_index = asset::kInvalidAssetIndex;
    std::uint32_t skin_index = asset::kInvalidAssetIndex;
    GltfPrimitiveDeformationKind deformation = GltfPrimitiveDeformationKind::Static;
    render::MeshHandle source_mesh{};
    render::MeshHandle output_mesh{};
    render::MaterialHandle material{};
    Bounds3D local_bounds{};
};

struct GltfSkinInfluence {
    std::array<std::uint32_t, 4> joints{0, 0, 0, 0};
    math::Vec4 weights{0.0F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(GltfSkinInfluence) == sizeof(float) * 8U);

struct GltfDeformationPrimitiveResources {
    GltfDeformablePrimitive3D primitive{};
    render::GpuDeformationPushConstants push_constants{};
    std::optional<vulkan::Buffer> base_vertices{};
    std::optional<vulkan::Buffer> morph_targets{};
    std::optional<vulkan::Buffer> skin_influences{};
    std::vector<vulkan::Buffer> morph_weights{};
    std::vector<vulkan::Buffer> joint_palettes{};
    std::vector<render::Mesh> output_meshes{};
    std::unique_ptr<vulkan::DescriptorSetArray> descriptor_sets{};
};

struct GltfDeformationResources {
    render::FrameMeshResourceTable frame_meshes{};
    std::unique_ptr<render::ComputePipelineResource> pipeline{};
    std::vector<GltfDeformationPrimitiveResources> primitives{};
};

struct GltfSceneImportConfig {
    std::uint32_t scene_index = asset::kInvalidAssetIndex;
    std::uint32_t frame_slot_count = 1;
    std::filesystem::path deformation_compute_shader{};
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
    std::vector<GltfDeformablePrimitive3D> deformable_primitives{};
    GltfDeformationResources deformation{};
    std::vector<render::Texture2D> textures{};
    std::optional<render::Texture2D> base_color_default{};
    std::optional<render::Texture2D> metallic_roughness_default{};
    std::optional<render::Texture2D> normal_default{};
    std::optional<render::Texture2D> occlusion_default{};
    std::optional<render::Texture2D> emissive_default{};
    std::optional<render::Texture2D> specular_default{};
    std::optional<render::Texture2D> specular_color_default{};
    bool active = false;
};

[[nodiscard]] GltfPrimitiveDeformationKind
gltf_primitive_deformation_kind(const asset::GltfNode& node,
                                const asset::GltfMeshPrimitive& primitive);
[[nodiscard]] bool gltf_primitive_requires_deformation(GltfPrimitiveDeformationKind kind);
[[nodiscard]] std::vector<render::GpuDeformationCommand>
gltf_deformation_commands_for_frame(const GltfSceneImportResources& resources,
                                    render::FrameSlot frame_slot);
void update_gltf_deformation_frame(GltfSceneImportResources& resources,
                                   const asset::GltfAsset& asset,
                                   const GltfSceneImportResult& result,
                                   const SceneReadView& scene_view, render::FrameSlot frame_slot,
                                   const animation::GltfAnimationSample* sample = nullptr);

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
