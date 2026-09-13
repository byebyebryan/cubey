#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cubey::asset {
using namespace gltf_internal;
namespace {

[[nodiscard]] std::uint32_t checked_probe_index(std::ptrdiff_t index, const char* label) {
    if (index < 0 || static_cast<std::uint64_t>(index) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw gltf_error(std::string(label) + " index is out of range");
    }
    return static_cast<std::uint32_t>(index);
}

template <typename T>
[[nodiscard]] std::uint32_t probe_pointer_index(const T* pointer, const T* base, cgltf_size count,
                                                const char* label) {
    if (pointer == nullptr) {
        return kInvalidAssetIndex;
    }
    if (base == nullptr || pointer < base || pointer >= base + count) {
        throw gltf_error(std::string(label) + " pointer is outside the glTF data");
    }
    return checked_probe_index(pointer - base, label);
}

[[nodiscard]] math::Mat4 probe_trs_matrix(math::Vec3 translation, math::Quat rotation,
                                          math::Vec3 scale) {
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
        return probe_trs_matrix(translation, rotation, scale);
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
        static_cast<void>(probe_pointer_index(attribute.data, accessor_base, accessor_count,
                                              "POSITION accessor"));
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
    const std::uint32_t node_index = probe_pointer_index(node, node_base, node_count, "scene node");
    if (visited.at(node_index) != 0U) {
        return;
    }
    visited[node_index] = 1U;
    const math::Mat4 world = parent_world * metadata_node_local_matrix(*node);

    if (node->mesh != nullptr) {
        const std::uint32_t mesh_index =
            probe_pointer_index(node->mesh, mesh_base, mesh_count, "mesh");
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

struct ProbeCgltfDataDeleter {
    void operator()(cgltf_data* data) const noexcept {
        cgltf_free(data);
    }
};

using ProbeCgltfDataPtr = std::unique_ptr<cgltf_data, ProbeCgltfDataDeleter>;

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
    ProbeCgltfDataPtr data(raw_data);

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
            probe_pointer_index(data->scene, data->scenes, data->scenes_count, "scene");
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

} // namespace cubey::asset
