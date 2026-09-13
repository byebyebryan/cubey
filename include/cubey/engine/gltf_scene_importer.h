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
#include <cubey/vulkan/upload_step.h>

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

// Plain upload scheduling policy for a complete glTF generation. The session
// applies the target only between indivisible destination operations.
struct GltfSceneUploadPolicy {
    double owner_cpu_target_milliseconds = 2.0;
    VkDeviceSize step_byte_cap = 32ULL * 1024ULL * 1024ULL;
    VkDeviceSize copy_byte_target = 2ULL * 1024ULL * 1024ULL;
};

struct GltfSceneImportConfig {
    std::uint32_t scene_index = asset::kInvalidAssetIndex;
    std::uint32_t frame_slot_count = 1;
    std::filesystem::path deformation_compute_shader{};
    std::string label_prefix = "gltf";
    GltfSceneUploadPolicy upload_policy{};
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
    // Base vertices and indices remain owned once by meshes[mesh_index].
    // Deformation preparation owns only the per-instance payloads layered on
    // that geometry.
    std::uint32_t node_index = asset::kInvalidAssetIndex;
    std::uint32_t mesh_index = asset::kInvalidAssetIndex;
    std::uint32_t primitive_index = asset::kInvalidAssetIndex;
    std::uint32_t skin_index = asset::kInvalidAssetIndex;
    GltfPrimitiveDeformationKind deformation = GltfPrimitiveDeformationKind::Static;
    // Counts are explicit so residency validates the packed payload rather
    // than inferring shader-visible dimensions from sentinel-backed storage.
    std::uint32_t morph_target_count = 0;
    std::uint32_t joint_count = 0;
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

struct GltfSceneUploadSessionMetrics {
    // Owner advances include allocation-only callbacks. Physical steps and
    // submissions count only graphics-queue uploads containing copies.
    std::uint32_t owner_advance_count = 0;
    std::uint32_t step_count = 0;
    std::uint32_t submission_count = 0;
    std::uint64_t uploaded_byte_count = 0;
    std::uint32_t copy_count = 0;
    // Owner CPU time includes destination creation, staging/copy recording,
    // and queue submission; it is not GPU completion latency.
    double owner_total_milliseconds = 0.0;
    double owner_max_step_milliseconds = 0.0;
    double owner_target_milliseconds = 2.0;
    // Counted against owner_advance_count after a completed owner advance.
    std::uint32_t owner_over_target_step_count = 0;
    std::uint32_t backpressure_count = 0;
    // Capacity is physical host-visible staging capacity. Reserved capacity
    // includes leases which have not yet retired on the graphics queue.
    std::uint64_t pool_initial_capacity_byte_count = 0;
    std::uint64_t pool_final_capacity_byte_count = 0;
    std::uint64_t pool_peak_capacity_byte_count = 0;
    // Snapshot while recording the final physical upload submission, before
    // its ticket completes and returns those leases to the staging pool.
    std::uint64_t pool_reserved_at_final_submission_byte_count = 0;
    std::uint32_t pool_growth_count = 0;
    // Wall-clock duration from the first successful copy submission until
    // the final same-queue ticket was observed complete.
    double first_step_to_final_completion_milliseconds = 0.0;
    // Appended to retain positional aggregate compatibility for existing
    // metrics consumers while preserving the configured policy evidence.
    std::uint64_t step_byte_cap = 32ULL * 1024ULL * 1024ULL;
    std::uint64_t copy_byte_target = 2ULL * 1024ULL * 1024ULL;
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
    vulkan::GpuUploadStepTicket final_upload_step{};
    // The session copies the generation aggregate here before handing resident
    // ownership off for atomic activation.
    GltfSceneUploadSessionMetrics upload_session_metrics{};

    GltfSceneResident() = default;
    GltfSceneResident(const GltfSceneResident&) = delete;
    GltfSceneResident& operator=(const GltfSceneResident&) = delete;
    GltfSceneResident(GltfSceneResident&&) noexcept = default;
    GltfSceneResident& operator=(GltfSceneResident&&) noexcept = default;
};

// A generation-scoped, owner-thread upload driver. Normal callers advance it
// once from a later application poll; headless callers may use finish(). The
// shared prepared scene keeps decoder-owned bytes stable while staged copies
// are in flight.
class GltfSceneUploadSession;
[[nodiscard]] std::shared_ptr<GltfSceneUploadSession>
begin_gltf_scene_upload_session(std::shared_ptr<const GltfPreparedScene> prepared,
                                GltfSceneImportConfig config, vulkan::GpuRuntime& gpu);

class GltfSceneUploadSession {
  public:
    ~GltfSceneUploadSession();

    GltfSceneUploadSession(const GltfSceneUploadSession&) = delete;
    GltfSceneUploadSession& operator=(const GltfSceneUploadSession&) = delete;

    // Returns true only after the final same-queue step is complete. `wait`
    // advances the same work state machine synchronously for headless finish.
    [[nodiscard]] bool poll(vulkan::GpuRuntime& gpu, bool wait);
    [[nodiscard]] bool complete() const;
    [[nodiscard]] bool failed() const;
    [[nodiscard]] std::string failure_message() const;
    [[nodiscard]] GltfSceneUploadSessionMetrics metrics() const;
    [[nodiscard]] GltfSceneResident take_resident();

  private:
    struct Impl;
    explicit GltfSceneUploadSession(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;

    friend std::shared_ptr<GltfSceneUploadSession>
    begin_gltf_scene_upload_session(std::shared_ptr<const GltfPreparedScene>, GltfSceneImportConfig,
                                    vulkan::GpuRuntime&);
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
                                   const asset::GltfRuntimeSceneData& runtime,
                                   const GltfSceneImportResult& result,
                                   const SceneReadView& scene_view, render::FrameSlot frame_slot,
                                   const animation::GltfAnimationSample* sample = nullptr);

[[nodiscard]] GltfPreparedScene prepare_gltf_scene(const asset::GltfAsset& asset,
                                                   const GltfSceneImportConfig& config,
                                                   GltfSceneImportCapabilities capabilities = {});

[[nodiscard]] GltfSceneImportResult
activate_gltf_scene(Engine& engine, SceneTransaction& transaction,
                    const GltfPreparedScene& prepared, GltfSceneResident&& resident,
                    GltfSceneImportResources& resources, const GltfSceneImportConfig& config);

[[nodiscard]] GltfSceneImportResult
import_gltf_scene(Engine& engine, SceneTransaction& transaction, const asset::GltfAsset& asset,
                  const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                  GltfSceneImportResources& resources, const GltfSceneImportConfig& config = {});

void destroy_gltf_scene_import(Engine& engine, GltfSceneImportResources& resources,
                               GltfSceneImportResult& result);

void apply_gltf_rigid_animation_sample(SceneEditQueue& edits,
                                       const asset::GltfRuntimeSceneData& runtime,
                                       const GltfSceneImportResult& result,
                                       const animation::GltfAnimationSample& sample);

} // namespace cubey
