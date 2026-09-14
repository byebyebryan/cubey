#include "gltf_scene_importer_internal.h"

#include "gltf_basisu_texture.h"

#include <cubey/render/material.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/sampler.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cubey {
namespace {

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

using TextureCache = std::unordered_map<TextureCacheKey, std::uint32_t, TextureCacheKeyHash>;

[[nodiscard]] VkFilter to_vk_filter(asset::GltfTextureFilter filter) {
    switch (filter) {
    case asset::GltfTextureFilter::Nearest:
        return VK_FILTER_NEAREST;
    case asset::GltfTextureFilter::Linear:
        return VK_FILTER_LINEAR;
    }
    return VK_FILTER_LINEAR;
}

[[nodiscard]] VkSamplerMipmapMode to_vk_mipmap_mode(asset::GltfTextureMipFilter filter) {
    switch (filter) {
    case asset::GltfTextureMipFilter::None:
    case asset::GltfTextureMipFilter::Nearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case asset::GltfTextureMipFilter::Linear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
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
    return color_space == asset::GltfTextureColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB
                                                             : VK_FORMAT_R8G8B8A8_UNORM;
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

[[nodiscard]] render::PbrTextureTransform pbr_texture_transform(const asset::GltfTextureRef& ref) {
    return {
        .offset_scale = {ref.offset.x, ref.offset.y, ref.scale.x, ref.scale.y},
        .rotation_texcoord = {std::cos(ref.rotation), std::sin(ref.rotation),
                              static_cast<float>(ref.texcoord), 0.0F},
    };
}

[[nodiscard]] render::PbrMaterialTextureTransforms
pbr_texture_transforms(const asset::GltfMaterial& material) {
    return {
        .base_color = pbr_texture_transform(material.base_color_texture),
        .metallic_roughness = pbr_texture_transform(material.metallic_roughness_texture),
        .normal = pbr_texture_transform(material.normal_texture),
        .occlusion = pbr_texture_transform(material.occlusion_texture),
        .emissive = pbr_texture_transform(material.emissive_texture),
        .specular = pbr_texture_transform(material.specular_texture),
        .specular_color = pbr_texture_transform(material.specular_color_texture),
        .clearcoat = pbr_texture_transform(material.clearcoat_texture),
        .clearcoat_roughness = pbr_texture_transform(material.clearcoat_roughness_texture),
        .clearcoat_normal = pbr_texture_transform(material.clearcoat_normal_texture),
        .sheen_color = pbr_texture_transform(material.sheen_color_texture),
        .sheen_roughness = pbr_texture_transform(material.sheen_roughness_texture),
        .anisotropy = pbr_texture_transform(material.anisotropy_texture),
        .iridescence = pbr_texture_transform(material.iridescence_texture),
        .iridescence_thickness = pbr_texture_transform(material.iridescence_thickness_texture),
        .transmission = pbr_texture_transform(material.transmission_texture),
        .volume_thickness = pbr_texture_transform(material.volume_thickness_texture),
    };
}

[[nodiscard]] std::uint32_t pbr_texture_flags(const asset::GltfMaterial& material) {
    std::uint32_t flags = 0U;
    const auto set = [&flags](bool present, render::PbrMaterialTextureFlag flag) {
        if (present) {
            flags |= render::pbr_material_texture_flag(flag);
        }
    };
    set(material.specular_texture.has_value(), render::PbrMaterialTextureFlag::Specular);
    set(material.specular_color_texture.has_value(), render::PbrMaterialTextureFlag::SpecularColor);
    set(material.clearcoat_texture.has_value(), render::PbrMaterialTextureFlag::Clearcoat);
    set(material.clearcoat_roughness_texture.has_value(),
        render::PbrMaterialTextureFlag::ClearcoatRoughness);
    set(material.clearcoat_normal_texture.has_value(),
        render::PbrMaterialTextureFlag::ClearcoatNormal);
    set(material.sheen_color_texture.has_value(), render::PbrMaterialTextureFlag::SheenColor);
    set(material.sheen_roughness_texture.has_value(),
        render::PbrMaterialTextureFlag::SheenRoughness);
    set(material.anisotropy_texture.has_value(), render::PbrMaterialTextureFlag::Anisotropy);
    set(material.iridescence_texture.has_value(), render::PbrMaterialTextureFlag::Iridescence);
    set(material.iridescence_thickness_texture.has_value(),
        render::PbrMaterialTextureFlag::IridescenceThickness);
    set(material.transmission_texture.has_value(), render::PbrMaterialTextureFlag::Transmission);
    set(material.volume_thickness_texture.has_value(),
        render::PbrMaterialTextureFlag::VolumeThickness);
    return flags;
}

[[nodiscard]] vulkan::SamplerConfig sampler_config_for_texture(const asset::GltfAsset& asset,
                                                               const asset::GltfTexture& texture,
                                                               std::uint32_t mip_levels = 1) {
    const float max_lod = static_cast<float>(mip_levels > 0 ? mip_levels - 1U : 0U);
    if (texture.sampler_index >= asset.samplers.size()) {
        return {.max_lod = max_lod};
    }
    const asset::GltfSampler& sampler = asset.samplers[texture.sampler_index];
    return {
        .min_filter = to_vk_filter(sampler.min_filter),
        .mag_filter = to_vk_filter(sampler.mag_filter),
        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .address_mode_u = to_vk_address_mode(sampler.wrap_s),
        .address_mode_v = to_vk_address_mode(sampler.wrap_t),
        .address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipmap_mode = to_vk_mipmap_mode(sampler.mip_filter),
        .max_lod = sampler.mip_filter == asset::GltfTextureMipFilter::None ? 0.0F : max_lod,
    };
}

[[nodiscard]] std::uint32_t
prepare_texture_for_ref(GltfPreparedScene& prepared, const asset::GltfAsset& asset,
                        const asset::GltfTextureRef& ref, asset::GltfTextureColorSpace color_space,
                        GltfSceneImportCapabilities capabilities, TextureCache& texture_cache) {
    if (!ref.has_value()) {
        return asset::kInvalidAssetIndex;
    }
    if (ref.texture_index >= asset.textures.size()) {
        throw std::runtime_error("glTF material texture index is out of range");
    }
    const TextureCacheKey key{.texture_index = ref.texture_index, .color_space = color_space};
    if (const auto existing = texture_cache.find(key); existing != texture_cache.end()) {
        return existing->second;
    }

    const asset::GltfTexture& texture = asset.textures[ref.texture_index];
    if (texture.image_index >= asset.images.size()) {
        throw std::runtime_error("glTF texture image index is out of range");
    }
    const asset::GltfImage& image = asset.images[texture.image_index];
    GltfPreparedTexture prepared_texture;
    if (image.encoding == asset::GltfImageEncoding::Ktx2Basisu) {
        GltfBasisuTextureUpload transcoded = transcode_gltf_basisu_texture(
            image, color_space, capabilities.supports_texture_compression_bc);
        prepared_texture = {
            .extent = transcoded.extent,
            .mip_levels = transcoded.mip_levels,
            .format = transcoded.format,
            .bytes = std::move(transcoded.bytes),
            .mips = std::move(transcoded.mips),
            .sampler = sampler_config_for_texture(asset, texture, transcoded.mip_levels),
        };
    } else if (image.encoding == asset::GltfImageEncoding::Rgba8) {
        prepared_texture = {
            .extent = {image.width, image.height},
            .mip_levels = 1,
            .format = image_format_for_color_space(color_space),
            .bytes = image.rgba8,
            .sampler = sampler_config_for_texture(asset, texture),
        };
    } else {
        throw std::runtime_error("unsupported glTF image encoding");
    }

    const std::uint32_t prepared_index = static_cast<std::uint32_t>(prepared.textures.size());
    prepared.textures.push_back(std::move(prepared_texture));
    texture_cache.emplace(key, prepared_index);
    return prepared_index;
}

void prepare_material_texture(GltfPreparedMaterial& material, GltfPreparedScene& prepared,
                              const asset::GltfAsset& asset, render::PbrMaterialBinding binding,
                              const asset::GltfTextureRef& texture,
                              asset::GltfTextureColorSpace color_space,
                              GltfSceneImportCapabilities capabilities,
                              TextureCache& texture_cache) {
    material.textures.push_back({
        .binding = binding,
        .texture_index = prepare_texture_for_ref(prepared, asset, texture, color_space,
                                                 capabilities, texture_cache),
    });
}

[[nodiscard]] std::string import_label(const GltfSceneImportConfig& config, const char* kind,
                                       std::size_t index) {
    return config.label_prefix + "." + kind + "." + std::to_string(index);
}

} // namespace

void prepare_gltf_materials(GltfPreparedScene& prepared, const asset::GltfAsset& asset,
                            const GltfSceneImportConfig& config,
                            GltfSceneImportCapabilities capabilities) {
    TextureCache texture_cache;
    prepared.materials.reserve(asset.materials.size());
    for (std::size_t index = 0; index < asset.materials.size(); ++index) {
        const asset::GltfMaterial& source = asset.materials[index];
        const render::MaterialAlphaMode alpha_mode = gltf_alpha_mode(source.alpha_mode);
        // A nonzero volume thickness defines a closed-medium boundary even
        // when transmissionFactor makes that optical contribution neutral.
        const bool volume_boundary = source.volume_thickness_factor > 0.0F;
        const VkCullModeFlags cull_mode =
            volume_boundary || !source.double_sided ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        GltfPreparedMaterial material{
            .definition =
                {
                    .label = source.label.empty() ? import_label(config, "material", index)
                                                  : source.label,
                    .factors =
                        {
                            .base_color_factor = source.base_color_factor,
                            .emissive_factor = source.emissive_factor,
                            .alpha_cutoff = source.alpha_mode == asset::GltfAlphaMode::Mask
                                                ? source.alpha_cutoff
                                                : 0.0F,
                            .metallic_factor = source.metallic_factor,
                            .roughness_factor = source.roughness_factor,
                            .normal_scale = source.normal_scale,
                            .occlusion_strength = source.occlusion_strength,
                            .specular_color_factor = source.specular_color_factor,
                            .specular_factor = source.specular_factor,
                            .dielectric_ior = source.ior,
                            .transmission_factor = source.transmission_factor,
                            .volume_thickness_factor = source.volume_thickness_factor,
                            .volume_attenuation_color = source.volume_attenuation_color,
                            .volume_attenuation_distance = source.volume_attenuation_distance,
                            .dispersion = source.dispersion,
                            .clearcoat_factor = source.clearcoat_factor,
                            .clearcoat_roughness_factor = source.clearcoat_roughness_factor,
                            .clearcoat_normal_scale = source.clearcoat_normal_scale,
                            .sheen_color_factor = source.sheen_color_factor,
                            .sheen_roughness_factor = source.sheen_roughness_factor,
                            .anisotropy_strength = source.anisotropy_strength,
                            .anisotropy_rotation = source.anisotropy_rotation,
                            .iridescence_factor = source.iridescence_factor,
                            .iridescence_ior = source.iridescence_ior,
                            .iridescence_thickness_minimum = source.iridescence_thickness_minimum,
                            .iridescence_thickness_maximum = source.iridescence_thickness_maximum,
                            .unlit = source.unlit,
                            .texture_flags = pbr_texture_flags(source),
                            .texture_transforms = pbr_texture_transforms(source),
                        },
                    .alpha_mode = alpha_mode,
                    // KHR_materials_volume defines a closed volume boundary,
                    // and doubleSided does not alter that boundary. This
                    // first thick-volume approximation renders its exterior
                    // face only; a later exact entry/exit implementation can
                    // supply the matching back-face information.
                    .cull_mode = cull_mode,
                    .sort_key = static_cast<std::uint32_t>(index),
                },
        };
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::BaseColor,
                                 source.base_color_texture,
                                 asset::gltf_texture_color_space_for_base_color(), capabilities,
                                 texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::MetallicRoughness,
                                 source.metallic_roughness_texture,
                                 asset::GltfTextureColorSpace::Linear, capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::Normal,
                                 source.normal_texture, asset::GltfTextureColorSpace::Linear,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::Occlusion,
                                 source.occlusion_texture, asset::GltfTextureColorSpace::Linear,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::Emissive,
                                 source.emissive_texture, asset::GltfTextureColorSpace::Srgb,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::Specular,
                                 source.specular_texture, asset::GltfTextureColorSpace::Linear,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::SpecularColor,
                                 source.specular_color_texture, asset::GltfTextureColorSpace::Srgb,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::Clearcoat,
                                 source.clearcoat_texture, asset::GltfTextureColorSpace::Linear,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::ClearcoatRoughness,
                                 source.clearcoat_roughness_texture,
                                 asset::GltfTextureColorSpace::Linear, capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::ClearcoatNormal,
                                 source.clearcoat_normal_texture,
                                 asset::GltfTextureColorSpace::Linear, capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::SheenColor,
                                 source.sheen_color_texture, asset::GltfTextureColorSpace::Srgb,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::SheenRoughness,
                                 source.sheen_roughness_texture,
                                 asset::GltfTextureColorSpace::Linear, capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::Anisotropy,
                                 source.anisotropy_texture, asset::GltfTextureColorSpace::Linear,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset, render::PbrMaterialBinding::Iridescence,
                                 source.iridescence_texture, asset::GltfTextureColorSpace::Linear,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::IridescenceThickness,
                                 source.iridescence_thickness_texture,
                                 asset::GltfTextureColorSpace::Linear, capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::Transmission,
                                 source.transmission_texture, asset::GltfTextureColorSpace::Linear,
                                 capabilities, texture_cache);
        prepare_material_texture(material, prepared, asset,
                                 render::PbrMaterialBinding::VolumeThickness,
                                 source.volume_thickness_texture,
                                 asset::GltfTextureColorSpace::Linear, capabilities, texture_cache);
        prepared.materials.push_back(std::move(material));
    }
    if (prepared.materials.empty()) {
        throw std::runtime_error("glTF scene import requires at least one material");
    }
}

} // namespace cubey
