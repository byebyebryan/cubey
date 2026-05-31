#pragma once

#include <cubey/core/math.h>
#include <cubey/render/material.h>
#include <cubey/render/primitive_mesh.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cubey::render {

struct PbrVertex {
    math::Vec3 position{};
    math::Vec3 normal{};
    math::Vec4 tangent{};
    math::Vec2 uv0{};
    math::Vec2 uv1{};
    math::Vec4 color0{1.0F, 1.0F, 1.0F, 1.0F};
};

enum class PbrTonemap : std::uint32_t {
    Linear = 0,
    Aces = 1,
};

enum class PbrOutputEncoding : std::uint32_t {
    Linear = 0,
    Srgb = 1,
};

enum class PbrDebugView : std::uint32_t {
    Final = 0,
    BaseColor = 1,
    Normal = 2,
    GeometricNormal = 3,
    Roughness = 4,
    Metallic = 5,
    Occlusion = 6,
    Emissive = 7,
    Shadow = 8,
    Alpha = 9,
    Uv0 = 10,
};

enum class PbrMaterialTextureFlag : std::uint32_t {
    Specular = 1U << 0U,
    SpecularColor = 1U << 1U,
    Clearcoat = 1U << 2U,
    ClearcoatRoughness = 1U << 3U,
    ClearcoatNormal = 1U << 4U,
    SheenColor = 1U << 5U,
    SheenRoughness = 1U << 6U,
    Anisotropy = 1U << 7U,
    Iridescence = 1U << 8U,
    IridescenceThickness = 1U << 9U,
};

[[nodiscard]] constexpr std::uint32_t
pbr_material_texture_flag(PbrMaterialTextureFlag flag) noexcept {
    return static_cast<std::uint32_t>(flag);
}

struct PbrDisplayTransform {
    float exposure = 0.0F;
    PbrTonemap tonemap = PbrTonemap::Aces;
    PbrOutputEncoding output_encoding = PbrOutputEncoding::Linear;
};

struct PbrTextureTransform {
    math::Vec4 offset_scale{0.0F, 0.0F, 1.0F, 1.0F};
    math::Vec4 rotation_texcoord{1.0F, 0.0F, 0.0F, 0.0F};
};

struct PbrMaterialTextureTransforms {
    PbrTextureTransform base_color{};
    PbrTextureTransform metallic_roughness{};
    PbrTextureTransform normal{};
    PbrTextureTransform occlusion{};
    PbrTextureTransform emissive{};
    PbrTextureTransform specular{};
    PbrTextureTransform specular_color{};
    PbrTextureTransform clearcoat{};
    PbrTextureTransform clearcoat_roughness{};
    PbrTextureTransform clearcoat_normal{};
    PbrTextureTransform sheen_color{};
    PbrTextureTransform sheen_roughness{};
    PbrTextureTransform anisotropy{};
    PbrTextureTransform iridescence{};
    PbrTextureTransform iridescence_thickness{};
};

struct PbrSceneUniforms {
    math::Mat4 view_projection{1.0F};
    math::Mat4 light_view_projection{1.0F};
    math::Vec4 camera_position{0.0F, 0.0F, 0.0F, 1.0F};
    math::Vec4 light_direction{0.0F, -1.0F, 0.0F, 0.0F};
    math::Vec4 light_color_intensity{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 ambient_color_intensity{0.03F, 0.03F, 0.03F, 1.0F};
    math::Vec4 environment_intensity_mip_count{1.0F, 1.0F, 0.0F, 0.0F};
    math::Vec4 display_transform{0.0F, 1.0F, 0.0F, 0.0F};
    math::Vec4 debug_options{0.0F, 0.0F, 0.0F, 0.0F};
    std::array<math::Vec4, 9> diffuse_irradiance_sh{};
    math::Vec4 environment_options{0.0F, 0.0F, 0.0F, 0.0F};
};

struct PbrSkyboxUniforms {
    math::Mat4 inverse_view_projection{1.0F};
    math::Vec4 camera_position{0.0F, 0.0F, 0.0F, 1.0F};
    math::Vec4 environment_rotation_intensity{1.0F, 0.0F, 1.0F, 0.0F};
    math::Vec4 display_transform{0.0F, 1.0F, 0.0F, 0.0F};
};

struct PbrPostUniforms {
    math::Vec4 display_transform{0.0F, 1.0F, 0.0F, 0.0F};
};

struct PbrMaterialFactors {
    math::Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec3 emissive_factor{0.0F, 0.0F, 0.0F};
    float alpha_cutoff = 0.0F;
    MaterialAlphaMode alpha_mode = MaterialAlphaMode::Opaque;
    float metallic_factor = 1.0F;
    float roughness_factor = 1.0F;
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
    math::Vec3 specular_color_factor{1.0F, 1.0F, 1.0F};
    float specular_factor = 1.0F;
    float reflectance = 0.5F;
    float clearcoat_factor = 0.0F;
    float clearcoat_roughness_factor = 0.0F;
    float clearcoat_normal_scale = 1.0F;
    math::Vec3 sheen_color_factor{0.0F, 0.0F, 0.0F};
    float sheen_roughness_factor = 0.0F;
    float anisotropy_strength = 0.0F;
    float anisotropy_rotation = 0.0F;
    float iridescence_factor = 0.0F;
    float iridescence_ior = 1.3F;
    float iridescence_thickness_minimum = 100.0F;
    float iridescence_thickness_maximum = 400.0F;
    bool unlit = false;
    std::uint32_t texture_flags = 0U;
    PbrMaterialTextureTransforms texture_transforms{};
};

struct PbrMaterialUniforms {
    math::Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 emissive_alpha_cutoff{0.0F, 0.0F, 0.0F, 0.0F};
    math::Vec4 metallic_roughness_normal_occlusion{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 specular_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 material_model{0.5F, 0.0F, 0.0F, 0.0F};
    math::Vec4 clearcoat_factor_roughness_normal{0.0F, 0.0F, 1.0F, 0.0F};
    math::Vec4 sheen_color_roughness{0.0F, 0.0F, 0.0F, 0.0F};
    math::Vec4 anisotropy_iridescence{0.0F, 1.0F, 0.0F, 0.0F};
    math::Vec4 iridescence_ior_thickness{1.3F, 100.0F, 400.0F, 0.0F};
    PbrMaterialTextureTransforms texture_transforms{};
};

struct PbrPushConstants {
    math::Mat4 model{1.0F};
};

static_assert(sizeof(PbrTextureTransform) == sizeof(math::Vec4) * 2U);
static_assert(sizeof(PbrMaterialTextureTransforms) == sizeof(math::Vec4) * 30U);
static_assert(sizeof(PbrVertex) == sizeof(float) * 18U);
static_assert(sizeof(PbrSceneUniforms) == (sizeof(math::Mat4) * 2U) + (sizeof(math::Vec4) * 17U));
static_assert(sizeof(PbrMaterialUniforms) == sizeof(math::Vec4) * 39U);
static_assert(sizeof(PbrSkyboxUniforms) == sizeof(math::Mat4) + (sizeof(math::Vec4) * 3U));
static_assert(sizeof(PbrPostUniforms) == sizeof(math::Vec4));
static_assert(sizeof(PbrPushConstants) == sizeof(math::Mat4));
static_assert(sizeof(PbrPushConstants) <= 128U);

enum class PbrSceneBinding : std::uint32_t {
    SceneUniforms = 0,
    ShadowMap = 1,
    IrradianceCube = 2,
    PrefilteredCube = 3,
    BrdfLut = 4,
};

enum class PbrMaterialBinding : std::uint32_t {
    BaseColor = 0,
    MetallicRoughness = 1,
    Normal = 2,
    Occlusion = 3,
    Emissive = 4,
    Specular = 5,
    SpecularColor = 6,
    Uniforms = 7,
    Clearcoat = 8,
    ClearcoatRoughness = 9,
    ClearcoatNormal = 10,
    SheenColor = 11,
    SheenRoughness = 12,
    Anisotropy = 13,
    Iridescence = 14,
    IridescenceThickness = 15,
};

enum class PbrSkyboxBinding : std::uint32_t {
    SkyboxUniforms = 0,
    EnvironmentCube = 1,
};

enum class PbrPostBinding : std::uint32_t {
    PostUniforms = 0,
    SceneColor = 1,
};

struct PbrForwardPassConfig {
    MaterialBlendMode blend = MaterialBlendMode::Opaque;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    std::string label{};
};

[[nodiscard]] VertexInputLayout pbr_vertex_input_layout();
[[nodiscard]] MaterialPassInfo pbr_forward_pass_info();
[[nodiscard]] MaterialPassInfo pbr_forward_pass_info(const PbrForwardPassConfig& config);
[[nodiscard]] MaterialPassInfo pbr_skybox_pass_info();
[[nodiscard]] MaterialPassInfo pbr_post_pass_info();
[[nodiscard]] std::string_view pbr_debug_view_name(PbrDebugView view);
[[nodiscard]] PbrDebugView pbr_debug_view_from_name(std::string_view name);
[[nodiscard]] PbrDebugView next_pbr_debug_view(PbrDebugView view);
[[nodiscard]] PbrDisplayTransform
pbr_display_transform_for_target(VkFormat target_format, float exposure = 0.0F,
                                 PbrTonemap tonemap = PbrTonemap::Aces);
[[nodiscard]] math::Vec4 pbr_display_transform_uniform(const PbrDisplayTransform& transform);
[[nodiscard]] float pbr_f0_from_reflectance(float reflectance);
[[nodiscard]] float pbr_reflectance_from_ior(float ior);
[[nodiscard]] PbrMaterialUniforms pbr_material_uniforms(const PbrMaterialFactors& factors,
                                                        MaterialAlphaMode alpha_mode);
[[nodiscard]] PbrMaterialUniforms pbr_material_uniforms(const PbrMaterialFactors& factors);
[[nodiscard]] PbrPushConstants pbr_push_constants(math::Mat4 model);

} // namespace cubey::render
