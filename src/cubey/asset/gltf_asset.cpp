#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

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

    assemble_gltf_material_data(asset, *data, path, profile);

    assemble_gltf_geometry_data(asset, *data, config);

    assemble_gltf_scene_data(asset, *data);

    assemble_gltf_animation_data(asset, *data);

    asset.default_scene = pointer_index(data->scene, data->scenes, data->scenes_count, "scene");
    if (asset.default_scene == kInvalidAssetIndex && !asset.scenes.empty()) {
        asset.default_scene = 0;
    }
    return asset;
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
