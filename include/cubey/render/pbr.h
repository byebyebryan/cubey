#pragma once

#include <cubey/core/math.h>
#include <cubey/render/material.h>
#include <cubey/render/primitive_mesh.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace cubey::render {

struct PbrVertex {
    math::Vec3 position{};
    math::Vec3 normal{};
    math::Vec4 tangent{};
    math::Vec2 uv0{};
};

struct PbrSceneUniforms {
    math::Mat4 view_projection{1.0F};
    math::Mat4 light_view_projection{1.0F};
    math::Vec4 camera_position{0.0F, 0.0F, 0.0F, 1.0F};
    math::Vec4 light_direction{0.0F, -1.0F, 0.0F, 0.0F};
    math::Vec4 light_color_intensity{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 ambient_color_intensity{0.03F, 0.03F, 0.03F, 1.0F};
};

struct PbrMaterialFactors {
    math::Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec3 emissive_factor{0.0F, 0.0F, 0.0F};
    float alpha_cutoff = 0.0F;
    float metallic_factor = 1.0F;
    float roughness_factor = 1.0F;
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
};

struct PbrPushConstants {
    math::Mat4 model{1.0F};
    math::Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 emissive_alpha_cutoff{0.0F, 0.0F, 0.0F, 0.0F};
    math::Vec4 metallic_roughness_normal_occlusion{1.0F, 1.0F, 1.0F, 1.0F};
};

static_assert(sizeof(PbrPushConstants) == sizeof(math::Mat4) + sizeof(math::Vec4) * 3U);
static_assert(sizeof(PbrPushConstants) <= 128U);

enum class PbrSceneBinding : std::uint32_t {
    SceneUniforms = 0,
    ShadowMap = 1,
};

enum class PbrMaterialBinding : std::uint32_t {
    BaseColor = 0,
    MetallicRoughness = 1,
    Normal = 2,
    Occlusion = 3,
    Emissive = 4,
};

struct PbrForwardPassConfig {
    MaterialBlendMode blend = MaterialBlendMode::Opaque;
    std::string label{};
};

[[nodiscard]] VertexInputLayout pbr_vertex_input_layout();
[[nodiscard]] MaterialPassInfo pbr_forward_pass_info();
[[nodiscard]] MaterialPassInfo pbr_forward_pass_info(const PbrForwardPassConfig& config);
[[nodiscard]] PbrPushConstants pbr_push_constants(math::Mat4 model,
                                                  const PbrMaterialFactors& factors);

} // namespace cubey::render
