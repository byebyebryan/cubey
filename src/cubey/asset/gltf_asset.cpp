#include <cubey/asset/gltf_asset.h>

#include <cubey/core/file_io.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
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
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace cubey::asset {
namespace {

[[nodiscard]] std::runtime_error gltf_error(const std::string& message) {
    return std::runtime_error("glTF asset: " + message);
}

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

[[nodiscard]] std::string label_or_empty(const char* label) {
    return label != nullptr ? std::string(label) : std::string{};
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::uint8_t hex_value(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
    }
    throw gltf_error("invalid percent-encoded URI");
}

[[nodiscard]] std::string percent_decode(std::string_view uri) {
    std::string decoded;
    decoded.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%') {
            if (i + 2 >= uri.size()) {
                throw gltf_error("truncated percent-encoded URI");
            }
            decoded.push_back(static_cast<char>((hex_value(uri[i + 1]) << 4U) |
                                                hex_value(uri[i + 2])));
            i += 2;
        } else {
            decoded.push_back(uri[i]);
        }
    }
    return decoded;
}

[[nodiscard]] std::uint8_t base64_value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<std::uint8_t>(value - 'A');
    }
    if (value >= 'a' && value <= 'z') {
        return static_cast<std::uint8_t>(value - 'a' + 26);
    }
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0' + 52);
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    throw gltf_error("invalid base64 data URI");
}

[[nodiscard]] std::vector<std::uint8_t> decode_base64(std::string_view encoded) {
    std::vector<std::uint8_t> decoded;
    std::array<std::uint8_t, 4> block{};
    std::size_t block_size = 0;
    std::size_t padding = 0;

    for (char value : encoded) {
        if (std::isspace(static_cast<unsigned char>(value)) != 0) {
            continue;
        }
        if (value == '=') {
            block[block_size++] = 0;
            ++padding;
        } else {
            if (padding != 0) {
                throw gltf_error("base64 padding must terminate the data URI");
            }
            block[block_size++] = base64_value(value);
        }

        if (block_size == block.size()) {
            decoded.push_back(static_cast<std::uint8_t>((block[0] << 2U) | (block[1] >> 4U)));
            if (padding < 2) {
                decoded.push_back(
                    static_cast<std::uint8_t>((block[1] << 4U) | (block[2] >> 2U)));
            }
            if (padding == 0) {
                decoded.push_back(static_cast<std::uint8_t>((block[2] << 6U) | block[3]));
            }
            block_size = 0;
            padding = 0;
        }
    }

    if (block_size != 0) {
        throw gltf_error("base64 data URI length is not a multiple of four");
    }
    return decoded;
}

[[nodiscard]] std::vector<std::uint8_t> decode_data_uri(std::string_view uri) {
    constexpr std::string_view data_prefix = "data:";
    if (!starts_with(uri, data_prefix)) {
        throw gltf_error("expected data URI");
    }
    const std::size_t comma = uri.find(',');
    if (comma == std::string_view::npos) {
        throw gltf_error("data URI is missing comma separator");
    }
    const std::string_view metadata = uri.substr(data_prefix.size(), comma - data_prefix.size());
    const std::string_view payload = uri.substr(comma + 1);
    if (metadata.find(";base64") != std::string_view::npos) {
        return decode_base64(payload);
    }

    const std::string decoded = percent_decode(payload);
    return {decoded.begin(), decoded.end()};
}

[[nodiscard]] std::vector<std::uint8_t> image_bytes(const cgltf_image& image,
                                                    const std::filesystem::path& source_path) {
    if (image.buffer_view != nullptr) {
        const cgltf_buffer_view* view = image.buffer_view;
        if (view->buffer == nullptr || view->buffer->data == nullptr) {
            throw gltf_error("image buffer view has no loaded buffer data");
        }
        const auto* begin = static_cast<const std::uint8_t*>(view->buffer->data) + view->offset;
        return std::vector<std::uint8_t>(begin, begin + view->size);
    }

    if (image.uri == nullptr) {
        throw gltf_error("image has neither URI nor buffer view");
    }

    const std::string_view uri(image.uri);
    if (starts_with(uri, "data:")) {
        return decode_data_uri(uri);
    }

    return read_binary_file(source_path.parent_path() / percent_decode(uri));
}

[[nodiscard]] GltfImage decode_image(const cgltf_image& source,
                                     const std::filesystem::path& source_path) {
    std::vector<std::uint8_t> bytes = image_bytes(source, source_path);
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                             &height, &channels, 4);
    if (decoded == nullptr) {
        throw gltf_error("failed to decode image: " + std::string(stbi_failure_reason()));
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(decoded);
        throw gltf_error("decoded image has invalid dimensions");
    }

    const std::size_t byte_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    GltfImage image{
        .label = label_or_empty(source.name),
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .rgba8 = std::vector<std::uint8_t>(decoded, decoded + byte_count),
    };
    stbi_image_free(decoded);
    return image;
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
        .wrap_s = wrap_mode(sampler.wrap_s),
        .wrap_t = wrap_mode(sampler.wrap_t),
    };
}

[[nodiscard]] GltfTextureRef load_texture_ref(const cgltf_texture_view& view,
                                              const cgltf_texture* texture_base,
                                              cgltf_size texture_count) {
    return {
        .texture_index = pointer_index(view.texture, texture_base, texture_count, "texture"),
        .texcoord = static_cast<std::uint32_t>(view.texcoord),
    };
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

[[nodiscard]] GltfMaterial load_material(const cgltf_material& material,
                                         const cgltf_texture* texture_base,
                                         cgltf_size texture_count) {
    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
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
        .emissive_factor =
            {
                material.emissive_factor[0],
                material.emissive_factor[1],
                material.emissive_factor[2],
            },
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
    };
}

[[nodiscard]] const cgltf_accessor* find_attribute(const cgltf_primitive& primitive,
                                                   cgltf_attribute_type type,
                                                   cgltf_int index = 0) noexcept {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attribute = primitive.attributes[i];
        if (attribute.type == type && attribute.index == index) {
            return attribute.data;
        }
    }
    return nullptr;
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

[[nodiscard]] math::Vec2 read_vec2(const cgltf_accessor* accessor, cgltf_size index) {
    cgltf_float values[2]{};
    if (cgltf_accessor_read_float(accessor, index, values, 2) == 0) {
        throw gltf_error("failed to read VEC2 accessor");
    }
    return {values[0], values[1]};
}

[[nodiscard]] math::Vec3 read_vec3(const cgltf_accessor* accessor, cgltf_size index) {
    cgltf_float values[3]{};
    if (cgltf_accessor_read_float(accessor, index, values, 3) == 0) {
        throw gltf_error("failed to read VEC3 accessor");
    }
    return {values[0], values[1], values[2]};
}

[[nodiscard]] math::Vec4 read_vec4(const cgltf_accessor* accessor, cgltf_size index) {
    cgltf_float values[4]{};
    if (cgltf_accessor_read_float(accessor, index, values, 4) == 0) {
        throw gltf_error("failed to read VEC4 accessor");
    }
    return {values[0], values[1], values[2], values[3]};
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

void generate_tangents(GltfMeshPrimitive& primitive) {
    std::vector<math::Vec3> accum(primitive.vertices.size(), math::Vec3{0.0F, 0.0F, 0.0F});
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
        const math::Vec2 delta_uv1 = v1.texcoord0 - v0.texcoord0;
        const math::Vec2 delta_uv2 = v2.texcoord0 - v0.texcoord0;
        const float determinant = delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x;
        if (std::abs(determinant) < 1.0e-6F) {
            continue;
        }
        const math::Vec3 tangent = (edge1 * delta_uv2.y - edge2 * delta_uv1.y) / determinant;
        accum[i0] += tangent;
        accum[i1] += tangent;
        accum[i2] += tangent;
    }

    for (std::size_t i = 0; i < primitive.vertices.size(); ++i) {
        const math::Vec3 normal = primitive.vertices[i].normal;
        math::Vec3 tangent = accum[i] - normal * glm::dot(normal, accum[i]);
        if (glm::length(tangent) < 1.0e-6F) {
            tangent = std::abs(normal.y) < 0.9F ? glm::cross(normal, math::Vec3{0.0F, 1.0F, 0.0F})
                                                : glm::cross(normal, math::Vec3{1.0F, 0.0F, 0.0F});
        }
        tangent = glm::normalize(tangent);
        primitive.vertices[i].tangent = {tangent.x, tangent.y, tangent.z, 1.0F};
    }
}

[[nodiscard]] GltfMeshPrimitive load_primitive(const cgltf_primitive& primitive,
                                               const cgltf_material* material_base,
                                               cgltf_size material_count,
                                               const GltfLoadConfig& config) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        throw gltf_error("only triangle primitives are supported");
    }

    const cgltf_accessor* positions =
        find_attribute(primitive, cgltf_attribute_type_position);
    const cgltf_accessor* normals = find_attribute(primitive, cgltf_attribute_type_normal);
    const cgltf_accessor* tangents = find_attribute(primitive, cgltf_attribute_type_tangent);
    const cgltf_accessor* texcoord0 = find_attribute(primitive, cgltf_attribute_type_texcoord, 0);

    require_accessor_components(positions, 3, "POSITION");
    require_accessor_components(normals, 3, "NORMAL");
    if (tangents != nullptr) {
        require_accessor_components(tangents, 4, "TANGENT");
    }
    if (texcoord0 != nullptr) {
        require_accessor_components(texcoord0, 2, "TEXCOORD_0");
    }

    GltfMeshPrimitive result;
    result.vertices.resize(positions->count);
    for (cgltf_size i = 0; i < positions->count; ++i) {
        result.vertices[i].position = read_vec3(positions, i);
        result.vertices[i].normal = glm::normalize(read_vec3(normals, i));
        if (tangents != nullptr) {
            result.vertices[i].tangent = read_vec4(tangents, i);
        }
        if (texcoord0 != nullptr) {
            result.vertices[i].texcoord0 = read_vec2(texcoord0, i);
        }
    }

    if (primitive.indices != nullptr) {
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

    if (tangents == nullptr && config.generate_missing_tangents && texcoord0 != nullptr) {
        generate_tangents(result);
    }

    result.material_index = pointer_index(primitive.material, material_base, material_count,
                                          "material");
    if (result.material_index == kInvalidAssetIndex) {
        result.material_index = 0;
    } else {
        ++result.material_index;
    }
    result.local_bounds = bounds_for_positions(result.vertices);
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
                                                   material_count, config));
    }
    return result;
}

[[nodiscard]] math::Mat4 trs_matrix(math::Vec3 translation, math::Quat rotation,
                                    math::Vec3 scale) {
    math::Mat4 matrix{1.0F};
    matrix = glm::translate(matrix, translation);
    matrix *= glm::mat4_cast(rotation);
    matrix = glm::scale(matrix, scale);
    return matrix;
}

[[nodiscard]] GltfNode load_node(const cgltf_node& node, const cgltf_node* node_base,
                                 cgltf_size node_count, const cgltf_mesh* mesh_base,
                                 cgltf_size mesh_count) {
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
        result.local_matrix = matrix;

        math::Vec3 skew{};
        math::Vec4 perspective{};
        glm::decompose(matrix, result.scale, result.rotation, result.translation, skew,
                       perspective);
    }

    result.mesh_index = pointer_index(node.mesh, mesh_base, mesh_count, "mesh");
    result.children.reserve(node.children_count);
    for (cgltf_size i = 0; i < node.children_count; ++i) {
        result.children.push_back(pointer_index(node.children[i], node_base, node_count, "node"));
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

void reject_unsupported_features(const cgltf_data& data) {
    for (cgltf_size i = 0; i < data.meshes_count; ++i) {
        const cgltf_mesh& mesh = data.meshes[i];
        if (mesh.weights_count != 0) {
            throw gltf_error("morph targets are not supported");
        }
        for (cgltf_size j = 0; j < mesh.primitives_count; ++j) {
            if (mesh.primitives[j].targets_count != 0) {
                throw gltf_error("morph target primitives are not supported");
            }
        }
    }
    if (data.animations_count != 0) {
        throw gltf_error("animations are not supported");
    }
    if (data.skins_count != 0) {
        throw gltf_error("skins are not supported");
    }
}

struct CgltfDataDeleter {
    void operator()(cgltf_data* data) const noexcept {
        cgltf_free(data);
    }
};

using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDataDeleter>;

} // namespace

GltfAsset load_gltf_asset(const std::filesystem::path& path, GltfLoadConfig config) {
    cgltf_options options{};
    cgltf_data* raw_data = nullptr;
    const std::string path_string = path.string();
    cgltf_result result = cgltf_parse_file(&options, path_string.c_str(), &raw_data);
    if (result != cgltf_result_success) {
        throw gltf_error("failed to parse " + path_string);
    }
    CgltfDataPtr data(raw_data);

    result = cgltf_load_buffers(&options, data.get(), path_string.c_str());
    if (result != cgltf_result_success) {
        throw gltf_error("failed to load buffers for " + path_string);
    }

    result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        throw gltf_error("validation failed for " + path_string);
    }

    reject_unsupported_features(*data);

    GltfAsset asset{
        .source_path = path,
    };

    asset.samplers.reserve(data->samplers_count);
    for (cgltf_size i = 0; i < data->samplers_count; ++i) {
        asset.samplers.push_back(load_sampler(data->samplers[i]));
    }

    asset.images.reserve(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        asset.images.push_back(decode_image(data->images[i], path));
    }

    asset.textures.reserve(data->textures_count);
    for (cgltf_size i = 0; i < data->textures_count; ++i) {
        const cgltf_texture& texture = data->textures[i];
        asset.textures.push_back(GltfTexture{
            .label = label_or_empty(texture.name),
            .image_index = pointer_index(texture.image, data->images, data->images_count, "image"),
            .sampler_index =
                pointer_index(texture.sampler, data->samplers, data->samplers_count, "sampler"),
        });
    }

    asset.materials.reserve(data->materials_count + 1);
    asset.materials.push_back(default_material());
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        asset.materials.push_back(load_material(data->materials[i], data->textures,
                                                data->textures_count));
    }

    asset.meshes.reserve(data->meshes_count);
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        asset.meshes.push_back(load_mesh(data->meshes[i], data->materials, data->materials_count,
                                         config));
    }

    asset.nodes.reserve(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        asset.nodes.push_back(load_node(data->nodes[i], data->nodes, data->nodes_count,
                                        data->meshes, data->meshes_count));
    }

    asset.scenes.reserve(data->scenes_count);
    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        asset.scenes.push_back(load_scene(data->scenes[i], data->nodes, data->nodes_count));
    }

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

GltfTextureColorSpace gltf_texture_color_space_for_material_slot(
    const GltfTextureRef& texture, GltfTextureColorSpace default_space) noexcept {
    return texture.has_value() ? default_space : GltfTextureColorSpace::Linear;
}

} // namespace cubey::asset
