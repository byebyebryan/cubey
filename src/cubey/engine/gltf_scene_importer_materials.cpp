#include "gltf_scene_importer_internal.h"

#include "gltf_basisu_texture.h"

#include <cubey/engine/engine.h>
#include <cubey/render/material.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/sampler.h>

#include <cmath>
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

[[nodiscard]] render::PbrTextureTransform pbr_texture_transform(const asset::GltfTextureRef& ref) {
    return {
        .offset_scale = {ref.offset.x, ref.offset.y, ref.scale.x, ref.scale.y},
        .rotation_texcoord =
            {
                std::cos(ref.rotation),
                std::sin(ref.rotation),
                static_cast<float>(ref.texcoord),
                0.0F,
            },
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
    };
}

[[nodiscard]] std::uint32_t pbr_texture_flags(const asset::GltfMaterial& material) {
    std::uint32_t flags = 0U;
    if (material.specular_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::Specular);
    }
    if (material.specular_color_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::SpecularColor);
    }
    if (material.clearcoat_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::Clearcoat);
    }
    if (material.clearcoat_roughness_texture.has_value()) {
        flags |=
            render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::ClearcoatRoughness);
    }
    if (material.clearcoat_normal_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::ClearcoatNormal);
    }
    if (material.sheen_color_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::SheenColor);
    }
    if (material.sheen_roughness_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::SheenRoughness);
    }
    if (material.anisotropy_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::Anisotropy);
    }
    if (material.iridescence_texture.has_value()) {
        flags |= render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::Iridescence);
    }
    if (material.iridescence_thickness_texture.has_value()) {
        flags |=
            render::pbr_material_texture_flag(render::PbrMaterialTextureFlag::IridescenceThickness);
    }
    return flags;
}

[[nodiscard]] const render::Texture2D& default_texture(const GltfSceneImportResources& resources,
                                                       render::PbrMaterialBinding binding) {
    if (!resources.default_textures.has_value()) {
        throw std::runtime_error("default PBR texture is not initialized");
    }
    return render::pbr_default_texture(resources.default_textures.value(), binding);
}

[[nodiscard]] TextureBinding texture_binding(const render::Texture2D& texture) {
    return {
        .sampler = texture.sampler().handle(),
        .view = texture.view(),
    };
}

[[nodiscard]] vulkan::SamplerConfig sampler_config_for_texture(const asset::GltfAsset& asset,
                                                               const asset::GltfTexture& texture,
                                                               std::uint32_t mip_levels = 1) {
    const float max_lod = static_cast<float>(mip_levels > 0 ? mip_levels - 1U : 0U);
    if (texture.sampler_index >= asset.samplers.size()) {
        return {
            .max_lod = max_lod,
        };
    }
    const asset::GltfSampler& sampler = asset.samplers[texture.sampler_index];
    const float sampled_max_lod =
        sampler.mip_filter == asset::GltfTextureMipFilter::None ? 0.0F : max_lod;
    return {
        .min_filter = to_vk_filter(sampler.min_filter),
        .mag_filter = to_vk_filter(sampler.mag_filter),
        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .address_mode_u = to_vk_address_mode(sampler.wrap_s),
        .address_mode_v = to_vk_address_mode(sampler.wrap_t),
        .address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipmap_mode = to_vk_mipmap_mode(sampler.mip_filter),
        .max_lod = sampled_max_lod,
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
    render::Texture2D uploaded = [&] {
        if (image.encoding == asset::GltfImageEncoding::Ktx2Basisu) {
            GltfBasisuTextureUpload transcoded = transcode_gltf_basisu_texture(
                image, color_space, device.supports_texture_compression_bc());
            return render::create_uploaded_texture_2d(
                device, gpu,
                {
                    .extent = transcoded.extent,
                    .mip_levels = transcoded.mip_levels,
                    .format = transcoded.format,
                    .bytes = std::span<const std::uint8_t>{transcoded.bytes.data(),
                                                           transcoded.bytes.size()},
                    .mips = std::span<const render::UploadedTexture2DMip>{transcoded.mips.data(),
                                                                          transcoded.mips.size()},
                    .create_sampler = true,
                    .sampler = sampler_config_for_texture(asset, texture, transcoded.mip_levels),
                });
        }
        if (image.encoding != asset::GltfImageEncoding::Rgba8) {
            throw std::runtime_error("unsupported glTF image encoding");
        }
        return render::create_uploaded_texture_2d(
            device, gpu,
            {
                .extent = {image.width, image.height},
                .format = image_format_for_color_space(color_space),
                .rgba8 = std::span<const std::uint8_t>{image.rgba8.data(), image.rgba8.size()},
                .create_sampler = true,
                .sampler = sampler_config_for_texture(asset, texture),
            });
    }();
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
    const TextureBinding specular = texture_binding_for_ref(
        device, gpu, resources, asset, source.specular_texture,
        asset::GltfTextureColorSpace::Linear, render::PbrMaterialBinding::Specular, texture_cache);
    const TextureBinding specular_color =
        texture_binding_for_ref(device, gpu, resources, asset, source.specular_color_texture,
                                asset::GltfTextureColorSpace::Srgb,
                                render::PbrMaterialBinding::SpecularColor, texture_cache);
    const TextureBinding clearcoat = texture_binding_for_ref(
        device, gpu, resources, asset, source.clearcoat_texture,
        asset::GltfTextureColorSpace::Linear, render::PbrMaterialBinding::Clearcoat, texture_cache);
    const TextureBinding clearcoat_roughness =
        texture_binding_for_ref(device, gpu, resources, asset, source.clearcoat_roughness_texture,
                                asset::GltfTextureColorSpace::Linear,
                                render::PbrMaterialBinding::ClearcoatRoughness, texture_cache);
    const TextureBinding clearcoat_normal =
        texture_binding_for_ref(device, gpu, resources, asset, source.clearcoat_normal_texture,
                                asset::GltfTextureColorSpace::Linear,
                                render::PbrMaterialBinding::ClearcoatNormal, texture_cache);
    const TextureBinding sheen_color = texture_binding_for_ref(
        device, gpu, resources, asset, source.sheen_color_texture,
        asset::GltfTextureColorSpace::Srgb, render::PbrMaterialBinding::SheenColor, texture_cache);
    const TextureBinding sheen_roughness =
        texture_binding_for_ref(device, gpu, resources, asset, source.sheen_roughness_texture,
                                asset::GltfTextureColorSpace::Linear,
                                render::PbrMaterialBinding::SheenRoughness, texture_cache);
    const TextureBinding anisotropy =
        texture_binding_for_ref(device, gpu, resources, asset, source.anisotropy_texture,
                                asset::GltfTextureColorSpace::Linear,
                                render::PbrMaterialBinding::Anisotropy, texture_cache);
    const TextureBinding iridescence =
        texture_binding_for_ref(device, gpu, resources, asset, source.iridescence_texture,
                                asset::GltfTextureColorSpace::Linear,
                                render::PbrMaterialBinding::Iridescence, texture_cache);
    const TextureBinding iridescence_thickness =
        texture_binding_for_ref(device, gpu, resources, asset, source.iridescence_thickness_texture,
                                asset::GltfTextureColorSpace::Linear,
                                render::PbrMaterialBinding::IridescenceThickness, texture_cache);

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
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Specular),
            .sampler = specular.sampler,
            .image_view = specular.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::SpecularColor),
            .sampler = specular_color.sampler,
            .image_view = specular_color.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Clearcoat),
            .sampler = clearcoat.sampler,
            .image_view = clearcoat.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::ClearcoatRoughness),
            .sampler = clearcoat_roughness.sampler,
            .image_view = clearcoat_roughness.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::ClearcoatNormal),
            .sampler = clearcoat_normal.sampler,
            .image_view = clearcoat_normal.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::SheenColor),
            .sampler = sheen_color.sampler,
            .image_view = sheen_color.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::SheenRoughness),
            .sampler = sheen_roughness.sampler,
            .image_view = sheen_roughness.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Anisotropy),
            .sampler = anisotropy.sampler,
            .image_view = anisotropy.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Iridescence),
            .sampler = iridescence.sampler,
            .image_view = iridescence.view,
        },
        render::SampledImageMaterialBinding{
            .binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::IridescenceThickness),
            .sampler = iridescence_thickness.sampler,
            .image_view = iridescence_thickness.view,
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
    if (resources.default_textures.has_value()) {
        return;
    }
    resources.default_textures.emplace(render::create_pbr_default_texture_set(device, gpu));
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
                .cull_mode = source.double_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT,
                .sort_key = static_cast<std::uint32_t>(index),
                .pass_mask = render::material_pass_mask_for_alpha_mode(alpha_mode),
            });
        result.material_handles.push_back(material);
        resources.materials.set_factors(
            material, render::PbrMaterialFactors{
                          .base_color_factor = source.base_color_factor,
                          .emissive_factor = source.emissive_factor,
                          .alpha_cutoff = source.alpha_mode == asset::GltfAlphaMode::Mask
                                              ? source.alpha_cutoff
                                              : 0.0F,
                          .alpha_mode = alpha_mode,
                          .metallic_factor = source.metallic_factor,
                          .roughness_factor = source.roughness_factor,
                          .normal_scale = source.normal_scale,
                          .occlusion_strength = source.occlusion_strength,
                          .specular_color_factor = source.specular_color_factor,
                          .specular_factor = source.specular_factor,
                          .reflectance = source.reflectance,
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
                      });
        resources.materials.emplace_instance(
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
