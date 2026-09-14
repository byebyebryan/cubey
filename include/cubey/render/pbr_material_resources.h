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

class PbrMaterialRecord {
  public:
    PbrMaterialRecord(PbrMaterialDefinition definition, const cubey::vulkan::Device& device,
                      const FrameUniformMaterialInstanceConfig& instance_config)
        : definition_(std::move(definition)), instance_(device, instance_config) {
        const PbrMaterialUniforms uniforms = pbr_material_uniforms(definition_);
        for (std::uint32_t slot_index = 0; slot_index < instance_config.frame_slot_count;
             ++slot_index) {
            instance_.upload(
                FrameSlot{.index = slot_index, .count = instance_config.frame_slot_count},
                uniforms);
        }
    }

    PbrMaterialRecord(const PbrMaterialRecord&) = delete;
    PbrMaterialRecord& operator=(const PbrMaterialRecord&) = delete;
    PbrMaterialRecord(PbrMaterialRecord&&) = delete;
    PbrMaterialRecord& operator=(PbrMaterialRecord&&) = delete;

    [[nodiscard]] const PbrMaterialDefinition& definition() const noexcept {
        return definition_;
    }
    [[nodiscard]] FrameUniformMaterialInstance<PbrMaterialUniforms>& instance() noexcept {
        return instance_;
    }
    [[nodiscard]] const FrameUniformMaterialInstance<PbrMaterialUniforms>&
    instance() const noexcept {
        return instance_;
    }

  private:
    const PbrMaterialDefinition definition_;
    FrameUniformMaterialInstance<PbrMaterialUniforms> instance_;
};

class PbrMaterialTable {
  public:
    [[nodiscard]] bool contains(MaterialHandle material) const;

    [[nodiscard]] const PbrMaterialDefinition& definition(MaterialHandle material) const;
    [[nodiscard]] FrameUniformMaterialInstance<PbrMaterialUniforms>&
    emplace(MaterialHandle material, PbrMaterialDefinition definition,
            const cubey::vulkan::Device& device,
            const FrameUniformMaterialInstanceConfig& instance_config);

    [[nodiscard]] FrameUniformMaterialInstance<PbrMaterialUniforms>&
    instance(MaterialHandle material);
    [[nodiscard]] const FrameUniformMaterialInstance<PbrMaterialUniforms>&
    instance(MaterialHandle material) const;
    [[nodiscard]] VkDescriptorSetLayout descriptor_set_layout() const;
    [[nodiscard]] VkDescriptorSetLayout layout(MaterialHandle material) const;
    void rebind(MaterialHandle from, MaterialHandle to);
    void erase(MaterialHandle material);
    void clear();

  private:
    void register_descriptor_set_layout(VkDescriptorSetLayout layout);

    MaterialResourceTable<PbrMaterialRecord> records_{};
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
};

} // namespace cubey::render
