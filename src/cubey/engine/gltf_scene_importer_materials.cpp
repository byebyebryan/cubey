#include "gltf_scene_importer_internal.h"

#include <cubey/engine/engine.h>
#include <cubey/render/material.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/sampler.h>

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cubey {
namespace {

struct TextureBinding {
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct TextureCacheKey {
    std::uint32_t texture_index = asset::kInvalidAssetIndex;
    asset::GltfTextureColorSpace color_space = asset::GltfTextureColorSpace::Linear;

    friend bool operator==(TextureCacheKey lhs, TextureCacheKey rhs) = default;
};

struct TextureCacheKeyHash {
    [[nodiscard]] std::size_t operator()(TextureCacheKey key) const noexcept {
        return (static_cast<std::size_t>(key.texture_index) << 1U) ^
               static_cast<std::size_t>(key.color_space);
    }
};

using TextureCache = std::unordered_map<TextureCacheKey, std::size_t, TextureCacheKeyHash>;

[[nodiscard]] VkFilter to_vk_filter(asset::GltfTextureFilter filter) {
    switch (filter) {
    case asset::GltfTextureFilter::Nearest:
        return VK_FILTER_NEAREST;
    case asset::GltfTextureFilter::Linear:
        return VK_FILTER_LINEAR;
    }
    return VK_FILTER_LINEAR;
}

[[nodiscard]] VkSamplerAddressMode to_vk_address_mode(asset::GltfTextureWrap wrap) {
    switch (wrap) {
    case asset::GltfTextureWrap::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case asset::GltfTextureWrap::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case asset::GltfTextureWrap::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

[[nodiscard]] VkFormat image_format_for_color_space(asset::GltfTextureColorSpace color_space) {
    switch (color_space) {
    case asset::GltfTextureColorSpace::Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case asset::GltfTextureColorSpace::Linear:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

[[nodiscard]] render::MaterialAlphaMode gltf_alpha_mode(asset::GltfAlphaMode mode) {
    switch (mode) {
    case asset::GltfAlphaMode::Mask:
        return render::MaterialAlphaMode::Mask;
    case asset::GltfAlphaMode::Blend:
        return render::MaterialAlphaMode::Blend;
    case asset::GltfAlphaMode::Opaque:
    default:
        return render::MaterialAlphaMode::Opaque;
    }
}

[[nodiscard]] render::Texture2D create_solid_texture(const vulkan::Device& device,
                                                     vulkan::GpuRuntime& gpu,
                                                     std::array<std::uint8_t, 4> color,
                                                     VkFormat format) {
    return render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {1, 1},
            .format = format,
            .rgba8 = std::span<const std::uint8_t>{color.data(), color.size()},
            .create_sampler = true,
            .sampler = {},
        });
}

[[nodiscard]] const render::Texture2D& default_texture(const GltfSceneImportResources& resources,
                                                       render::PbrMaterialBinding binding) {
    const std::optional<render::Texture2D>* texture = nullptr;
    switch (binding) {
    case render::PbrMaterialBinding::BaseColor:
        texture = &resources.base_color_default;
        break;
    case render::PbrMaterialBinding::MetallicRoughness:
        texture = &resources.metallic_roughness_default;
        break;
    case render::PbrMaterialBinding::Normal:
        texture = &resources.normal_default;
        break;
    case render::PbrMaterialBinding::Occlusion:
        texture = &resources.occlusion_default;
        break;
    case render::PbrMaterialBinding::Emissive:
        texture = &resources.emissive_default;
        break;
    case render::PbrMaterialBinding::Uniforms:
        break;
    }
    if (texture == nullptr || !texture->has_value()) {
        throw std::runtime_error("default PBR texture is not initialized");
    }
    return texture->value();
}

[[nodiscard]] TextureBinding texture_binding(const render::Texture2D& texture) {
    return {
        .sampler = texture.sampler().handle(),
        .view = texture.view(),
    };
}

[[nodiscard]] vulkan::SamplerConfig sampler_config_for_texture(const asset::GltfAsset& asset,
                                                               const asset::GltfTexture& texture) {
    if (texture.sampler_index >= asset.samplers.size()) {
        return {};
    }
    const asset::GltfSampler& sampler = asset.samplers[texture.sampler_index];
    return {
        .min_filter = to_vk_filter(sampler.min_filter),
        .mag_filter = to_vk_filter(sampler.mag_filter),
        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .address_mode_u = to_vk_address_mode(sampler.wrap_s),
        .address_mode_v = to_vk_address_mode(sampler.wrap_t),
        .address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };
}

[[nodiscard]] TextureBinding
texture_binding_for_ref(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                        GltfSceneImportResources& resources, const asset::GltfAsset& asset,
                        const asset::GltfTextureRef& ref, asset::GltfTextureColorSpace color_space,
                        render::PbrMaterialBinding fallback_slot, TextureCache& texture_cache) {
    if (!ref.has_value()) {
        return texture_binding(default_texture(resources, fallback_slot));
    }
    if (ref.texture_index >= asset.textures.size()) {
        throw std::runtime_error("glTF material texture index is out of range");
    }
    const TextureCacheKey cache_key{
        .texture_index = ref.texture_index,
        .color_space = color_space,
    };
    const auto cached = texture_cache.find(cache_key);
    if (cached != texture_cache.end()) {
        return texture_binding(resources.textures.at(cached->second));
    }

    const asset::GltfTexture& texture = asset.textures[ref.texture_index];
    if (texture.image_index >= asset.images.size()) {
        throw std::runtime_error("glTF texture image index is out of range");
    }
    const asset::GltfImage& image = asset.images[texture.image_index];
    render::Texture2D uploaded = render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {image.width, image.height},
            .format = image_format_for_color_space(color_space),
            .rgba8 = std::span<const std::uint8_t>{image.rgba8.data(), image.rgba8.size()},
            .create_sampler = true,
            .sampler = sampler_config_for_texture(asset, texture),
        });
    resources.textures.push_back(std::move(uploaded));
    texture_cache.emplace(cache_key, resources.textures.size() - 1U);
    return texture_binding(resources.textures.back());
}

[[nodiscard]] std::vector<render::SampledImageMaterialBinding>
material_sampled_image_bindings(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                GltfSceneImportResources& resources, const asset::GltfAsset& asset,
                                const asset::GltfMaterial& source, TextureCache& texture_cache) {
    const TextureBinding base_color =
        texture_binding_for_ref(device, gpu, resources, asset, source.base_color_texture,
                                asset::gltf_texture_color_space_for_base_color(),
                                render::PbrMaterialBinding::BaseColor, texture_cache);
    const TextureBinding metallic_roughness =
        texture_binding_for_ref(device, gpu, resources, asset, source.metallic_roughness_texture,
                                asset::GltfTextureColorSpace::Linear,
                                render::PbrMaterialBinding::MetallicRoughness, texture_cache);
    const TextureBinding normal = texture_binding_for_ref(
        device, gpu, resources, asset, source.normal_texture, asset::GltfTextureColorSpace::Linear,
        render::PbrMaterialBinding::Normal, texture_cache);
    const TextureBinding occlusion = texture_binding_for_ref(
        device, gpu, resources, asset, source.occlusion_texture,
        asset::GltfTextureColorSpace::Linear, render::PbrMaterialBinding::Occlusion, texture_cache);
    const TextureBinding emissive = texture_binding_for_ref(
        device, gpu, resources, asset, source.emissive_texture, asset::GltfTextureColorSpace::Srgb,
        render::PbrMaterialBinding::Emissive, texture_cache);

    return {
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::BaseColor),
            .sampler = base_color.sampler,
            .image_view = base_color.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::MetallicRoughness),
            .sampler = metallic_roughness.sampler,
            .image_view = metallic_roughness.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Normal),
            .sampler = normal.sampler,
            .image_view = normal.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Occlusion),
            .sampler = occlusion.sampler,
            .image_view = occlusion.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Emissive),
            .sampler = emissive.sampler,
            .image_view = emissive.view,
        },
    };
}

[[nodiscard]] std::string import_label(const GltfSceneImportConfig& config, const char* kind,
                                       std::size_t index) {
    return config.label_prefix + "." + kind + "." + std::to_string(index);
}

} // namespace

void create_default_textures(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                             GltfSceneImportResources& resources) {
    if (resources.base_color_default.has_value()) {
        return;
    }
    resources.base_color_default.emplace(
        create_solid_texture(device, gpu, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_SRGB));
    resources.metallic_roughness_default.emplace(
        create_solid_texture(device, gpu, {255, 255, 0, 255}, VK_FORMAT_R8G8B8A8_UNORM));
    resources.normal_default.emplace(
        create_solid_texture(device, gpu, {128, 128, 255, 255}, VK_FORMAT_R8G8B8A8_UNORM));
    resources.occlusion_default.emplace(
        create_solid_texture(device, gpu, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_UNORM));
    resources.emissive_default.emplace(
        create_solid_texture(device, gpu, {0, 0, 0, 255}, VK_FORMAT_R8G8B8A8_SRGB));
}

void create_material_resources(Engine& engine, const vulkan::Device& device,
                               vulkan::GpuRuntime& gpu, GltfSceneImportResources& resources,
                               GltfSceneImportResult& result, const asset::GltfAsset& asset,
                               const GltfSceneImportConfig& config) {
    const render::MaterialPassInfo pass = render::pbr_forward_pass_info();
    TextureCache texture_cache;
    result.material_handles.reserve(asset.materials.size());
    for (std::size_t index = 0; index < asset.materials.size(); ++index) {
        const asset::GltfMaterial& source = asset.materials[index];
        const render::MaterialAlphaMode alpha_mode = gltf_alpha_mode(source.alpha_mode);
        const std::string label =
            source.label.empty() ? import_label(config, "material", index) : source.label;
        const render::MaterialHandle material =
            engine.render_resources().create_material(render::MaterialInfo{
                .label = label,
                .alpha_mode = gltf_alpha_mode(source.alpha_mode),
                .blend = render::material_blend_mode_for_alpha_mode(alpha_mode),
                .sort_key = static_cast<std::uint32_t>(index),
                .pass_mask = render::material_pass_mask_for_alpha_mode(alpha_mode),
            });
        result.material_handles.push_back(material);
        resources.material_factors.emplace(
            material,
            render::PbrMaterialFactors{
                .base_color_factor = source.base_color_factor,
                .emissive_factor = source.emissive_factor,
                .alpha_cutoff =
                    source.alpha_mode == asset::GltfAlphaMode::Mask ? source.alpha_cutoff : 0.0F,
                .alpha_mode = alpha_mode,
                .metallic_factor = source.metallic_factor,
                .roughness_factor = source.roughness_factor,
                .normal_scale = source.normal_scale,
                .occlusion_strength = source.occlusion_strength,
                .specular_color_factor = source.specular_color_factor,
                .specular_factor = source.specular_factor,
                .reflectance = source.reflectance,
            });
        resources.material_instances.emplace(
            material, device,
            render::FrameUniformMaterialInstanceConfig{
                .material_pass = pass,
                .descriptor_set = 1,
                .frame_slot_count = config.frame_slot_count,
                .uniform_binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Uniforms),
                .sampled_images = material_sampled_image_bindings(device, gpu, resources, asset,
                                                                  source, texture_cache),
            });
    }
    if (result.material_handles.empty()) {
        throw std::runtime_error("glTF scene import requires at least one material");
    }
    result.first_material_handle = result.material_handles.front();
}

} // namespace cubey
