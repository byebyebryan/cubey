#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

namespace cubey::asset::gltf_internal {
namespace {

[[nodiscard]] std::uint32_t checked_material_index(std::ptrdiff_t index, const char* label) {
    if (index < 0 || static_cast<std::uint64_t>(index) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw gltf_error(std::string(label) + " index is out of range");
    }
    return static_cast<std::uint32_t>(index);
}

template <typename T>
[[nodiscard]] std::uint32_t material_pointer_index(const T* pointer, const T* base,
                                                   cgltf_size count, const char* label) {
    if (pointer == nullptr) {
        return kInvalidAssetIndex;
    }
    if (base == nullptr || pointer < base || pointer >= base + count) {
        throw gltf_error(std::string(label) + " pointer is outside the glTF data");
    }
    return checked_material_index(pointer - base, label);
}

[[nodiscard]] GltfTextureFilter min_filter(cgltf_int value) noexcept {
    switch (value) {
    case 9728:
    case 9984:
    case 9986:
        return GltfTextureFilter::Nearest;
    default:
        return GltfTextureFilter::Linear;
    }
}

[[nodiscard]] GltfTextureFilter mag_filter(cgltf_int value) noexcept {
    return value == 9728 ? GltfTextureFilter::Nearest : GltfTextureFilter::Linear;
}

[[nodiscard]] GltfTextureMipFilter mip_filter(cgltf_int value) noexcept {
    switch (value) {
    case 9728:
    case 9729:
        return GltfTextureMipFilter::None;
    case 9984:
    case 9985:
        return GltfTextureMipFilter::Nearest;
    case 9986:
    case 9987:
    default:
        return GltfTextureMipFilter::Linear;
    }
}

[[nodiscard]] GltfTextureWrap wrap_mode(cgltf_int value) noexcept {
    switch (value) {
    case 33071:
        return GltfTextureWrap::ClampToEdge;
    case 33648:
        return GltfTextureWrap::MirroredRepeat;
    default:
        return GltfTextureWrap::Repeat;
    }
}

[[nodiscard]] GltfSampler load_sampler(const cgltf_sampler& sampler) {
    return {
        .label = label_or_empty(sampler.name),
        .min_filter = min_filter(sampler.min_filter),
        .mag_filter = mag_filter(sampler.mag_filter),
        .mip_filter = mip_filter(sampler.min_filter),
        .wrap_s = wrap_mode(sampler.wrap_s),
        .wrap_t = wrap_mode(sampler.wrap_t),
    };
}

[[nodiscard]] GltfTextureRef load_texture_ref(const cgltf_texture_view& view,
                                              const cgltf_texture* texture_base,
                                              cgltf_size texture_count) {
    GltfTextureRef ref{
        .texture_index =
            material_pointer_index(view.texture, texture_base, texture_count, "texture"),
        .texcoord = static_cast<std::uint32_t>(view.texcoord),
    };
    if (view.has_transform != 0) {
        ref.offset = {view.transform.offset[0], view.transform.offset[1]};
        ref.rotation = view.transform.rotation;
        ref.scale = {view.transform.scale[0], view.transform.scale[1]};
        if (view.transform.has_texcoord != 0) {
            ref.texcoord = static_cast<std::uint32_t>(view.transform.texcoord);
        }
    }
    if (ref.has_value() && ref.texcoord > 1U) {
        throw gltf_error("texture coordinate sets above TEXCOORD_1 are not supported");
    }
    return ref;
}

[[nodiscard]] GltfAlphaMode load_alpha_mode(cgltf_alpha_mode mode) {
    switch (mode) {
    case cgltf_alpha_mode_mask:
        return GltfAlphaMode::Mask;
    case cgltf_alpha_mode_blend:
        return GltfAlphaMode::Blend;
    case cgltf_alpha_mode_opaque:
    default:
        return GltfAlphaMode::Opaque;
    }
}

[[nodiscard]] GltfMaterial default_material() {
    return {
        .label = "default",
        .base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F},
        .metallic_factor = 1.0F,
        .roughness_factor = 1.0F,
    };
}

void require_valid_material_ior(float ior) {
    if (!std::isfinite(ior) || (ior != 0.0F && ior < 1.0F)) {
        throw gltf_error(
            "KHR_materials_ior ior must be 0 or a finite value greater than or equal to 1");
    }
}

void require_valid_specular_factor(float factor) {
    if (!std::isfinite(factor) || factor < 0.0F || factor > 1.0F) {
        throw gltf_error("KHR_materials_specular specularFactor must be finite and in [0, 1]");
    }
}

void require_valid_specular_color(const math::Vec3& color) {
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
        color.r < 0.0F || color.g < 0.0F || color.b < 0.0F) {
        throw gltf_error(
            "KHR_materials_specular specularColorFactor must contain finite nonnegative values");
    }
}

void require_valid_clearcoat_factor(float factor, const char* field) {
    if (!std::isfinite(factor) || factor < 0.0F || factor > 1.0F) {
        throw gltf_error(std::string("KHR_materials_clearcoat ") + field +
                         " must be finite and in [0, 1]");
    }
}

void require_valid_sheen_color(const math::Vec3& color) {
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
        color.r < 0.0F || color.r > 1.0F || color.g < 0.0F || color.g > 1.0F || color.b < 0.0F ||
        color.b > 1.0F) {
        throw gltf_error(
            "KHR_materials_sheen sheenColorFactor must contain finite values in [0, 1]");
    }
}

void require_valid_sheen_roughness(float roughness) {
    if (!std::isfinite(roughness) || roughness < 0.0F || roughness > 1.0F) {
        throw gltf_error("KHR_materials_sheen sheenRoughnessFactor must be finite and in [0, 1]");
    }
}

void require_lit_material_extension_compatibility(const cgltf_material& material,
                                                  const char* extension) {
    if (material.unlit != 0) {
        throw gltf_error(std::string(extension) + " must not be used with KHR_materials_unlit");
    }
    if (material.has_pbr_specular_glossiness != 0) {
        throw gltf_error(std::string(extension) +
                         " must not be used with KHR_materials_pbrSpecularGlossiness");
    }
}

void require_valid_anisotropy_strength(float strength) {
    if (!std::isfinite(strength) || strength < 0.0F || strength > 1.0F) {
        throw gltf_error(
            "KHR_materials_anisotropy anisotropyStrength must be finite and in [0, 1]");
    }
}

void require_valid_anisotropy_rotation(float rotation) {
    if (!std::isfinite(rotation)) {
        throw gltf_error("KHR_materials_anisotropy anisotropyRotation must be finite");
    }
}

void require_valid_iridescence_factor(float factor) {
    if (!std::isfinite(factor) || factor < 0.0F || factor > 1.0F) {
        throw gltf_error(
            "KHR_materials_iridescence iridescenceFactor must be finite and in [0, 1]");
    }
}

void require_valid_iridescence_ior(float ior) {
    if (!std::isfinite(ior) || ior < 1.0F) {
        throw gltf_error("KHR_materials_iridescence iridescenceIor must be finite and greater than "
                         "or equal to 1");
    }
}

void require_valid_iridescence_thickness(float thickness, const char* field) {
    if (!std::isfinite(thickness) || thickness < 0.0F) {
        throw gltf_error(std::string("KHR_materials_iridescence ") + field +
                         " must be finite and greater than or equal to 0");
    }
}

void require_valid_transmission_factor(float factor) {
    if (!std::isfinite(factor) || factor < 0.0F || factor > 1.0F) {
        throw gltf_error(
            "KHR_materials_transmission transmissionFactor must be finite and in [0, 1]");
    }
}

void require_valid_volume_thickness_factor(float thickness) {
    if (!std::isfinite(thickness) || thickness < 0.0F) {
        throw gltf_error(
            "KHR_materials_volume thicknessFactor must be finite and greater than or equal to 0");
    }
}

void require_valid_dispersion(float dispersion) {
    if (!std::isfinite(dispersion) || dispersion < 0.0F) {
        throw gltf_error(
            "KHR_materials_dispersion dispersion must be finite and greater than or equal to 0");
    }
}

void require_valid_volume_attenuation_color(const math::Vec3& color) {
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
        color.r < 0.0F || color.r > 1.0F || color.g < 0.0F || color.g > 1.0F || color.b < 0.0F ||
        color.b > 1.0F) {
        throw gltf_error(
            "KHR_materials_volume attenuationColor must contain finite values in [0, 1]");
    }
}

[[nodiscard]] float load_volume_attenuation_distance(const cgltf_volume& volume) {
    // cgltf uses FLT_MAX to represent the glTF default of +infinity. Cubey
    // carries that as zero so shader attenuation can cheaply no-op.
    if (volume.attenuation_distance == std::numeric_limits<float>::max()) {
        return 0.0F;
    }
    if (!std::isfinite(volume.attenuation_distance) || volume.attenuation_distance <= 0.0F) {
        throw gltf_error(
            "KHR_materials_volume attenuationDistance must be finite and greater than 0");
    }
    return volume.attenuation_distance;
}

[[nodiscard]] GltfMaterial load_material(const cgltf_material& material,
                                         const cgltf_texture* texture_base,
                                         cgltf_size texture_count) {
    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
    const math::Vec3 specular_color =
        material.has_specular != 0
            ? math::Vec3{
                  material.specular.specular_color_factor[0],
                  material.specular.specular_color_factor[1],
                  material.specular.specular_color_factor[2],
              }
            : math::Vec3{1.0F, 1.0F, 1.0F};
    const float emissive_strength =
        material.has_emissive_strength != 0 ? material.emissive_strength.emissive_strength : 1.0F;
    const math::Vec3 sheen_color =
        material.has_sheen != 0
            ? math::Vec3{
                  material.sheen.sheen_color_factor[0],
                  material.sheen.sheen_color_factor[1],
                  material.sheen.sheen_color_factor[2],
              }
            : math::Vec3{0.0F, 0.0F, 0.0F};
    const math::Vec3 volume_attenuation_color =
        material.has_volume != 0
            ? math::Vec3{
                  material.volume.attenuation_color[0],
                  material.volume.attenuation_color[1],
                  material.volume.attenuation_color[2],
              }
            : math::Vec3{1.0F, 1.0F, 1.0F};
    if (material.has_ior != 0) {
        require_valid_material_ior(material.ior.ior);
    }
    if (material.has_specular != 0) {
        require_valid_specular_factor(material.specular.specular_factor);
        require_valid_specular_color(specular_color);
    }
    if (material.has_clearcoat != 0) {
        require_valid_clearcoat_factor(material.clearcoat.clearcoat_factor, "clearcoatFactor");
        require_valid_clearcoat_factor(material.clearcoat.clearcoat_roughness_factor,
                                       "clearcoatRoughnessFactor");
        require_lit_material_extension_compatibility(material, "KHR_materials_clearcoat");
    }
    if (material.has_sheen != 0) {
        require_valid_sheen_color(sheen_color);
        require_valid_sheen_roughness(material.sheen.sheen_roughness_factor);
        require_lit_material_extension_compatibility(material, "KHR_materials_sheen");
    }
    if (material.has_anisotropy != 0) {
        require_valid_anisotropy_strength(material.anisotropy.anisotropy_strength);
        require_valid_anisotropy_rotation(material.anisotropy.anisotropy_rotation);
        require_lit_material_extension_compatibility(material, "KHR_materials_anisotropy");
    }
    if (material.has_iridescence != 0) {
        require_valid_iridescence_factor(material.iridescence.iridescence_factor);
        require_valid_iridescence_ior(material.iridescence.iridescence_ior);
        require_valid_iridescence_thickness(material.iridescence.iridescence_thickness_min,
                                            "iridescenceThicknessMinimum");
        require_valid_iridescence_thickness(material.iridescence.iridescence_thickness_max,
                                            "iridescenceThicknessMaximum");
        require_lit_material_extension_compatibility(material, "KHR_materials_iridescence");
    }
    if (material.has_transmission != 0) {
        require_valid_transmission_factor(material.transmission.transmission_factor);
    }
    if (material.has_dispersion != 0) {
        if (material.has_volume == 0) {
            throw gltf_error("KHR_materials_dispersion requires KHR_materials_volume");
        }
        require_valid_dispersion(material.dispersion.dispersion);
        require_lit_material_extension_compatibility(material, "KHR_materials_dispersion");
    }
    const float volume_attenuation_distance =
        material.has_volume != 0 ? load_volume_attenuation_distance(material.volume) : 0.0F;
    if (material.has_volume != 0) {
        if (material.has_transmission == 0) {
            throw gltf_error("KHR_materials_volume requires KHR_materials_transmission");
        }
        require_valid_volume_thickness_factor(material.volume.thickness_factor);
        require_valid_volume_attenuation_color(volume_attenuation_color);
        require_lit_material_extension_compatibility(material, "KHR_materials_volume");
    }
    if (material.has_transmission != 0) {
        require_lit_material_extension_compatibility(material, "KHR_materials_transmission");
    }
    return {
        .label = label_or_empty(material.name),
        .base_color_factor =
            {
                pbr.base_color_factor[0],
                pbr.base_color_factor[1],
                pbr.base_color_factor[2],
                pbr.base_color_factor[3],
            },
        .metallic_factor = pbr.metallic_factor,
        .roughness_factor = pbr.roughness_factor,
        .specular_color_factor = specular_color,
        .specular_factor = material.has_specular != 0 ? material.specular.specular_factor : 1.0F,
        .specular_texture =
            material.has_specular != 0
                ? load_texture_ref(material.specular.specular_texture, texture_base, texture_count)
                : GltfTextureRef{},
        .specular_color_texture = material.has_specular != 0
                                      ? load_texture_ref(material.specular.specular_color_texture,
                                                         texture_base, texture_count)
                                      : GltfTextureRef{},
        .ior = material.has_ior != 0 ? material.ior.ior : 1.5F,
        .transmission_factor =
            material.has_transmission != 0 ? material.transmission.transmission_factor : 0.0F,
        .transmission_texture = material.has_transmission != 0
                                    ? load_texture_ref(material.transmission.transmission_texture,
                                                       texture_base, texture_count)
                                    : GltfTextureRef{},
        .volume_thickness_factor =
            material.has_volume != 0 ? material.volume.thickness_factor : 0.0F,
        .volume_thickness_texture =
            material.has_volume != 0
                ? load_texture_ref(material.volume.thickness_texture, texture_base, texture_count)
                : GltfTextureRef{},
        .volume_attenuation_color = volume_attenuation_color,
        .volume_attenuation_distance = volume_attenuation_distance,
        .dispersion = material.has_dispersion != 0 ? material.dispersion.dispersion : 0.0F,
        .emissive_factor =
            {
                material.emissive_factor[0] * emissive_strength,
                material.emissive_factor[1] * emissive_strength,
                material.emissive_factor[2] * emissive_strength,
            },
        .clearcoat_factor =
            material.has_clearcoat != 0 ? material.clearcoat.clearcoat_factor : 0.0F,
        .clearcoat_roughness_factor =
            material.has_clearcoat != 0 ? material.clearcoat.clearcoat_roughness_factor : 0.0F,
        .clearcoat_normal_scale =
            material.has_clearcoat != 0 &&
                    material.clearcoat.clearcoat_normal_texture.texture != nullptr
                ? material.clearcoat.clearcoat_normal_texture.scale
                : 1.0F,
        .clearcoat_texture = material.has_clearcoat != 0
                                 ? load_texture_ref(material.clearcoat.clearcoat_texture,
                                                    texture_base, texture_count)
                                 : GltfTextureRef{},
        .clearcoat_roughness_texture =
            material.has_clearcoat != 0
                ? load_texture_ref(material.clearcoat.clearcoat_roughness_texture, texture_base,
                                   texture_count)
                : GltfTextureRef{},
        .clearcoat_normal_texture =
            material.has_clearcoat != 0
                ? load_texture_ref(material.clearcoat.clearcoat_normal_texture, texture_base,
                                   texture_count)
                : GltfTextureRef{},
        .sheen_color_factor = sheen_color,
        .sheen_roughness_factor =
            material.has_sheen != 0 ? material.sheen.sheen_roughness_factor : 0.0F,
        .sheen_color_texture =
            material.has_sheen != 0
                ? load_texture_ref(material.sheen.sheen_color_texture, texture_base, texture_count)
                : GltfTextureRef{},
        .sheen_roughness_texture = material.has_sheen != 0
                                       ? load_texture_ref(material.sheen.sheen_roughness_texture,
                                                          texture_base, texture_count)
                                       : GltfTextureRef{},
        .anisotropy_strength =
            material.has_anisotropy != 0 ? material.anisotropy.anisotropy_strength : 0.0F,
        .anisotropy_rotation =
            material.has_anisotropy != 0 ? material.anisotropy.anisotropy_rotation : 0.0F,
        .anisotropy_texture = material.has_anisotropy != 0
                                  ? load_texture_ref(material.anisotropy.anisotropy_texture,
                                                     texture_base, texture_count)
                                  : GltfTextureRef{},
        .iridescence_factor =
            material.has_iridescence != 0 ? material.iridescence.iridescence_factor : 0.0F,
        .iridescence_ior =
            material.has_iridescence != 0 ? material.iridescence.iridescence_ior : 1.3F,
        .iridescence_thickness_minimum =
            material.has_iridescence != 0 ? material.iridescence.iridescence_thickness_min : 100.0F,
        .iridescence_thickness_maximum =
            material.has_iridescence != 0 ? material.iridescence.iridescence_thickness_max : 400.0F,
        .iridescence_texture = material.has_iridescence != 0
                                   ? load_texture_ref(material.iridescence.iridescence_texture,
                                                      texture_base, texture_count)
                                   : GltfTextureRef{},
        .iridescence_thickness_texture =
            material.has_iridescence != 0
                ? load_texture_ref(material.iridescence.iridescence_thickness_texture, texture_base,
                                   texture_count)
                : GltfTextureRef{},
        .normal_scale = material.normal_texture.scale,
        .occlusion_strength = material.occlusion_texture.scale,
        .base_color_texture = load_texture_ref(pbr.base_color_texture, texture_base, texture_count),
        .metallic_roughness_texture =
            load_texture_ref(pbr.metallic_roughness_texture, texture_base, texture_count),
        .normal_texture = load_texture_ref(material.normal_texture, texture_base, texture_count),
        .occlusion_texture =
            load_texture_ref(material.occlusion_texture, texture_base, texture_count),
        .emissive_texture =
            load_texture_ref(material.emissive_texture, texture_base, texture_count),
        .alpha_mode = load_alpha_mode(material.alpha_mode),
        .alpha_cutoff = material.alpha_cutoff,
        .double_sided = material.double_sided != 0,
        .unlit = material.unlit != 0,
    };
}
} // namespace

void assemble_gltf_material_data(GltfAsset& asset, const cgltf_data& data,
                                 const std::filesystem::path& source_path,
                                 GltfAssetLoadProfile* profile) {
    asset.samplers.reserve(data.samplers_count);
    for (cgltf_size i = 0; i < data.samplers_count; ++i) {
        asset.samplers.push_back(load_sampler(data.samplers[i]));
    }

    asset.images.reserve(data.images_count);
    for (cgltf_size i = 0; i < data.images_count; ++i) {
        asset.images.push_back(decode_image(data.images[i], source_path, profile));
    }

    asset.textures.reserve(data.textures_count);
    for (cgltf_size i = 0; i < data.textures_count; ++i) {
        const cgltf_texture& texture = data.textures[i];
        const cgltf_image* image = texture.has_basisu ? texture.basisu_image : texture.image;
        asset.textures.push_back(GltfTexture{
            .label = label_or_empty(texture.name),
            .image_index = material_pointer_index(image, data.images, data.images_count, "image"),
            .sampler_index = material_pointer_index(texture.sampler, data.samplers,
                                                    data.samplers_count, "sampler"),
        });
    }

    asset.materials.reserve(data.materials_count + 1);
    asset.materials.push_back(default_material());
    for (cgltf_size i = 0; i < data.materials_count; ++i) {
        asset.materials.push_back(
            load_material(data.materials[i], data.textures, data.textures_count));
    }
}

} // namespace cubey::asset::gltf_internal
