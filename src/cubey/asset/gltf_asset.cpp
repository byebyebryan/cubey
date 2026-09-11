#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace cubey::asset {
using namespace gltf_internal;
namespace {

class ScopedLoadProfilePhase {
  public:
    ScopedLoadProfilePhase(GltfAssetLoadProfile* profile, double GltfAssetLoadProfile::* field)
        : profile_(profile), field_(field), started_(Clock::now()) {}

    ~ScopedLoadProfilePhase() {
        if (profile_ != nullptr) {
            profile_->*field_ +=
                std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
        }
    }

    ScopedLoadProfilePhase(const ScopedLoadProfilePhase&) = delete;
    ScopedLoadProfilePhase& operator=(const ScopedLoadProfilePhase&) = delete;

  private:
    using Clock = std::chrono::steady_clock;

    GltfAssetLoadProfile* profile_ = nullptr;
    double GltfAssetLoadProfile::* field_ = nullptr;
    Clock::time_point started_{};
};

class ScopedAssetAssemblyProfile {
  public:
    explicit ScopedAssetAssemblyProfile(GltfAssetLoadProfile* profile)
        : profile_(profile), started_(Clock::now()),
          image_payload_before_(profile != nullptr ? profile->image_payload_milliseconds : 0.0),
          image_decode_before_(profile != nullptr ? profile->image_decode_milliseconds : 0.0) {}

    ~ScopedAssetAssemblyProfile() {
        if (profile_ == nullptr) {
            return;
        }
        const double elapsed =
            std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
        const double nested_image_work =
            (profile_->image_payload_milliseconds - image_payload_before_) +
            (profile_->image_decode_milliseconds - image_decode_before_);
        profile_->asset_assembly_milliseconds += std::max(0.0, elapsed - nested_image_work);
    }

    ScopedAssetAssemblyProfile(const ScopedAssetAssemblyProfile&) = delete;
    ScopedAssetAssemblyProfile& operator=(const ScopedAssetAssemblyProfile&) = delete;

  private:
    using Clock = std::chrono::steady_clock;

    GltfAssetLoadProfile* profile_ = nullptr;
    Clock::time_point started_{};
    double image_payload_before_ = 0.0;
    double image_decode_before_ = 0.0;
};

[[nodiscard]] std::uint32_t checked_index(std::ptrdiff_t index, const char* label) {
    if (index < 0 || static_cast<std::uint64_t>(index) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw gltf_error(std::string(label) + " index is out of range");
    }
    return static_cast<std::uint32_t>(index);
}

template <typename T>
[[nodiscard]] std::uint32_t pointer_index(const T* pointer, const T* base, cgltf_size count,
                                          const char* label) {
    if (pointer == nullptr) {
        return kInvalidAssetIndex;
    }
    if (base == nullptr || pointer < base || pointer >= base + count) {
        throw gltf_error(std::string(label) + " pointer is outside the glTF data");
    }
    return checked_index(pointer - base, label);
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
        .texture_index = pointer_index(view.texture, texture_base, texture_count, "texture"),
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

[[nodiscard]] bool normalized_unsigned_accessor(const cgltf_accessor* accessor) noexcept {
    return accessor != nullptr && accessor->normalized != 0 &&
           (accessor->component_type == cgltf_component_type_r_8u ||
            accessor->component_type == cgltf_component_type_r_16u);
}

void require_optional_texcoord_accessor(const cgltf_accessor* accessor, const char* label) {
    if (accessor == nullptr) {
        return;
    }
    if (cgltf_num_components(accessor->type) != 2) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
    if (accessor->component_type != cgltf_component_type_r_32f &&
        !normalized_unsigned_accessor(accessor)) {
        throw gltf_error(std::string(label) +
                         " attribute must use FLOAT or normalized unsigned components");
    }
}

void require_optional_color_accessor(const cgltf_accessor* accessor, const char* label) {
    if (accessor == nullptr) {
        return;
    }
    const cgltf_size component_count = cgltf_num_components(accessor->type);
    if (component_count != 3 && component_count != 4) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
    if (accessor->component_type != cgltf_component_type_r_32f &&
        !normalized_unsigned_accessor(accessor)) {
        throw gltf_error(std::string(label) +
                         " attribute must use FLOAT or normalized unsigned components");
    }
}

[[nodiscard]] std::vector<float> read_float_accessor_values(const cgltf_accessor* accessor,
                                                            cgltf_size component_count,
                                                            const char* label);

[[nodiscard]] std::vector<math::Vec4> read_color_accessor_values(const cgltf_accessor* accessor,
                                                                 const char* label) {
    require_optional_color_accessor(accessor, label);
    if (accessor == nullptr) {
        return {};
    }
    const cgltf_size component_count = cgltf_num_components(accessor->type);
    const std::vector<float> unpacked =
        read_float_accessor_values(accessor, component_count, label);
    std::vector<math::Vec4> values;
    values.reserve(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        const std::size_t offset = static_cast<std::size_t>(i * component_count);
        values.push_back({
            unpacked[offset + 0U],
            unpacked[offset + 1U],
            unpacked[offset + 2U],
            component_count == 4 ? unpacked[offset + 3U] : 1.0F,
        });
    }
    return values;
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

[[nodiscard]] const cgltf_accessor* find_attribute(std::span<const cgltf_attribute> attributes,
                                                   cgltf_attribute_type type,
                                                   cgltf_int index = 0) noexcept {
    for (const cgltf_attribute& attribute : attributes) {
        if (attribute.type == type && attribute.index == index) {
            return attribute.data;
        }
    }
    return nullptr;
}

[[nodiscard]] const cgltf_accessor* find_attribute(const cgltf_primitive& primitive,
                                                   cgltf_attribute_type type,
                                                   cgltf_int index = 0) noexcept {
    return find_attribute(
        std::span<const cgltf_attribute>{primitive.attributes, primitive.attributes_count}, type,
        index);
}

void require_accessor_components(const cgltf_accessor* accessor, cgltf_size components,
                                 const char* label) {
    if (accessor == nullptr) {
        throw gltf_error(std::string("primitive is missing required ") + label + " attribute");
    }
    if (cgltf_num_components(accessor->type) != components) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
    if (accessor->component_type != cgltf_component_type_r_32f) {
        throw gltf_error(std::string(label) + " attribute must use FLOAT components");
    }
}

void require_optional_accessor_components(const cgltf_accessor* accessor, cgltf_size components,
                                          const char* label) {
    if (accessor == nullptr) {
        return;
    }
    if (cgltf_num_components(accessor->type) != components) {
        throw gltf_error(std::string(label) + " attribute has unsupported component count");
    }
}

void require_float_accessor(const cgltf_accessor* accessor, cgltf_type type, const char* label) {
    if (accessor == nullptr) {
        throw gltf_error(std::string(label) + " accessor is missing");
    }
    if (accessor->type != type) {
        throw gltf_error(std::string(label) + " accessor has unsupported type");
    }
    if (accessor->component_type != cgltf_component_type_r_32f) {
        throw gltf_error(std::string(label) + " accessor must use FLOAT components");
    }
}

void require_optional_float_accessor(const cgltf_accessor* accessor, cgltf_type type,
                                     const char* label) {
    if (accessor == nullptr) {
        return;
    }
    require_float_accessor(accessor, type, label);
}

void require_optional_morph_accessor_count(const cgltf_accessor* accessor, cgltf_size vertex_count,
                                           const char* label) {
    if (accessor == nullptr) {
        return;
    }
    if (accessor->count != vertex_count) {
        throw gltf_error(std::string(label) + " morph target count must match POSITION count");
    }
}

[[nodiscard]] std::vector<float> read_float_accessor_values(const cgltf_accessor* accessor,
                                                            cgltf_size component_count,
                                                            const char* label) {
    if (accessor == nullptr) {
        throw gltf_error(std::string(label) + " accessor is missing");
    }
    std::vector<float> values(accessor->count * component_count);
    if (values.empty()) {
        return values;
    }
    const cgltf_size read_count =
        cgltf_accessor_unpack_floats(accessor, values.data(), values.size());
    if (read_count != values.size()) {
        throw gltf_error(std::string("failed to read ") + label + " accessor");
    }
    return values;
}

[[nodiscard]] math::Vec2 vec2_at(std::span<const float> values, std::size_t index) {
    const std::size_t offset = index * 2U;
    return {values[offset + 0U], values[offset + 1U]};
}

[[nodiscard]] math::Vec3 vec3_at(std::span<const float> values, std::size_t index) {
    const std::size_t offset = index * 3U;
    return {values[offset + 0U], values[offset + 1U], values[offset + 2U]};
}

[[nodiscard]] math::Vec4 vec4_at(std::span<const float> values, std::size_t index) {
    const std::size_t offset = index * 4U;
    return {values[offset + 0U], values[offset + 1U], values[offset + 2U], values[offset + 3U]};
}

[[nodiscard]] std::array<std::uint16_t, 4> u16_vec4_at(std::span<const float> values,
                                                       std::size_t index, const char* label) {
    const std::size_t offset = index * 4U;
    std::array<std::uint16_t, 4> result{};
    for (std::size_t component = 0; component < result.size(); ++component) {
        const float value = values[offset + component];
        if (value < 0.0F || value > static_cast<float>(std::numeric_limits<std::uint16_t>::max()) ||
            std::floor(value) != value) {
            throw gltf_error(std::string(label) + " value is out of range");
        }
        result[component] = static_cast<std::uint16_t>(value);
    }
    return result;
}

[[nodiscard]] std::vector<std::array<std::uint16_t, 4>>
read_u16_vec4_accessor_values(const cgltf_accessor* accessor, const char* label) {
    const std::vector<float> values = read_float_accessor_values(accessor, 4, label);
    std::vector<std::array<std::uint16_t, 4>> result;
    result.reserve(accessor->count);
    for (cgltf_size index = 0; index < accessor->count; ++index) {
        result.push_back(u16_vec4_at(values, index, label));
    }
    return result;
}

[[nodiscard]] math::Mat4 read_mat4(const cgltf_accessor* accessor, cgltf_size index) {
    cgltf_float values[16]{};
    if (cgltf_accessor_read_float(accessor, index, values, 16) == 0) {
        const std::vector<float> unpacked = read_float_accessor_values(accessor, 16, "MAT4");
        std::memcpy(values, unpacked.data() + (index * 16U), sizeof(values));
    }

    math::Mat4 matrix{1.0F};
    std::memcpy(&matrix[0][0], values, sizeof(values));
    return matrix;
}

[[nodiscard]] GltfBounds3D bounds_for_positions(std::span<const GltfVertex> vertices) {
    if (vertices.empty()) {
        return {};
    }

    math::Vec3 min_position = vertices.front().position;
    math::Vec3 max_position = vertices.front().position;
    for (const GltfVertex& vertex : vertices) {
        min_position = glm::min(min_position, vertex.position);
        max_position = glm::max(max_position, vertex.position);
    }

    return {
        .center = (min_position + max_position) * 0.5F,
        .half_extent = (max_position - min_position) * 0.5F,
    };
}

[[nodiscard]] math::Vec2 tangent_texcoord(const GltfVertex& vertex, std::uint32_t texcoord_set) {
    return texcoord_set == 1U ? vertex.texcoord1 : vertex.texcoord0;
}

void generate_tangents(GltfMeshPrimitive& primitive, std::uint32_t texcoord_set) {
    std::vector<math::Vec3> tangent_accum(primitive.vertices.size(), math::Vec3{0.0F, 0.0F, 0.0F});
    std::vector<math::Vec3> bitangent_accum(primitive.vertices.size(),
                                            math::Vec3{0.0F, 0.0F, 0.0F});
    for (std::size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
        const std::uint32_t i0 = primitive.indices[i + 0];
        const std::uint32_t i1 = primitive.indices[i + 1];
        const std::uint32_t i2 = primitive.indices[i + 2];
        if (i0 >= primitive.vertices.size() || i1 >= primitive.vertices.size() ||
            i2 >= primitive.vertices.size()) {
            throw gltf_error("primitive index is out of vertex range");
        }
        const GltfVertex& v0 = primitive.vertices[i0];
        const GltfVertex& v1 = primitive.vertices[i1];
        const GltfVertex& v2 = primitive.vertices[i2];
        const math::Vec3 edge1 = v1.position - v0.position;
        const math::Vec3 edge2 = v2.position - v0.position;
        const math::Vec2 delta_uv1 =
            tangent_texcoord(v1, texcoord_set) - tangent_texcoord(v0, texcoord_set);
        const math::Vec2 delta_uv2 =
            tangent_texcoord(v2, texcoord_set) - tangent_texcoord(v0, texcoord_set);
        const float determinant = delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x;
        if (std::abs(determinant) < 1.0e-6F) {
            continue;
        }
        const math::Vec3 tangent = (edge1 * delta_uv2.y - edge2 * delta_uv1.y) / determinant;
        const math::Vec3 bitangent = (edge2 * delta_uv1.x - edge1 * delta_uv2.x) / determinant;
        tangent_accum[i0] += tangent;
        tangent_accum[i1] += tangent;
        tangent_accum[i2] += tangent;
        bitangent_accum[i0] += bitangent;
        bitangent_accum[i1] += bitangent;
        bitangent_accum[i2] += bitangent;
    }

    for (std::size_t i = 0; i < primitive.vertices.size(); ++i) {
        const math::Vec3 normal = primitive.vertices[i].normal;
        math::Vec3 tangent = tangent_accum[i] - normal * glm::dot(normal, tangent_accum[i]);
        const bool has_triangle_tangent = glm::length(tangent) >= 1.0e-6F;
        if (!has_triangle_tangent) {
            tangent = std::abs(normal.y) < 0.9F ? glm::cross(normal, math::Vec3{0.0F, 1.0F, 0.0F})
                                                : glm::cross(normal, math::Vec3{1.0F, 0.0F, 0.0F});
        }
        tangent = glm::normalize(tangent);
        float handedness = 1.0F;
        if (has_triangle_tangent && glm::length(bitangent_accum[i]) >= 1.0e-6F) {
            // glTF normal maps use +Y up while UV V starts at the top of the image. Emit the
            // handedness that makes the shader's cross(normal, tangent) basis match that
            // convention.
            handedness =
                glm::dot(glm::cross(normal, tangent), bitangent_accum[i]) < 0.0F ? 1.0F : -1.0F;
        }
        primitive.vertices[i].tangent = {tangent.x, tangent.y, tangent.z, handedness};
    }
}

[[nodiscard]] math::Vec3 triangle_normal(const GltfVertex& v0, const GltfVertex& v1,
                                         const GltfVertex& v2) {
    const math::Vec3 edge1 = v1.position - v0.position;
    const math::Vec3 edge2 = v2.position - v0.position;
    const math::Vec3 normal = glm::cross(edge1, edge2);
    const float length = glm::length(normal);
    if (length < 1.0e-6F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return normal / length;
}

void remap_morph_values(std::vector<math::Vec3>& values,
                        std::span<const std::uint32_t> source_indices, const char* label) {
    if (values.empty()) {
        return;
    }

    std::vector<math::Vec3> remapped;
    remapped.reserve(source_indices.size());
    for (const std::uint32_t source_index : source_indices) {
        if (source_index >= values.size()) {
            throw gltf_error(std::string(label) + " morph target source index is out of range");
        }
        remapped.push_back(values[source_index]);
    }
    values = std::move(remapped);
}

void generate_flat_normals(GltfMeshPrimitive& primitive) {
    if (primitive.indices.size() % 3 != 0) {
        throw gltf_error("triangle primitive index count must be divisible by 3");
    }

    std::vector<GltfVertex> expanded_vertices;
    std::vector<std::uint32_t> expanded_indices;
    std::vector<std::uint32_t> source_indices;
    expanded_vertices.reserve(primitive.indices.size());
    expanded_indices.reserve(primitive.indices.size());
    source_indices.reserve(primitive.indices.size());

    for (std::size_t i = 0; i < primitive.indices.size(); i += 3) {
        const std::uint32_t i0 = primitive.indices[i + 0];
        const std::uint32_t i1 = primitive.indices[i + 1];
        const std::uint32_t i2 = primitive.indices[i + 2];
        if (i0 >= primitive.vertices.size() || i1 >= primitive.vertices.size() ||
            i2 >= primitive.vertices.size()) {
            throw gltf_error("primitive index is out of vertex range");
        }

        const math::Vec3 normal =
            triangle_normal(primitive.vertices[i0], primitive.vertices[i1], primitive.vertices[i2]);
        for (const std::uint32_t source_index : {i0, i1, i2}) {
            GltfVertex vertex = primitive.vertices[source_index];
            vertex.normal = normal;
            expanded_vertices.push_back(vertex);
            const std::size_t expanded_index = expanded_vertices.size() - 1;
            if (expanded_index > std::numeric_limits<std::uint32_t>::max()) {
                throw gltf_error("expanded primitive vertex count is out of range");
            }
            expanded_indices.push_back(static_cast<std::uint32_t>(expanded_index));
            source_indices.push_back(source_index);
        }
    }

    for (GltfMorphTarget& target : primitive.morph_targets) {
        remap_morph_values(target.position_deltas, source_indices, "POSITION");
        remap_morph_values(target.normal_deltas, source_indices, "NORMAL");
        remap_morph_values(target.tangent_deltas, source_indices, "TANGENT");
    }

    primitive.vertices = std::move(expanded_vertices);
    primitive.indices = std::move(expanded_indices);
}

void require_supported_skin_attributes(const cgltf_primitive& primitive) {
    if (find_attribute(primitive, cgltf_attribute_type_joints, 1) != nullptr ||
        find_attribute(primitive, cgltf_attribute_type_weights, 1) != nullptr) {
        throw gltf_error("JOINTS_1 and WEIGHTS_1 are not supported");
    }
}

void require_supported_morph_target_attributes(const cgltf_morph_target& target) {
    for (cgltf_size i = 0; i < target.attributes_count; ++i) {
        const cgltf_attribute& attribute = target.attributes[i];
        const bool supported =
            attribute.index == 0 && (attribute.type == cgltf_attribute_type_position ||
                                     attribute.type == cgltf_attribute_type_normal ||
                                     attribute.type == cgltf_attribute_type_tangent);
        if (!supported) {
            throw gltf_error("unsupported morph target attribute");
        }
    }
}

[[nodiscard]] std::vector<math::Vec3> read_vec3_accessor_values(const cgltf_accessor* accessor,
                                                                const char* label) {
    require_optional_float_accessor(accessor, cgltf_type_vec3, label);
    if (accessor == nullptr) {
        return {};
    }
    const std::vector<float> unpacked = read_float_accessor_values(accessor, 3, label);
    std::vector<math::Vec3> values;
    values.reserve(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        values.push_back(vec3_at(unpacked, i));
    }
    return values;
}

[[nodiscard]] std::string morph_target_label(char* const* target_names,
                                             cgltf_size target_names_count,
                                             cgltf_size target_index) {
    if (target_index >= target_names_count) {
        return {};
    }
    return label_or_empty(target_names[target_index]);
}

[[nodiscard]] GltfMorphTarget load_morph_target(const cgltf_morph_target& target,
                                                cgltf_size vertex_count, std::string label) {
    require_supported_morph_target_attributes(target);
    const auto attributes =
        std::span<const cgltf_attribute>{target.attributes, target.attributes_count};
    const cgltf_accessor* positions = find_attribute(attributes, cgltf_attribute_type_position);
    const cgltf_accessor* normals = find_attribute(attributes, cgltf_attribute_type_normal);
    const cgltf_accessor* tangents = find_attribute(attributes, cgltf_attribute_type_tangent);
    require_optional_morph_accessor_count(positions, vertex_count, "POSITION");
    require_optional_morph_accessor_count(normals, vertex_count, "NORMAL");
    require_optional_morph_accessor_count(tangents, vertex_count, "TANGENT");
    return {
        .label = std::move(label),
        .position_deltas = read_vec3_accessor_values(positions, "POSITION morph target"),
        .normal_deltas = read_vec3_accessor_values(normals, "NORMAL morph target"),
        .tangent_deltas = read_vec3_accessor_values(tangents, "TANGENT morph target"),
    };
}

[[nodiscard]] std::uint32_t normal_texture_texcoord_set(const cgltf_primitive& primitive) {
    if (primitive.material == nullptr || primitive.material->normal_texture.texture == nullptr) {
        return 0U;
    }
    const cgltf_texture_view& view = primitive.material->normal_texture;
    cgltf_int texcoord = view.texcoord;
    if (view.has_transform != 0 && view.transform.has_texcoord != 0) {
        texcoord = view.transform.texcoord;
    }
    if (texcoord < 0 || texcoord > 1) {
        throw gltf_error("normal texture coordinate sets above TEXCOORD_1 are not supported");
    }
    return static_cast<std::uint32_t>(texcoord);
}

[[nodiscard]] std::uint32_t texture_texcoord_set(const cgltf_texture_view& view,
                                                 const char* texture_label) {
    cgltf_int texcoord = view.texcoord;
    if (view.has_transform != 0 && view.transform.has_texcoord != 0) {
        texcoord = view.transform.texcoord;
    }
    if (texcoord < 0 || texcoord > 1) {
        throw gltf_error(std::string(texture_label) + " texCoord must be TEXCOORD_0 or TEXCOORD_1");
    }
    return static_cast<std::uint32_t>(texcoord);
}

[[nodiscard]] std::uint32_t anisotropy_tangent_texcoord_set(const cgltf_primitive& primitive) {
    if (primitive.material == nullptr || primitive.material->has_anisotropy == 0) {
        return normal_texture_texcoord_set(primitive);
    }

    const cgltf_material& material = *primitive.material;
    const bool has_normal_texture = material.normal_texture.texture != nullptr;
    const bool has_anisotropy_texture = material.anisotropy.anisotropy_texture.texture != nullptr;
    const std::uint32_t normal_texcoord =
        has_normal_texture ? texture_texcoord_set(material.normal_texture, "normalTexture") : 0U;
    const std::uint32_t anisotropy_texcoord =
        has_anisotropy_texture ? texture_texcoord_set(material.anisotropy.anisotropy_texture,
                                                      "KHR_materials_anisotropy anisotropyTexture")
                               : 0U;
    if (has_normal_texture && has_anisotropy_texture && normal_texcoord != anisotropy_texcoord) {
        throw gltf_error("KHR_materials_anisotropy requires matching normalTexture and "
                         "anisotropyTexture texCoord sets when generating TANGENT");
    }
    return has_normal_texture ? normal_texcoord
                              : (has_anisotropy_texture ? anisotropy_texcoord : 0U);
}

void require_anisotropy_tangent_space(const cgltf_primitive& primitive,
                                      const cgltf_accessor* tangents,
                                      const cgltf_accessor* texcoord0,
                                      const cgltf_accessor* texcoord1, const GltfLoadConfig& config,
                                      GltfMeshPrimitive& result) {
    if (primitive.material == nullptr || primitive.material->has_anisotropy == 0 ||
        tangents != nullptr) {
        return;
    }

    if (!config.generate_missing_tangents) {
        throw gltf_error("KHR_materials_anisotropy requires a TANGENT attribute when "
                         "generate_missing_tangents is disabled");
    }
    const std::uint32_t texcoord_set = anisotropy_tangent_texcoord_set(primitive);
    const bool has_texcoords = texcoord_set == 1U ? texcoord1 != nullptr : texcoord0 != nullptr;
    if (!has_texcoords) {
        throw gltf_error(std::string("KHR_materials_anisotropy requires TEXCOORD_") +
                         std::to_string(texcoord_set) + " to generate the required tangent space");
    }
    generate_tangents(result, texcoord_set);
}

void expand_bounds_for_morph_targets(GltfMeshPrimitive& primitive) {
    if (primitive.vertices.empty() || primitive.morph_targets.empty()) {
        return;
    }

    math::Vec3 min_position = primitive.vertices.front().position;
    math::Vec3 max_position = primitive.vertices.front().position;
    const auto add_position = [&](math::Vec3 position) {
        min_position = glm::min(min_position, position);
        max_position = glm::max(max_position, position);
    };

    for (std::size_t vertex_index = 0; vertex_index < primitive.vertices.size(); ++vertex_index) {
        const math::Vec3 base_position = primitive.vertices[vertex_index].position;
        add_position(base_position);
        for (const GltfMorphTarget& target : primitive.morph_targets) {
            if (target.position_deltas.empty()) {
                continue;
            }
            add_position(base_position + target.position_deltas[vertex_index]);
        }
    }

    primitive.local_bounds = {
        .center = (min_position + max_position) * 0.5F,
        .half_extent = (max_position - min_position) * 0.5F,
    };
}

[[nodiscard]] GltfMeshPrimitive load_primitive(const cgltf_primitive& primitive,
                                               const cgltf_material* material_base,
                                               cgltf_size material_count, char* const* target_names,
                                               cgltf_size target_names_count,
                                               const GltfLoadConfig& config) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        throw gltf_error("only triangle primitives are supported");
    }

    const cgltf_accessor* positions = find_attribute(primitive, cgltf_attribute_type_position);
    const cgltf_accessor* normals = find_attribute(primitive, cgltf_attribute_type_normal);
    const cgltf_accessor* tangents = find_attribute(primitive, cgltf_attribute_type_tangent);
    const cgltf_accessor* texcoord0 = find_attribute(primitive, cgltf_attribute_type_texcoord, 0);
    const cgltf_accessor* texcoord1 = find_attribute(primitive, cgltf_attribute_type_texcoord, 1);
    const cgltf_accessor* color0 = find_attribute(primitive, cgltf_attribute_type_color, 0);
    const cgltf_accessor* joints0 = find_attribute(primitive, cgltf_attribute_type_joints, 0);
    const cgltf_accessor* weights0 = find_attribute(primitive, cgltf_attribute_type_weights, 0);

    require_accessor_components(positions, 3, "POSITION");
    if (normals != nullptr) {
        require_accessor_components(normals, 3, "NORMAL");
    } else if (!config.generate_missing_normals) {
        throw gltf_error("primitive is missing required NORMAL attribute");
    }
    if (tangents != nullptr) {
        require_accessor_components(tangents, 4, "TANGENT");
    }
    require_optional_texcoord_accessor(texcoord0, "TEXCOORD_0");
    require_optional_texcoord_accessor(texcoord1, "TEXCOORD_1");
    require_optional_color_accessor(color0, "COLOR_0");
    require_supported_skin_attributes(primitive);
    require_optional_accessor_components(joints0, 4, "JOINTS_0");
    require_optional_accessor_components(weights0, 4, "WEIGHTS_0");
    if ((joints0 == nullptr) != (weights0 == nullptr)) {
        throw gltf_error("JOINTS_0 and WEIGHTS_0 must be provided together");
    }
    if (joints0 != nullptr && (joints0->component_type != cgltf_component_type_r_8u &&
                               joints0->component_type != cgltf_component_type_r_16u)) {
        throw gltf_error("JOINTS_0 must use UNSIGNED_BYTE or UNSIGNED_SHORT components");
    }

    const std::vector<float> position_values = read_float_accessor_values(positions, 3, "POSITION");
    const std::vector<float> normal_values = normals != nullptr
                                                 ? read_float_accessor_values(normals, 3, "NORMAL")
                                                 : std::vector<float>{};
    const std::vector<float> tangent_values =
        tangents != nullptr ? read_float_accessor_values(tangents, 4, "TANGENT")
                            : std::vector<float>{};
    const std::vector<float> texcoord0_values =
        texcoord0 != nullptr ? read_float_accessor_values(texcoord0, 2, "TEXCOORD_0")
                             : std::vector<float>{};
    const std::vector<float> texcoord1_values =
        texcoord1 != nullptr ? read_float_accessor_values(texcoord1, 2, "TEXCOORD_1")
                             : std::vector<float>{};
    const std::vector<math::Vec4> color0_values = read_color_accessor_values(color0, "COLOR_0");
    const std::vector<std::array<std::uint16_t, 4>> joints0_values =
        joints0 != nullptr ? read_u16_vec4_accessor_values(joints0, "JOINTS_0")
                           : std::vector<std::array<std::uint16_t, 4>>{};
    const std::vector<float> weights0_values =
        weights0 != nullptr ? read_float_accessor_values(weights0, 4, "WEIGHTS_0")
                            : std::vector<float>{};

    GltfMeshPrimitive result;
    result.vertices.resize(positions->count);
    for (cgltf_size i = 0; i < positions->count; ++i) {
        result.vertices[i].position = vec3_at(position_values, i);
        if (normals != nullptr) {
            result.vertices[i].normal = glm::normalize(vec3_at(normal_values, i));
        }
        if (tangents != nullptr) {
            result.vertices[i].tangent = vec4_at(tangent_values, i);
        }
        if (texcoord0 != nullptr) {
            result.vertices[i].texcoord0 = vec2_at(texcoord0_values, i);
        }
        if (texcoord1 != nullptr) {
            result.vertices[i].texcoord1 = vec2_at(texcoord1_values, i);
        }
        if (color0 != nullptr) {
            result.vertices[i].color0 = color0_values[i];
        }
        if (joints0 != nullptr) {
            result.vertices[i].joints0 = joints0_values[i];
            result.vertices[i].weights0 = vec4_at(weights0_values, i);
        }
    }

    if (primitive.indices != nullptr) {
        if (primitive.indices->is_sparse != 0) {
            throw gltf_error("primitive index sparse accessors are not supported");
        }
        result.indices.resize(primitive.indices->count);
        for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
            const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
            if (index >= positions->count) {
                throw gltf_error("primitive index is out of range");
            }
            result.indices[i] = static_cast<std::uint32_t>(index);
        }
    } else {
        result.indices.resize(result.vertices.size());
        for (std::size_t i = 0; i < result.indices.size(); ++i) {
            result.indices[i] = static_cast<std::uint32_t>(i);
        }
    }

    result.material_index =
        pointer_index(primitive.material, material_base, material_count, "material");
    if (result.material_index == kInvalidAssetIndex) {
        result.material_index = 0;
    } else {
        ++result.material_index;
    }
    result.morph_targets.reserve(primitive.targets_count);
    for (cgltf_size i = 0; i < primitive.targets_count; ++i) {
        result.morph_targets.push_back(
            load_morph_target(primitive.targets[i], positions->count,
                              morph_target_label(target_names, target_names_count, i)));
    }
    if (normals == nullptr) {
        generate_flat_normals(result);
    }
    if (primitive.material != nullptr && primitive.material->has_anisotropy != 0) {
        require_anisotropy_tangent_space(primitive, tangents, texcoord0, texcoord1, config, result);
    } else {
        const std::uint32_t tangent_texcoord_set = normal_texture_texcoord_set(primitive);
        const bool has_tangent_texcoords =
            tangent_texcoord_set == 1U ? texcoord1 != nullptr : texcoord0 != nullptr;
        if (tangents == nullptr && config.generate_missing_tangents && has_tangent_texcoords) {
            generate_tangents(result, tangent_texcoord_set);
        }
    }
    result.local_bounds = bounds_for_positions(result.vertices);
    expand_bounds_for_morph_targets(result);
    return result;
}

[[nodiscard]] GltfMesh load_mesh(const cgltf_mesh& mesh, const cgltf_material* material_base,
                                 cgltf_size material_count, const GltfLoadConfig& config) {
    GltfMesh result{
        .label = label_or_empty(mesh.name),
    };
    result.primitives.reserve(mesh.primitives_count);
    for (cgltf_size i = 0; i < mesh.primitives_count; ++i) {
        result.primitives.push_back(load_primitive(mesh.primitives[i], material_base,
                                                   material_count, mesh.target_names,
                                                   mesh.target_names_count, config));
    }
    result.weights.reserve(mesh.weights_count);
    for (cgltf_size i = 0; i < mesh.weights_count; ++i) {
        result.weights.push_back(mesh.weights[i]);
    }
    return result;
}

[[nodiscard]] math::Mat4 trs_matrix(math::Vec3 translation, math::Quat rotation, math::Vec3 scale) {
    math::Mat4 matrix{1.0F};
    matrix = glm::translate(matrix, translation);
    matrix *= glm::mat4_cast(rotation);
    matrix = glm::scale(matrix, scale);
    return matrix;
}

struct MetadataBoundsAccumulator {
    math::Vec3 min{0.0F};
    math::Vec3 max{0.0F};
    bool has_value = false;

    void add(math::Vec3 value) {
        if (!has_value) {
            min = value;
            max = value;
            has_value = true;
            return;
        }
        min = glm::min(min, value);
        max = glm::max(max, value);
    }

    void add_bounds(math::Vec3 local_min, math::Vec3 local_max, const math::Mat4& transform,
                    const char* label) {
        for (std::uint32_t corner = 0U; corner < 8U; ++corner) {
            const math::Vec3 point{
                (corner & 1U) != 0U ? local_max.x : local_min.x,
                (corner & 2U) != 0U ? local_max.y : local_min.y,
                (corner & 4U) != 0U ? local_max.z : local_min.z,
            };
            const math::Vec4 transformed = transform * math::Vec4{point, 1.0F};
            if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) ||
                !std::isfinite(transformed.z) || !std::isfinite(transformed.w) ||
                std::abs(transformed.w) < 1.0e-6F) {
                throw gltf_error(std::string(label) + " transform produces invalid bounds");
            }
            add({transformed.x / transformed.w, transformed.y / transformed.w,
                 transformed.z / transformed.w});
        }
    }

    [[nodiscard]] GltfBounds3D bounds_or_default() const {
        if (!has_value) {
            return {
                .center = {0.0F, 0.0F, 0.0F},
                .half_extent = {1.0F, 1.0F, 1.0F},
            };
        }
        return {
            .center = (min + max) * 0.5F,
            .half_extent = (max - min) * 0.5F,
        };
    }
};

[[nodiscard]] math::Mat4 metadata_node_local_matrix(const cgltf_node& node) {
    const math::Vec3 translation =
        node.has_translation != 0
            ? math::Vec3{node.translation[0], node.translation[1], node.translation[2]}
            : math::Vec3{0.0F};
    const math::Quat rotation =
        node.has_rotation != 0
            ? math::Quat{node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]}
            : math::Quat{1.0F, 0.0F, 0.0F, 0.0F};
    const math::Vec3 scale = node.has_scale != 0
                                 ? math::Vec3{node.scale[0], node.scale[1], node.scale[2]}
                                 : math::Vec3{1.0F};
    if (node.has_matrix == 0) {
        return trs_matrix(translation, rotation, scale);
    }

    math::Mat4 matrix{1.0F};
    std::memcpy(&matrix[0][0], node.matrix, sizeof(node.matrix));
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                throw gltf_error("node matrix contains a non-finite value");
            }
        }
    }
    return matrix;
}

[[nodiscard]] const cgltf_accessor* metadata_position_accessor(const cgltf_primitive& primitive,
                                                               const cgltf_accessor* accessor_base,
                                                               cgltf_size accessor_count) {
    if (primitive.attributes == nullptr) {
        throw gltf_error("mesh primitive has no attributes");
    }
    for (cgltf_size index = 0; index < primitive.attributes_count; ++index) {
        const cgltf_attribute& attribute = primitive.attributes[index];
        if (attribute.type != cgltf_attribute_type_position) {
            continue;
        }
        if (attribute.data == nullptr) {
            throw gltf_error("POSITION attribute has no accessor");
        }
        static_cast<void>(
            pointer_index(attribute.data, accessor_base, accessor_count, "POSITION accessor"));
        return attribute.data;
    }
    throw gltf_error("mesh primitive is missing required POSITION attribute");
}

void accumulate_metadata_node_bounds(const cgltf_node* node, const cgltf_node* node_base,
                                     cgltf_size node_count, const cgltf_mesh* mesh_base,
                                     cgltf_size mesh_count, const cgltf_accessor* accessor_base,
                                     cgltf_size accessor_count, const math::Mat4& parent_world,
                                     std::vector<std::uint8_t>& visited,
                                     MetadataBoundsAccumulator& accumulator) {
    if (node == nullptr) {
        throw gltf_error("scene contains a null node");
    }
    const std::uint32_t node_index = pointer_index(node, node_base, node_count, "scene node");
    if (visited.at(node_index) != 0U) {
        return;
    }
    visited[node_index] = 1U;
    const math::Mat4 world = parent_world * metadata_node_local_matrix(*node);

    if (node->mesh != nullptr) {
        const std::uint32_t mesh_index = pointer_index(node->mesh, mesh_base, mesh_count, "mesh");
        const cgltf_mesh& mesh = mesh_base[mesh_index];
        if (mesh.primitives == nullptr && mesh.primitives_count != 0U) {
            throw gltf_error("mesh has no primitive metadata");
        }
        for (cgltf_size primitive_index = 0; primitive_index < mesh.primitives_count;
             ++primitive_index) {
            const cgltf_primitive& primitive = mesh.primitives[primitive_index];
            const cgltf_accessor* position =
                metadata_position_accessor(primitive, accessor_base, accessor_count);
            if (position->type != cgltf_type_vec3 ||
                position->component_type != cgltf_component_type_r_32f || position->count == 0U) {
                throw gltf_error("POSITION accessor must be a non-empty FLOAT VEC3");
            }
            if (position->has_min == 0 || position->has_max == 0) {
                throw gltf_error("POSITION accessor is missing required min/max bounds");
            }
            const math::Vec3 local_min{position->min[0], position->min[1], position->min[2]};
            const math::Vec3 local_max{position->max[0], position->max[1], position->max[2]};
            for (const float value :
                 {local_min.x, local_min.y, local_min.z, local_max.x, local_max.y, local_max.z}) {
                if (!std::isfinite(value)) {
                    throw gltf_error("POSITION accessor min/max contains a non-finite value");
                }
            }
            if (glm::any(glm::lessThan(local_max, local_min))) {
                throw gltf_error("POSITION accessor min/max bounds are inverted");
            }
            accumulator.add_bounds(local_min, local_max, world, "POSITION accessor");
        }
    }

    if (node->children == nullptr && node->children_count != 0U) {
        throw gltf_error("scene node has no child metadata");
    }
    for (cgltf_size child_index = 0; child_index < node->children_count; ++child_index) {
        accumulate_metadata_node_bounds(node->children[child_index], node_base, node_count,
                                        mesh_base, mesh_count, accessor_base, accessor_count, world,
                                        visited, accumulator);
    }
}

[[nodiscard]] GltfNode load_node(const cgltf_node& node, const cgltf_node* node_base,
                                 cgltf_size node_count, const cgltf_mesh* mesh_base,
                                 cgltf_size mesh_count, const cgltf_skin* skin_base,
                                 cgltf_size skin_count) {
    GltfNode result{
        .label = label_or_empty(node.name),
    };

    if (node.has_translation) {
        result.translation = {node.translation[0], node.translation[1], node.translation[2]};
    }
    if (node.has_rotation) {
        result.rotation = {node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]};
    }
    if (node.has_scale) {
        result.scale = {node.scale[0], node.scale[1], node.scale[2]};
    }
    result.local_matrix = trs_matrix(result.translation, result.rotation, result.scale);

    if (node.has_matrix) {
        math::Mat4 matrix{1.0F};
        std::memcpy(&matrix[0][0], node.matrix, sizeof(node.matrix));
        result.has_matrix = true;
        result.local_matrix = matrix;

        math::Vec3 skew{};
        math::Vec4 perspective{};
        glm::decompose(matrix, result.scale, result.rotation, result.translation, skew,
                       perspective);
    }

    result.mesh_index = pointer_index(node.mesh, mesh_base, mesh_count, "mesh");
    result.skin_index = pointer_index(node.skin, skin_base, skin_count, "skin");
    result.weights.reserve(node.weights_count);
    for (cgltf_size i = 0; i < node.weights_count; ++i) {
        result.weights.push_back(node.weights[i]);
    }
    result.children.reserve(node.children_count);
    for (cgltf_size i = 0; i < node.children_count; ++i) {
        result.children.push_back(pointer_index(node.children[i], node_base, node_count, "node"));
    }
    return result;
}

[[nodiscard]] GltfSkin load_skin(const cgltf_skin& skin, const cgltf_skin* skin_base,
                                 cgltf_size skin_count, const cgltf_node* node_base,
                                 cgltf_size node_count) {
    static_cast<void>(skin_base);
    static_cast<void>(skin_count);
    GltfSkin result{
        .label = label_or_empty(skin.name),
        .skeleton_node_index = pointer_index(skin.skeleton, node_base, node_count, "node"),
    };

    result.joints.reserve(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        result.joints.push_back(pointer_index(skin.joints[i], node_base, node_count, "node"));
    }

    if (skin.inverse_bind_matrices != nullptr) {
        require_float_accessor(skin.inverse_bind_matrices, cgltf_type_mat4, "inverseBindMatrices");
        if (skin.inverse_bind_matrices->count != skin.joints_count) {
            throw gltf_error("inverseBindMatrices count must match skin joint count");
        }
        result.inverse_bind_matrices.reserve(skin.inverse_bind_matrices->count);
        for (cgltf_size i = 0; i < skin.inverse_bind_matrices->count; ++i) {
            result.inverse_bind_matrices.push_back(read_mat4(skin.inverse_bind_matrices, i));
        }
    } else {
        result.inverse_bind_matrices.resize(skin.joints_count, math::Mat4{1.0F});
    }
    return result;
}

[[nodiscard]] GltfScene load_scene(const cgltf_scene& scene, const cgltf_node* node_base,
                                   cgltf_size node_count) {
    GltfScene result{
        .label = label_or_empty(scene.name),
    };
    result.root_nodes.reserve(scene.nodes_count);
    for (cgltf_size i = 0; i < scene.nodes_count; ++i) {
        result.root_nodes.push_back(pointer_index(scene.nodes[i], node_base, node_count, "node"));
    }
    return result;
}

[[nodiscard]] GltfAnimationInterpolation
load_animation_interpolation(cgltf_interpolation_type interpolation) {
    switch (interpolation) {
    case cgltf_interpolation_type_step:
        return GltfAnimationInterpolation::Step;
    case cgltf_interpolation_type_cubic_spline:
        return GltfAnimationInterpolation::CubicSpline;
    case cgltf_interpolation_type_linear:
    default:
        return GltfAnimationInterpolation::Linear;
    }
}

[[nodiscard]] GltfAnimationTargetPath load_animation_target_path(cgltf_animation_path_type path) {
    switch (path) {
    case cgltf_animation_path_type_translation:
        return GltfAnimationTargetPath::Translation;
    case cgltf_animation_path_type_rotation:
        return GltfAnimationTargetPath::Rotation;
    case cgltf_animation_path_type_scale:
        return GltfAnimationTargetPath::Scale;
    case cgltf_animation_path_type_weights:
        return GltfAnimationTargetPath::Weights;
    case cgltf_animation_path_type_invalid:
    default:
        throw gltf_error("unsupported animation target path");
    }
}

[[nodiscard]] bool normalized_signed_animation_accessor(const cgltf_accessor* accessor) noexcept {
    return accessor != nullptr && accessor->normalized != 0 &&
           (accessor->component_type == cgltf_component_type_r_8 ||
            accessor->component_type == cgltf_component_type_r_16);
}

[[nodiscard]] bool normalized_unsigned_animation_accessor(const cgltf_accessor* accessor) noexcept {
    return accessor != nullptr && accessor->normalized != 0 &&
           (accessor->component_type == cgltf_component_type_r_8u ||
            accessor->component_type == cgltf_component_type_r_16u);
}

void require_animation_output_component_type(const cgltf_accessor* accessor,
                                             GltfAnimationTargetPath path) {
    if (accessor->component_type == cgltf_component_type_r_32f) {
        return;
    }
    if (path == GltfAnimationTargetPath::Rotation &&
        normalized_signed_animation_accessor(accessor)) {
        return;
    }
    if (path == GltfAnimationTargetPath::Weights &&
        normalized_unsigned_animation_accessor(accessor)) {
        return;
    }
    throw gltf_error("animation output accessor has unsupported component type for target path");
}

[[nodiscard]] cgltf_size animation_output_factor(GltfAnimationInterpolation interpolation) {
    return interpolation == GltfAnimationInterpolation::CubicSpline ? 3U : 1U;
}

void require_animation_output_shape(const cgltf_animation_sampler& source,
                                    GltfAnimationInterpolation interpolation,
                                    GltfAnimationTargetPath path) {
    if (source.output == nullptr) {
        throw gltf_error("animation output accessor is missing");
    }
    require_animation_output_component_type(source.output, path);
    const cgltf_size input_count = source.input != nullptr ? source.input->count : 0U;
    const cgltf_size factor = animation_output_factor(interpolation);
    const cgltf_size output_count = source.output->count;
    if (input_count == 0U) {
        throw gltf_error("animation input accessor must contain at least one key");
    }
    switch (path) {
    case GltfAnimationTargetPath::Translation:
    case GltfAnimationTargetPath::Scale:
        if (source.output->type != cgltf_type_vec3 || output_count != input_count * factor) {
            throw gltf_error("animation translation/scale output must be a matching VEC3 accessor");
        }
        return;
    case GltfAnimationTargetPath::Rotation:
        if (source.output->type != cgltf_type_vec4 || output_count != input_count * factor) {
            throw gltf_error("animation rotation output must be a matching VEC4 accessor");
        }
        return;
    case GltfAnimationTargetPath::Weights:
        if (source.output->type != cgltf_type_scalar || output_count == 0U ||
            output_count % (input_count * factor) != 0U) {
            throw gltf_error("animation weights output must be matching scalar samples");
        }
        return;
    }
    throw gltf_error("unsupported animation target path");
}

[[nodiscard]] GltfAnimationSampler load_animation_sampler(const cgltf_animation_sampler& sampler,
                                                          GltfAnimationTargetPath target_path) {
    require_float_accessor(sampler.input, cgltf_type_scalar, "animation input");
    const GltfAnimationInterpolation interpolation =
        load_animation_interpolation(sampler.interpolation);
    require_animation_output_shape(sampler, interpolation, target_path);
    const cgltf_size component_count = cgltf_num_components(sampler.output->type);
    if (component_count == 0) {
        throw gltf_error("animation output accessor has unsupported type");
    }
    if (component_count > std::numeric_limits<std::uint32_t>::max()) {
        throw gltf_error("animation output component count is out of range");
    }

    return {
        .interpolation = interpolation,
        .input_times = read_float_accessor_values(sampler.input, 1, "animation input"),
        .output_values =
            read_float_accessor_values(sampler.output, component_count, "animation output"),
        .component_count = static_cast<std::uint32_t>(component_count),
    };
}

[[nodiscard]] GltfAnimation load_animation(const cgltf_animation& animation,
                                           const cgltf_node* node_base, cgltf_size node_count) {
    GltfAnimation result{
        .label = label_or_empty(animation.name),
    };

    std::vector<std::optional<GltfAnimationTargetPath>> sampler_targets(animation.samplers_count);
    for (cgltf_size i = 0; i < animation.channels_count; ++i) {
        const cgltf_animation_channel& channel = animation.channels[i];
        const std::uint32_t sampler_index = pointer_index(
            channel.sampler, animation.samplers, animation.samplers_count, "animation sampler");
        const GltfAnimationTargetPath target_path = load_animation_target_path(channel.target_path);
        std::optional<GltfAnimationTargetPath>& prior = sampler_targets[sampler_index];
        if (prior.has_value()) {
            const bool compatible = prior.value() == target_path ||
                                    ((prior.value() == GltfAnimationTargetPath::Translation ||
                                      prior.value() == GltfAnimationTargetPath::Scale) &&
                                     (target_path == GltfAnimationTargetPath::Translation ||
                                      target_path == GltfAnimationTargetPath::Scale));
            if (!compatible) {
                throw gltf_error("animation sampler is shared across incompatible target paths");
            }
            continue;
        }
        prior = target_path;
    }

    result.samplers.reserve(animation.samplers_count);
    for (cgltf_size i = 0; i < animation.samplers_count; ++i) {
        if (!sampler_targets[i].has_value()) {
            throw gltf_error("animation sampler is not referenced by any channel");
        }
        GltfAnimationSampler sampler =
            load_animation_sampler(animation.samplers[i], sampler_targets[i].value());
        if (!sampler.input_times.empty()) {
            result.duration_seconds =
                std::max(result.duration_seconds,
                         *std::max_element(sampler.input_times.begin(), sampler.input_times.end()));
        }
        result.samplers.push_back(std::move(sampler));
    }

    result.channels.reserve(animation.channels_count);
    for (cgltf_size i = 0; i < animation.channels_count; ++i) {
        const cgltf_animation_channel& channel = animation.channels[i];
        result.channels.push_back(GltfAnimationChannel{
            .sampler_index = pointer_index(channel.sampler, animation.samplers,
                                           animation.samplers_count, "animation sampler"),
            .node_index = pointer_index(channel.target_node, node_base, node_count, "node"),
            .target_path = load_animation_target_path(channel.target_path),
        });
    }
    return result;
}

[[nodiscard]] bool supports_required_extension(std::string_view extension) noexcept {
    // This is intentionally stricter than the set of extensions that the
    // importer can parse. An extension is accepted from extensionsRequired
    // only after its data path and rendered semantics have both been closed.
    static constexpr std::array<std::string_view, 13> kSupportedRequiredExtensions{
        "KHR_materials_emissive_strength",
        "KHR_materials_ior",
        "KHR_materials_specular",
        "KHR_materials_clearcoat",
        "KHR_materials_anisotropy",
        "KHR_materials_iridescence",
        "KHR_materials_sheen",
        "KHR_texture_transform",
        "KHR_texture_basisu",
        "KHR_materials_unlit",
        "KHR_materials_transmission",
        "KHR_materials_volume",
        "KHR_materials_dispersion",
    };
    return std::ranges::find(kSupportedRequiredExtensions, extension) !=
           kSupportedRequiredExtensions.end();
}

void reject_unsupported_features(const cgltf_data& data) {
    for (cgltf_size i = 0; i < data.extensions_required_count; ++i) {
        const std::string_view extension = data.extensions_required[i] != nullptr
                                               ? std::string_view{data.extensions_required[i]}
                                               : std::string_view{};
        if (!supports_required_extension(extension)) {
            throw gltf_error("required glTF extension is not supported: " + std::string(extension));
        }
    }
}

struct CgltfDataDeleter {
    void operator()(cgltf_data* data) const noexcept {
        cgltf_free(data);
    }
};

using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDataDeleter>;

constexpr std::uint32_t kGlbMagic = 0x46546C67U;
constexpr std::uint32_t kGlbVersion = 2U;
constexpr std::uint32_t kGlbJsonChunkMagic = 0x4E4F534AU;
constexpr std::uint64_t kGlbHeaderSize = 12U;
constexpr std::uint64_t kGlbChunkHeaderSize = 8U;

[[nodiscard]] std::uint32_t read_little_endian_u32(std::span<const std::uint8_t> bytes,
                                                   std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t)) {
        throw gltf_error("GLB header or chunk header is truncated");
    }
    return static_cast<std::uint32_t>(bytes[offset + 0U]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] bool has_glb_extension(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    return extension == ".glb" || extension == ".GLB";
}

[[nodiscard]] std::uint64_t probe_file_size(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        throw gltf_error("failed to stat " + path.string());
    }
    if (size > std::numeric_limits<std::uint64_t>::max()) {
        throw gltf_error("file size overflows GLB metadata limits for " + path.string());
    }
    return static_cast<std::uint64_t>(size);
}

[[nodiscard]] std::vector<std::uint8_t> read_probe_region(std::ifstream& file,
                                                          const std::filesystem::path& path,
                                                          std::uint64_t offset, std::uint64_t size,
                                                          const char* description) {
    if (size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        throw gltf_error(std::string(description) + " is too large for " + path.string());
    }
    if (offset > std::numeric_limits<std::uint64_t>::max() - size ||
        offset + size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw gltf_error(std::string(description) + " offset overflows for " + path.string());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (bytes.empty()) {
        return bytes;
    }

    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        throw gltf_error("failed to seek to " + std::string(description) + " in " + path.string());
    }
    const std::streamsize expected = static_cast<std::streamsize>(size);
    file.read(reinterpret_cast<char*>(bytes.data()), expected);
    if (file.gcount() != expected) {
        throw gltf_error("truncated " + std::string(description) + " in " + path.string());
    }
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> read_gltf_json_payload(const std::filesystem::path& path) {
    const std::uint64_t file_size = probe_file_size(path);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw gltf_error("failed to open " + path.string());
    }

    const bool extension_is_glb = has_glb_extension(path);
    bool magic_is_glb = false;
    if (file_size >= sizeof(std::uint32_t)) {
        const std::vector<std::uint8_t> prefix =
            read_probe_region(file, path, 0U, sizeof(std::uint32_t), "GLB magic");
        magic_is_glb = read_little_endian_u32(prefix, 0U) == kGlbMagic;
    } else if (extension_is_glb) {
        throw gltf_error("GLB header is truncated for " + path.string());
    }

    if (!extension_is_glb && !magic_is_glb) {
        return read_probe_region(file, path, 0U, file_size, "glTF JSON");
    }
    if (file_size < kGlbHeaderSize) {
        throw gltf_error("GLB header is truncated for " + path.string());
    }

    const std::vector<std::uint8_t> header =
        read_probe_region(file, path, 0U, kGlbHeaderSize, "GLB header");
    if (read_little_endian_u32(header, 0U) != kGlbMagic) {
        throw gltf_error("invalid GLB magic for " + path.string());
    }
    if (read_little_endian_u32(header, 4U) != kGlbVersion) {
        throw gltf_error("unsupported GLB version for " + path.string());
    }
    const std::uint64_t declared_length = read_little_endian_u32(header, 8U);
    if (declared_length != file_size) {
        throw gltf_error("GLB declared length does not match file size for " + path.string());
    }
    if (declared_length < kGlbHeaderSize + kGlbChunkHeaderSize || declared_length % 4U != 0U) {
        throw gltf_error("GLB declared length is invalid for " + path.string());
    }

    const std::vector<std::uint8_t> json_header =
        read_probe_region(file, path, kGlbHeaderSize, kGlbChunkHeaderSize, "GLB JSON chunk header");
    const std::uint32_t json_length = read_little_endian_u32(json_header, 0U);
    if (read_little_endian_u32(json_header, 4U) != kGlbJsonChunkMagic) {
        throw gltf_error("GLB first chunk is not a JSON chunk for " + path.string());
    }
    if (json_length == 0U || json_length % 4U != 0U) {
        throw gltf_error("GLB JSON chunk length is invalid for " + path.string());
    }
    const std::uint64_t json_offset = kGlbHeaderSize + kGlbChunkHeaderSize;
    const std::uint64_t json_end = json_offset + static_cast<std::uint64_t>(json_length);
    if (json_end < json_offset || json_end > declared_length) {
        throw gltf_error("GLB JSON chunk exceeds the declared length for " + path.string());
    }
    const std::uint64_t trailing_bytes = declared_length - json_end;
    if (trailing_bytes != 0U && trailing_bytes < kGlbChunkHeaderSize) {
        throw gltf_error("GLB trailing chunk header is truncated for " + path.string());
    }

    // Deliberately read only the JSON payload. The BIN chunk header/payload
    // and any bytes after it remain untouched; cgltf receives a JSON-only
    // document and validation uses the declared buffer-view sizes.
    return read_probe_region(file, path, json_offset, json_length, "GLB JSON chunk");
}

} // namespace

GltfAsset load_gltf_asset(const std::filesystem::path& path, GltfLoadConfig config,
                          GltfAssetLoadProfile* profile) {
    if (profile != nullptr) {
        *profile = {};
    }
    cgltf_options options{};
    cgltf_data* raw_data = nullptr;
    const std::string path_string = path.string();
    cgltf_result result = cgltf_result_success;
    {
        const ScopedLoadProfilePhase document_parse(
            profile, &GltfAssetLoadProfile::document_parse_milliseconds);
        result = cgltf_parse_file(&options, path_string.c_str(), &raw_data);
    }
    if (result != cgltf_result_success) {
        throw gltf_error("failed to parse " + path_string);
    }
    CgltfDataPtr data(raw_data);

    {
        const ScopedLoadProfilePhase buffer_load(profile,
                                                 &GltfAssetLoadProfile::buffer_load_milliseconds);
        result = cgltf_load_buffers(&options, data.get(), path_string.c_str());
    }
    if (result != cgltf_result_success) {
        throw gltf_error("failed to load buffers for " + path_string);
    }

    {
        const ScopedLoadProfilePhase asset_validate(
            profile, &GltfAssetLoadProfile::asset_validate_milliseconds);
        result = cgltf_validate(data.get());
        if (result != cgltf_result_success) {
            throw gltf_error("validation failed for " + path_string);
        }
        reject_unsupported_features(*data);
    }

    const ScopedAssetAssemblyProfile asset_assembly(profile);
    GltfAsset asset{
        .source_path = path,
    };

    asset.samplers.reserve(data->samplers_count);
    for (cgltf_size i = 0; i < data->samplers_count; ++i) {
        asset.samplers.push_back(load_sampler(data->samplers[i]));
    }

    asset.images.reserve(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        asset.images.push_back(decode_image(data->images[i], path, profile));
    }

    asset.textures.reserve(data->textures_count);
    for (cgltf_size i = 0; i < data->textures_count; ++i) {
        const cgltf_texture& texture = data->textures[i];
        const cgltf_image* image = texture.has_basisu ? texture.basisu_image : texture.image;
        asset.textures.push_back(GltfTexture{
            .label = label_or_empty(texture.name),
            .image_index = pointer_index(image, data->images, data->images_count, "image"),
            .sampler_index =
                pointer_index(texture.sampler, data->samplers, data->samplers_count, "sampler"),
        });
    }

    asset.materials.reserve(data->materials_count + 1);
    asset.materials.push_back(default_material());
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        asset.materials.push_back(
            load_material(data->materials[i], data->textures, data->textures_count));
    }

    asset.meshes.reserve(data->meshes_count);
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        asset.meshes.push_back(
            load_mesh(data->meshes[i], data->materials, data->materials_count, config));
    }

    asset.nodes.reserve(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        asset.nodes.push_back(load_node(data->nodes[i], data->nodes, data->nodes_count,
                                        data->meshes, data->meshes_count, data->skins,
                                        data->skins_count));
    }

    asset.skins.reserve(data->skins_count);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        asset.skins.push_back(load_skin(data->skins[i], data->skins, data->skins_count, data->nodes,
                                        data->nodes_count));
    }

    asset.scenes.reserve(data->scenes_count);
    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        asset.scenes.push_back(load_scene(data->scenes[i], data->nodes, data->nodes_count));
    }

    asset.animations.reserve(data->animations_count);
    for (cgltf_size i = 0; i < data->animations_count; ++i) {
        asset.animations.push_back(
            load_animation(data->animations[i], data->nodes, data->nodes_count));
    }

    asset.default_scene = pointer_index(data->scene, data->scenes, data->scenes_count, "scene");
    if (asset.default_scene == kInvalidAssetIndex && !asset.scenes.empty()) {
        asset.default_scene = 0;
    }
    return asset;
}

GltfBounds3D probe_gltf_scene_bounds(const std::filesystem::path& path, std::uint32_t scene_index) {
    cgltf_options options{};
    options.type = cgltf_file_type_gltf;
    cgltf_data* raw_data = nullptr;
    const std::string path_string = path.string();
    const std::vector<std::uint8_t> json = read_gltf_json_payload(path);
    cgltf_result result = cgltf_parse(&options, json.data(), json.size(), &raw_data);
    if (result != cgltf_result_success) {
        throw gltf_error("failed to parse " + path_string);
    }
    CgltfDataPtr data(raw_data);

    // Validation only inspects the parsed JSON metadata and declared
    // buffer-view sizes. The private reader above intentionally supplies no
    // external or embedded BIN bytes, so this probe never loads buffers,
    // images, or material payloads before the first windowed frame.
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        throw gltf_error("validation failed for " + path_string);
    }

    std::uint32_t resolved_scene_index = scene_index;
    if (resolved_scene_index == kInvalidAssetIndex) {
        resolved_scene_index =
            pointer_index(data->scene, data->scenes, data->scenes_count, "scene");
        if (resolved_scene_index == kInvalidAssetIndex && data->scenes_count != 0U) {
            resolved_scene_index = 0U;
        }
    }
    if (resolved_scene_index == kInvalidAssetIndex) {
        return MetadataBoundsAccumulator{}.bounds_or_default();
    }
    if (resolved_scene_index >= data->scenes_count || data->scenes == nullptr) {
        throw gltf_error("scene index is out of range");
    }

    const cgltf_scene& scene = data->scenes[resolved_scene_index];
    if (scene.nodes == nullptr && scene.nodes_count != 0U) {
        throw gltf_error("scene has no root node metadata");
    }
    std::vector<std::uint8_t> visited(data->nodes_count, 0U);
    MetadataBoundsAccumulator accumulator;
    for (cgltf_size root_index = 0; root_index < scene.nodes_count; ++root_index) {
        accumulate_metadata_node_bounds(scene.nodes[root_index], data->nodes, data->nodes_count,
                                        data->meshes, data->meshes_count, data->accessors,
                                        data->accessors_count, math::Mat4{1.0F}, visited,
                                        accumulator);
    }
    return accumulator.bounds_or_default();
}

const char* gltf_alpha_mode_name(GltfAlphaMode mode) noexcept {
    switch (mode) {
    case GltfAlphaMode::Mask:
        return "MASK";
    case GltfAlphaMode::Blend:
        return "BLEND";
    case GltfAlphaMode::Opaque:
    default:
        return "OPAQUE";
    }
}

GltfTextureColorSpace gltf_texture_color_space_for_base_color() noexcept {
    return GltfTextureColorSpace::Srgb;
}

GltfTextureColorSpace
gltf_texture_color_space_for_material_slot(const GltfTextureRef& texture,
                                           GltfTextureColorSpace default_space) noexcept {
    return texture.has_value() ? default_space : GltfTextureColorSpace::Linear;
}

} // namespace cubey::asset
