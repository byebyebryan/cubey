#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace cubey::asset::gltf_internal {
namespace {

[[nodiscard]] std::uint32_t checked_scene_index(std::ptrdiff_t index, const char* label) {
    if (index < 0 || static_cast<std::uint64_t>(index) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw gltf_error(std::string(label) + " index is out of range");
    }
    return static_cast<std::uint32_t>(index);
}

template <typename T>
[[nodiscard]] std::uint32_t scene_pointer_index(const T* pointer, const T* base, cgltf_size count,
                                                const char* label) {
    if (pointer == nullptr) {
        return kInvalidAssetIndex;
    }
    if (base == nullptr || pointer < base || pointer >= base + count) {
        throw gltf_error(std::string(label) + " pointer is outside the glTF data");
    }
    return checked_scene_index(pointer - base, label);
}

void require_scene_float_accessor(const cgltf_accessor* accessor, cgltf_type type,
                                  const char* label) {
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

[[nodiscard]] math::Mat4 scene_trs_matrix(math::Vec3 translation, math::Quat rotation,
                                          math::Vec3 scale) {
    math::Mat4 matrix{1.0F};
    matrix = glm::translate(matrix, translation);
    matrix *= glm::mat4_cast(rotation);
    matrix = glm::scale(matrix, scale);
    return matrix;
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
    result.local_matrix = scene_trs_matrix(result.translation, result.rotation, result.scale);

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

    result.mesh_index = scene_pointer_index(node.mesh, mesh_base, mesh_count, "mesh");
    result.skin_index = scene_pointer_index(node.skin, skin_base, skin_count, "skin");
    result.weights.reserve(node.weights_count);
    for (cgltf_size i = 0; i < node.weights_count; ++i) {
        result.weights.push_back(node.weights[i]);
    }
    result.children.reserve(node.children_count);
    for (cgltf_size i = 0; i < node.children_count; ++i) {
        result.children.push_back(
            scene_pointer_index(node.children[i], node_base, node_count, "node"));
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
        .skeleton_node_index = scene_pointer_index(skin.skeleton, node_base, node_count, "node"),
    };

    result.joints.reserve(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        result.joints.push_back(scene_pointer_index(skin.joints[i], node_base, node_count, "node"));
    }

    if (skin.inverse_bind_matrices != nullptr) {
        require_scene_float_accessor(skin.inverse_bind_matrices, cgltf_type_mat4,
                                     "inverseBindMatrices");
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
        result.root_nodes.push_back(
            scene_pointer_index(scene.nodes[i], node_base, node_count, "node"));
    }
    return result;
}
} // namespace

void assemble_gltf_scene_data(GltfAsset& asset, const cgltf_data& data) {
    asset.nodes.reserve(data.nodes_count);
    for (cgltf_size i = 0; i < data.nodes_count; ++i) {
        asset.nodes.push_back(load_node(data.nodes[i], data.nodes, data.nodes_count, data.meshes,
                                        data.meshes_count, data.skins, data.skins_count));
    }

    asset.skins.reserve(data.skins_count);
    for (cgltf_size i = 0; i < data.skins_count; ++i) {
        asset.skins.push_back(
            load_skin(data.skins[i], data.skins, data.skins_count, data.nodes, data.nodes_count));
    }

    asset.scenes.reserve(data.scenes_count);
    for (cgltf_size i = 0; i < data.scenes_count; ++i) {
        asset.scenes.push_back(load_scene(data.scenes[i], data.nodes, data.nodes_count));
    }
}

} // namespace cubey::asset::gltf_internal
