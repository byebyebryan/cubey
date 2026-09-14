#pragma once

#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pbr.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace cubey::render {

struct PbrDefaultTextureSpec {
    PbrMaterialBinding binding = PbrMaterialBinding::BaseColor;
    std::array<std::uint8_t, 4> rgba8{255, 255, 255, 255};
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

struct PbrDefaultTextureSet {
    Texture2D base_color;
    Texture2D metallic_roughness;
    Texture2D normal;
    Texture2D occlusion;
    Texture2D emissive;
    Texture2D specular;
    Texture2D specular_color;
    Texture2D clearcoat;
    Texture2D clearcoat_roughness;
    Texture2D clearcoat_normal;
    Texture2D sheen_color;
    Texture2D sheen_roughness;
    Texture2D anisotropy;
    Texture2D iridescence;
    Texture2D iridescence_thickness;
    Texture2D transmission;
    Texture2D volume_thickness;
};

[[nodiscard]] std::span<const PbrMaterialBinding> pbr_sampled_material_bindings() noexcept;
[[nodiscard]] std::span<const PbrDefaultTextureSpec> pbr_default_texture_specs() noexcept;
[[nodiscard]] PbrDefaultTextureSet
create_pbr_default_texture_set(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu);
[[nodiscard]] PbrDefaultTextureSet
create_pbr_default_texture_set(const cubey::vulkan::Device& device,
                               cubey::vulkan::GpuOwnerContext& gpu);
[[nodiscard]] PbrDefaultTextureSet
create_pbr_default_texture_set(const cubey::vulkan::Device& device,
                               cubey::vulkan::GpuUploadBatch& batch);
// Adopts the sampled textures described by pbr_default_texture_specs(). This
// lets resumable upload sessions create one destination texture at a time.
[[nodiscard]] PbrDefaultTextureSet make_pbr_default_texture_set(std::vector<Texture2D> textures);
[[nodiscard]] const Texture2D& pbr_default_texture(const PbrDefaultTextureSet& set,
                                                   PbrMaterialBinding binding);
[[nodiscard]] std::vector<SampledImageMaterialBinding>
pbr_default_sampled_image_bindings(const PbrDefaultTextureSet& set);

inline constexpr std::uint32_t kDefaultPbrMaterialBlockCapacity = 64U;

struct PbrMaterialUniformBlockLayout {
    VkDeviceSize uniform_stride = 0;
    VkDeviceSize uniform_block_byte_size = 0;
};

// Calculates the fixed per-material offsets in one pooled uniform buffer.
// min_uniform_buffer_offset_alignment may be zero on a synthetic/test device;
// that has the same effect as an alignment of one.
[[nodiscard]] PbrMaterialUniformBlockLayout
pbr_material_uniform_block_layout(VkDeviceSize uniform_byte_size,
                                  VkDeviceSize min_uniform_buffer_offset_alignment,
                                  std::uint32_t material_capacity);

struct PbrMaterialTableConfig {
    MaterialPassInfo material_pass{};
    std::uint32_t descriptor_set = 1U;
    std::uint32_t uniform_binding = static_cast<std::uint32_t>(PbrMaterialBinding::Uniforms);
    std::uint32_t block_capacity = kDefaultPbrMaterialBlockCapacity;
};

struct PbrMaterialTableMetrics {
    bool initialized = false;
    std::uint32_t material_count = 0;
    // Descriptor slots and uniform offsets are monotonic within a block. They
    // are reclaimed only when clear() retires the table's complete residency.
    std::uint32_t allocated_descriptor_set_count = 0;
    std::uint32_t block_count = 0;
    std::uint32_t descriptor_pool_count = 0;
    std::uint32_t uniform_buffer_count = 0;
    std::uint32_t block_capacity = 0;
    VkDeviceSize uniform_stride = 0;
    VkDeviceSize uniform_block_byte_size = 0;
    VkDeviceSize allocated_uniform_byte_size = 0;
};

class PbrMaterialRecord {
  public:
    PbrMaterialRecord(PbrMaterialDefinition definition, VkDescriptorSet descriptor_set,
                      std::uint32_t descriptor_set_index, VkDeviceSize uniform_offset);

    PbrMaterialRecord(const PbrMaterialRecord&) = delete;
    PbrMaterialRecord& operator=(const PbrMaterialRecord&) = delete;
    PbrMaterialRecord(PbrMaterialRecord&&) = delete;
    PbrMaterialRecord& operator=(PbrMaterialRecord&&) = delete;

    [[nodiscard]] const PbrMaterialDefinition& definition() const noexcept {
        return definition_;
    }
    [[nodiscard]] VkDescriptorSet descriptor_set() const noexcept {
        return descriptor_set_;
    }
    [[nodiscard]] std::uint32_t descriptor_set_index() const noexcept {
        return descriptor_set_index_;
    }
    [[nodiscard]] VkDeviceSize uniform_offset() const noexcept {
        return uniform_offset_;
    }

  private:
    const PbrMaterialDefinition definition_;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    std::uint32_t descriptor_set_index_ = 0;
    VkDeviceSize uniform_offset_ = 0;
};

class PbrMaterialTable {
  public:
    PbrMaterialTable();
    ~PbrMaterialTable();

    PbrMaterialTable(const PbrMaterialTable&) = delete;
    PbrMaterialTable& operator=(const PbrMaterialTable&) = delete;
    PbrMaterialTable(PbrMaterialTable&&) noexcept;
    PbrMaterialTable& operator=(PbrMaterialTable&&) noexcept;

    // Initialization fixes the sole descriptor schema for every PBR material
    // in this table, including the valid zero-material state.
    void initialize(const cubey::vulkan::Device& device, PbrMaterialTableConfig config);
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool contains(MaterialHandle material) const;

    [[nodiscard]] const PbrMaterialDefinition& definition(MaterialHandle material) const;
    [[nodiscard]] PbrMaterialRecord&
    emplace(MaterialHandle material, PbrMaterialDefinition definition,
            std::span<const SampledImageMaterialBinding> sampled_images);
    [[nodiscard]] PbrMaterialRecord& record(MaterialHandle material);
    [[nodiscard]] const PbrMaterialRecord& record(MaterialHandle material) const;
    [[nodiscard]] VkDescriptorSetLayout descriptor_set_layout() const;
    [[nodiscard]] PbrMaterialTableMetrics metrics() const;
    void rebind(MaterialHandle from, MaterialHandle to);
    // erase() removes the logical handle only. Immutable descriptor slots and
    // uniform offsets stay reserved until clear() retires all table residency.
    void erase(MaterialHandle material);
    // Retires records, pooled blocks, the canonical descriptor layout, and its
    // device configuration. A subsequent publication must initialize again.
    void clear();

  private:
    struct State;

    MaterialResourceTable<PbrMaterialRecord> records_{};
    std::unique_ptr<State> state_{};
};

void bind_pbr_material(const cubey::vulkan::CommandRecorder& recorder,
                       const GraphicsPipelineResource& pipeline, const PbrMaterialRecord& material);

} // namespace cubey::render
