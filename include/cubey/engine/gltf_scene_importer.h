#pragma once

#include <cubey/animation/gltf_animation.h>
#include <cubey/asset/gltf_asset.h>
#include <cubey/render/deformation.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/renderable_manager.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cubey {

class Engine;
class SceneEditQueue;
class SceneReadView;
class SceneTransaction;

namespace vulkan {
class Device;
class GpuOwnerContext;
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

// Device capability values captured by the caller before CPU preparation begins.
// Keeping this value plain-data makes preparation safe to run outside the GPU owner.
struct GltfSceneImportCapabilities {
    bool supports_texture_compression_bc = false;
};

struct GltfPreparedTexture {
    VkExtent2D extent{1, 1};
    std::uint32_t mip_levels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool rgba8 = true;
    std::vector<std::uint8_t> bytes{};
    std::vector<render::UploadedTexture2DMip> mips{};
    vulkan::SamplerConfig sampler{};
};

struct GltfPreparedMaterialTexture {
    render::PbrMaterialBinding binding = render::PbrMaterialBinding::BaseColor;
    std::uint32_t texture_index = asset::kInvalidAssetIndex;
};

struct GltfPreparedMaterial {
    render::MaterialInfo info{};
    render::PbrMaterialFactors factors{};
    std::vector<GltfPreparedMaterialTexture> textures{};
};

struct GltfPreparedMeshPrimitive {
    std::vector<render::PbrVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    Bounds3D local_bounds{};
    std::uint32_t material_index = 0;
};

struct GltfPreparedMesh {
    std::vector<GltfPreparedMeshPrimitive> primitives{};
};

struct GltfPreparedNode {
    Transform3D transform{};
    std::uint32_t mesh_index = asset::kInvalidAssetIndex;
    std::uint32_t skin_index = asset::kInvalidAssetIndex;
    std::vector<float> weights{};
    std::vector<std::uint32_t> children{};
};

struct GltfPreparedDeformationPrimitive {
    std::uint32_t node_index = asset::kInvalidAssetIndex;
    std::uint32_t mesh_index = asset::kInvalidAssetIndex;
    std::uint32_t primitive_index = asset::kInvalidAssetIndex;
    std::uint32_t skin_index = asset::kInvalidAssetIndex;
    GltfPrimitiveDeformationKind deformation = GltfPrimitiveDeformationKind::Static;
    std::vector<render::PbrVertex> base_vertices{};
    std::vector<std::uint32_t> indices{};
    std::vector<float> morph_targets{};
    std::vector<GltfSkinInfluence> skin_influences{};
    std::vector<float> initial_morph_weights{};
    std::vector<math::Mat4> initial_joint_palette{};
};

// CPU-only import product. It owns all byte payloads required for residency and
// intentionally contains no engine handles, scenes, or Vulkan object wrappers.
struct GltfPreparedScene {
    std::vector<GltfPreparedMaterial> materials{};
    std::vector<GltfPreparedTexture> textures{};
    std::vector<GltfPreparedMesh> meshes{};
    std::vector<GltfPreparedNode> nodes{};
    std::vector<std::uint32_t> root_nodes{};
    std::vector<GltfPreparedDeformationPrimitive> deformable_primitives{};
    Bounds3D bounds{};
    std::uint32_t triangle_count = 0;
};

struct GltfSceneImportResult {
    std::vector<Entity> root_entities{};
    std::vector<Entity> node_entities{};
    Bounds3D bounds{};
    std::uint32_t triangle_count = 0;
    std::uint64_t mesh_upload_byte_count = 0;
    std::uint32_t mesh_upload_transfer_submission_count = 0;
    std::vector<render::MeshHandle> mesh_handles{};
    std::vector<render::MaterialHandle> material_handles{};
    render::MaterialHandle first_material_handle{};
};

struct GltfSceneImportResources {
    render::MeshResourceTable<render::Mesh> meshes{};
    render::PbrMaterialTable materials{};
    std::vector<std::vector<GltfImportedPrimitive3D>> mesh_primitives{};
    std::vector<GltfDeformablePrimitive3D> deformable_primitives{};
    GltfDeformationResources deformation{};
    std::vector<render::Texture2D> textures{};
    std::optional<render::PbrDefaultTextureSet> default_textures{};
    bool active = false;
};

// GPU-resident import product. Handles are local staging handles until
// activate_gltf_scene adopts them into Engine's resource registry.
struct GltfSceneResident {
    GltfSceneImportResources resources{};
    std::vector<render::MaterialHandle> material_handles{};
    std::vector<render::MeshHandle> static_mesh_handles{};
    std::vector<render::MeshHandle> deformation_mesh_handles{};
    std::uint64_t mesh_upload_byte_count = 0;
    std::uint32_t mesh_upload_transfer_submission_count = 0;

    GltfSceneResident() = default;
    GltfSceneResident(const GltfSceneResident&) = delete;
    GltfSceneResident& operator=(const GltfSceneResident&) = delete;
    GltfSceneResident(GltfSceneResident&&) noexcept = default;
    GltfSceneResident& operator=(GltfSceneResident&&) noexcept = default;
};

[[nodiscard]] GltfPrimitiveDeformationKind
gltf_primitive_deformation_kind(const asset::GltfNode& node,
                                const asset::GltfMeshPrimitive& primitive);
[[nodiscard]] Transform3D gltf_node_transform_3d(const asset::GltfNode& node);
[[nodiscard]] bool gltf_primitive_requires_deformation(GltfPrimitiveDeformationKind kind);
[[nodiscard]] std::vector<render::GpuDeformationCommand>
gltf_deformation_commands_for_frame(const GltfSceneImportResources& resources,
                                    render::FrameSlot frame_slot);
void update_gltf_deformation_frame(GltfSceneImportResources& resources,
                                   const asset::GltfAsset& asset,
                                   const GltfSceneImportResult& result,
                                   const SceneReadView& scene_view, render::FrameSlot frame_slot,
                                   const animation::GltfAnimationSample* sample = nullptr);

[[nodiscard]] GltfPreparedScene prepare_gltf_scene(const asset::GltfAsset& asset,
                                                   GltfSceneImportConfig config,
                                                   GltfSceneImportCapabilities capabilities = {});

[[nodiscard]] GltfSceneResident build_gltf_scene_resident(vulkan::GpuOwnerContext& gpu,
                                                          const GltfPreparedScene& prepared,
                                                          const GltfSceneImportConfig& config);

[[nodiscard]] GltfSceneImportResult
activate_gltf_scene(Engine& engine, SceneTransaction& transaction,
                    const GltfPreparedScene& prepared, GltfSceneResident&& resident,
                    GltfSceneImportResources& resources, const GltfSceneImportConfig& config);

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
