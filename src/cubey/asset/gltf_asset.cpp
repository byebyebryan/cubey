#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

    assemble_gltf_material_data(asset, *data, path, profile);

    assemble_gltf_geometry_data(asset, *data, config);

    assemble_gltf_scene_data(asset, *data);

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
