#pragma once

#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/pipeline.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::render {

enum class MaterialDomain : std::uint8_t {
    Surface3D,
};

enum class MaterialBlendMode : std::uint8_t {
    Opaque,
    // Premultiplied source-over blending; material inputs remain straight alpha.
    AlphaBlend,
};

enum class MaterialAlphaMode : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

enum class MaterialPassKind : std::uint8_t {
    DepthOnly,
    ForwardColor,
};

struct MaterialPassMask {
    std::uint32_t bits = 0;

    friend bool operator==(MaterialPassMask lhs, MaterialPassMask rhs) = default;
};

[[nodiscard]] constexpr MaterialPassMask material_pass_mask(MaterialPassKind kind) noexcept {
    return MaterialPassMask{.bits = 1U << static_cast<std::uint32_t>(kind)};
}

[[nodiscard]] constexpr MaterialPassMask operator|(MaterialPassMask lhs,
                                                   MaterialPassMask rhs) noexcept {
    return MaterialPassMask{.bits = lhs.bits | rhs.bits};
}

[[nodiscard]] constexpr MaterialPassMask default_material_pass_mask() noexcept {
    return material_pass_mask(MaterialPassKind::DepthOnly) |
           material_pass_mask(MaterialPassKind::ForwardColor);
}

struct MaterialInfo {
    std::string label{};
    MaterialDomain domain = MaterialDomain::Surface3D;
    MaterialAlphaMode alpha_mode = MaterialAlphaMode::Opaque;
    MaterialBlendMode blend = MaterialBlendMode::Opaque;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    std::uint32_t sort_key = 0;
    MaterialPassMask pass_mask = default_material_pass_mask();
};

[[nodiscard]] constexpr MaterialBlendMode
material_blend_mode_for_alpha_mode(MaterialAlphaMode mode) noexcept {
    switch (mode) {
    case MaterialAlphaMode::Blend:
        return MaterialBlendMode::AlphaBlend;
    case MaterialAlphaMode::Opaque:
    case MaterialAlphaMode::Mask:
    default:
        return MaterialBlendMode::Opaque;
    }
}

[[nodiscard]] constexpr MaterialPassMask
material_pass_mask_for_alpha_mode(MaterialAlphaMode mode) noexcept {
    switch (mode) {
    case MaterialAlphaMode::Blend:
        return material_pass_mask(MaterialPassKind::ForwardColor);
    case MaterialAlphaMode::Opaque:
    case MaterialAlphaMode::Mask:
    default:
        return default_material_pass_mask();
    }
}

struct MaterialDescriptorSetLayout {
    std::uint32_t set = 0;
    std::vector<cubey::vulkan::DescriptorSetBindingConfig> bindings{};
};

struct MaterialPassInfo {
    std::string label{};
    MaterialPassKind kind = MaterialPassKind::ForwardColor;
    std::vector<MaterialDescriptorSetLayout> descriptor_sets{};
    std::vector<VkPushConstantRange> push_constants{};
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::uint32_t patch_control_points = 0;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode = VK_CULL_MODE_NONE;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depth_test = false;
    bool depth_write = false;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
    bool blend_enable = false;
    VkBlendFactor src_color_blend_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp color_blend_op = VK_BLEND_OP_ADD;
    VkBlendFactor src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alpha_blend_op = VK_BLEND_OP_ADD;
};

[[nodiscard]] constexpr bool material_supports_pass(MaterialPassMask mask,
                                                    MaterialPassKind kind) noexcept {
    return (mask.bits & material_pass_mask(kind).bits) != 0;
}

[[nodiscard]] constexpr bool material_supports_pass(const MaterialInfo& material,
                                                    MaterialPassKind kind) noexcept {
    return material_supports_pass(material.pass_mask, kind);
}

[[nodiscard]] MaterialDescriptorSetLayout sampled_texture_descriptor_set_layout(
    std::uint32_t set, std::uint32_t binding_count = 1,
    VkShaderStageFlags stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT);
void validate_push_constant_ranges(std::span<const VkPushConstantRange> push_constants,
                                   std::uint32_t max_push_constant_bytes,
                                   std::string_view owner_label);
void validate_material_pass_info(const MaterialPassInfo& info);
[[nodiscard]] const MaterialDescriptorSetLayout&
material_descriptor_set_layout(const MaterialPassInfo& info, std::uint32_t set);
[[nodiscard]] cubey::vulkan::DescriptorSetInfo
material_descriptor_set_info(const MaterialPassInfo& info, std::uint32_t set,
                             std::uint32_t max_sets = 1);
void apply_material_pass_state(const MaterialPassInfo& info,
                               cubey::vulkan::DynamicGraphicsPipelineConfig& config);

} // namespace cubey::render
