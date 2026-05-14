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

enum class PbrTonemap : std::uint32_t {
    Linear = 0,
    Aces = 1,
};

enum class PbrOutputEncoding : std::uint32_t {
    Linear = 0,
    Srgb = 1,
};

struct PbrDisplayTransform {
    float exposure = 0.0F;
    PbrTonemap tonemap = PbrTonemap::Aces;
    PbrOutputEncoding output_encoding = PbrOutputEncoding::Linear;
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
    bool unlit = false;
};

struct PbrMaterialUniforms {
    math::Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 emissive_alpha_cutoff{0.0F, 0.0F, 0.0F, 0.0F};
    math::Vec4 metallic_roughness_normal_occlusion{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 specular_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    math::Vec4 material_model{0.5F, 0.0F, 0.0F, 0.0F};
};

struct PbrPushConstants {
    math::Mat4 model{1.0F};
};

static_assert(sizeof(PbrMaterialUniforms) == sizeof(math::Vec4) * 5U);
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
    Uniforms = 5,
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
    std::string label{};
};

[[nodiscard]] VertexInputLayout pbr_vertex_input_layout();
[[nodiscard]] MaterialPassInfo pbr_forward_pass_info();
[[nodiscard]] MaterialPassInfo pbr_forward_pass_info(const PbrForwardPassConfig& config);
[[nodiscard]] MaterialPassInfo pbr_skybox_pass_info();
[[nodiscard]] MaterialPassInfo pbr_post_pass_info();
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
