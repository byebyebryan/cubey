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
#include <unordered_map>
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
};

[[nodiscard]] std::span<const PbrMaterialBinding> pbr_sampled_material_bindings() noexcept;
[[nodiscard]] std::span<const PbrDefaultTextureSpec> pbr_default_texture_specs() noexcept;
[[nodiscard]] PbrDefaultTextureSet
create_pbr_default_texture_set(const cubey::vulkan::Device& device,
                               cubey::vulkan::GpuRuntime& gpu);
[[nodiscard]] const Texture2D& pbr_default_texture(const PbrDefaultTextureSet& set,
                                                   PbrMaterialBinding binding);
[[nodiscard]] std::vector<SampledImageMaterialBinding>
pbr_default_sampled_image_bindings(const PbrDefaultTextureSet& set);

class PbrMaterialTable {
  public:
    [[nodiscard]] bool contains_factors(MaterialHandle material) const;
    [[nodiscard]] bool contains_instance(MaterialHandle material) const;
    [[nodiscard]] bool contains(MaterialHandle material) const;

    void set_factors(MaterialHandle material, const PbrMaterialFactors& factors);
    [[nodiscard]] PbrMaterialFactors& factors(MaterialHandle material);
    [[nodiscard]] const PbrMaterialFactors& factors(MaterialHandle material) const;

    template <typename... Args>
    FrameUniformMaterialInstance<PbrMaterialUniforms>& emplace_instance(MaterialHandle material,
                                                                        Args&&... args) {
        return instances_.emplace(material, std::forward<Args>(args)...);
    }

    [[nodiscard]] FrameUniformMaterialInstance<PbrMaterialUniforms>& instance(
        MaterialHandle material);
    [[nodiscard]] const FrameUniformMaterialInstance<PbrMaterialUniforms>& instance(
        MaterialHandle material) const;
    [[nodiscard]] VkDescriptorSetLayout layout(MaterialHandle material) const;
    void upload(MaterialHandle material, FrameSlot frame_slot) const;
    void upload(MaterialHandle material, FrameSlot frame_slot, MaterialAlphaMode alpha_mode) const;
    void erase(MaterialHandle material);
    void clear();

  private:
    MaterialResourceTable<FrameUniformMaterialInstance<PbrMaterialUniforms>> instances_{};
    std::unordered_map<MaterialHandle, PbrMaterialFactors, MaterialHandleHash> factors_{};
};

} // namespace cubey::render
