#pragma once

#include <cubey/core/math.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace cubey::asset {

inline constexpr std::uint32_t kInvalidAssetIndex = std::numeric_limits<std::uint32_t>::max();

enum class GltfAlphaMode : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

enum class GltfTextureFilter : std::uint8_t {
    Nearest,
    Linear,
};

enum class GltfTextureWrap : std::uint8_t {
    Repeat,
    ClampToEdge,
    MirroredRepeat,
};

enum class GltfTextureColorSpace : std::uint8_t {
    Linear,
    Srgb,
};

enum class GltfAnimationInterpolation : std::uint8_t {
    Step,
    Linear,
    CubicSpline,
};

enum class GltfAnimationTargetPath : std::uint8_t {
    Translation,
    Rotation,
    Scale,
    Weights,
};

struct GltfTextureRef {
    std::uint32_t texture_index = kInvalidAssetIndex;
    std::uint32_t texcoord = 0;

    [[nodiscard]] bool has_value() const noexcept {
        return texture_index != kInvalidAssetIndex;
    }
};

struct GltfSampler {
    std::string label{};
    GltfTextureFilter min_filter = GltfTextureFilter::Linear;
    GltfTextureFilter mag_filter = GltfTextureFilter::Linear;
    GltfTextureWrap wrap_s = GltfTextureWrap::Repeat;
    GltfTextureWrap wrap_t = GltfTextureWrap::Repeat;
};

struct GltfImage {
    std::string label{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8{};
};

struct GltfTexture {
    std::string label{};
    std::uint32_t image_index = kInvalidAssetIndex;
    std::uint32_t sampler_index = kInvalidAssetIndex;
};

struct GltfMaterial {
    std::string label{};
    math::Vec4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
    float metallic_factor = 1.0F;
    float roughness_factor = 1.0F;
    math::Vec3 specular_color_factor{1.0F, 1.0F, 1.0F};
    float specular_factor = 1.0F;
    float reflectance = 0.5F;
    math::Vec3 emissive_factor{0.0F, 0.0F, 0.0F};
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
    GltfTextureRef base_color_texture{};
    GltfTextureRef metallic_roughness_texture{};
    GltfTextureRef normal_texture{};
    GltfTextureRef occlusion_texture{};
    GltfTextureRef emissive_texture{};
    GltfAlphaMode alpha_mode = GltfAlphaMode::Opaque;
    float alpha_cutoff = 0.5F;
    bool double_sided = false;
    bool unlit = false;
};

struct GltfVertex {
    math::Vec3 position{0.0F, 0.0F, 0.0F};
    math::Vec3 normal{0.0F, 1.0F, 0.0F};
    math::Vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
    math::Vec2 texcoord0{0.0F, 0.0F};
    std::array<std::uint16_t, 4> joints0{0, 0, 0, 0};
    math::Vec4 weights0{0.0F, 0.0F, 0.0F, 0.0F};
};

struct GltfBounds3D {
    math::Vec3 center{0.0F, 0.0F, 0.0F};
    math::Vec3 half_extent{0.0F, 0.0F, 0.0F};
};

struct GltfMorphTarget {
    std::string label{};
    std::vector<math::Vec3> position_deltas{};
    std::vector<math::Vec3> normal_deltas{};
    std::vector<math::Vec3> tangent_deltas{};
};

struct GltfMeshPrimitive {
    std::vector<GltfVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    std::vector<GltfMorphTarget> morph_targets{};
    std::uint32_t material_index = 0;
    GltfBounds3D local_bounds{};
};

struct GltfMesh {
    std::string label{};
    std::vector<GltfMeshPrimitive> primitives{};
    std::vector<float> weights{};
};

struct GltfNode {
    std::string label{};
    math::Vec3 translation{0.0F, 0.0F, 0.0F};
    math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
    math::Mat4 local_matrix{1.0F};
    std::uint32_t mesh_index = kInvalidAssetIndex;
    std::uint32_t skin_index = kInvalidAssetIndex;
    std::vector<float> weights{};
    std::vector<std::uint32_t> children{};
};

struct GltfSkin {
    std::string label{};
    std::uint32_t skeleton_node_index = kInvalidAssetIndex;
    std::vector<std::uint32_t> joints{};
    std::vector<math::Mat4> inverse_bind_matrices{};
};

struct GltfAnimationSampler {
    GltfAnimationInterpolation interpolation = GltfAnimationInterpolation::Linear;
    std::vector<float> input_times{};
    std::vector<float> output_values{};
    std::uint32_t component_count = 0;
};

struct GltfAnimationChannel {
    std::uint32_t sampler_index = kInvalidAssetIndex;
    std::uint32_t node_index = kInvalidAssetIndex;
    GltfAnimationTargetPath target_path = GltfAnimationTargetPath::Translation;
};

struct GltfAnimation {
    std::string label{};
    std::vector<GltfAnimationSampler> samplers{};
    std::vector<GltfAnimationChannel> channels{};
    float duration_seconds = 0.0F;
};

struct GltfScene {
    std::string label{};
    std::vector<std::uint32_t> root_nodes{};
};

struct GltfAsset {
    std::filesystem::path source_path{};
    std::vector<GltfScene> scenes{};
    std::uint32_t default_scene = 0;
    std::vector<GltfNode> nodes{};
    std::vector<GltfMesh> meshes{};
    std::vector<GltfSkin> skins{};
    std::vector<GltfAnimation> animations{};
    std::vector<GltfMaterial> materials{};
    std::vector<GltfTexture> textures{};
    std::vector<GltfSampler> samplers{};
    std::vector<GltfImage> images{};
};

struct GltfLoadConfig {
    bool generate_missing_tangents = true;
    bool generate_missing_normals = true;
};

[[nodiscard]] GltfAsset load_gltf_asset(const std::filesystem::path& path,
                                        GltfLoadConfig config = {});
[[nodiscard]] const char* gltf_alpha_mode_name(GltfAlphaMode mode) noexcept;
[[nodiscard]] GltfTextureColorSpace gltf_texture_color_space_for_base_color() noexcept;
[[nodiscard]] GltfTextureColorSpace gltf_texture_color_space_for_material_slot(
    const GltfTextureRef& texture, GltfTextureColorSpace default_space) noexcept;

} // namespace cubey::asset
