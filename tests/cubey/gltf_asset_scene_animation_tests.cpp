#include "gltf_asset_test_support.h"

using namespace cubey::test_support;

void test_gltf_asset_marks_nodes_authored_with_matrix() {
    const std::filesystem::path dir = test_dir("cubey-gltf-matrix-node");
    const std::filesystem::path path = dir / "matrix_node.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{
    "name": "MatrixNode",
    "matrix": [
      1.0, 0.0, 0.0, 0.0,
      0.0, 2.0, 0.0, 0.0,
      0.0, 0.0, 3.0, 0.0,
      4.0, 5.0, 6.0, 1.0
    ]
  }]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.nodes.size() == 1, "matrix-node asset should load one node");
    require(asset.nodes[0].has_matrix, "glTF node should remember matrix-authored transforms");
    require_close(asset.nodes[0].local_matrix[3][0], 4.0F,
                  "glTF matrix node should preserve authored matrix translation X");
    require_close(asset.nodes[0].local_matrix[3][1], 5.0F,
                  "glTF matrix node should preserve authored matrix translation Y");
    require_close(asset.nodes[0].local_matrix[3][2], 6.0F,
                  "glTF matrix node should preserve authored matrix translation Z");
}

void test_gltf_asset_loads_empty_animation() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_animation");
    const std::filesystem::path path = dir / "animation.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "animations": [{"channels": [], "samplers": []}]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);
    require(asset.animations.size() == 1, "loader should preserve animation records");
    require(asset.animations[0].samplers.empty(), "empty animation samplers should load");
    require(asset.animations[0].channels.empty(), "empty animation channels should load");
    std::filesystem::remove_all(dir);
}
void test_gltf_asset_loads_rigid_animation_channels() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_rigid_animation");

    std::vector<std::uint8_t> bytes;
    const std::size_t time_offset = bytes.size();
    append_f32(bytes, 0.0F);
    append_f32(bytes, 1.0F);
    const std::size_t translation_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 1.0F, 2.0F, 3.0F);
    cubey::write_binary_file(dir / "animation.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "nodes": [{"name": "Animated"}],
  "animations": [{
    "name": "Move",
    "samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}],
    "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]
  }],
  "buffers": [{"uri": "animation.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(time_offset) +
                             R"JSON(, "byteLength": 8},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(translation_offset) +
                             R"JSON(, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR",
     "min": [0.0], "max": [1.0]},
    {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC3"}
  ]
})JSON";
    const std::filesystem::path path = dir / "animation.gltf";
    write_text_file(path, gltf);

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.animations.size() == 1, "loader should preserve one animation");
    const cubey::asset::GltfAnimation& animation = asset.animations[0];
    require(animation.label == "Move", "animation name should load");
    require_close(animation.duration_seconds, 1.0F, "animation duration should come from inputs");
    require(animation.samplers.size() == 1, "animation sampler should load");
    require(animation.channels.size() == 1, "animation channel should load");
    require(animation.samplers[0].interpolation == cubey::asset::GltfAnimationInterpolation::Linear,
            "animation interpolation should load");
    require(animation.samplers[0].component_count == 3,
            "translation sampler should expose vec3 component count");
    require(animation.samplers[0].input_times == std::vector<float>({0.0F, 1.0F}),
            "animation input times should load");
    require(animation.samplers[0].output_values.size() == 6,
            "translation output values should flatten vec3 samples");
    require_close(animation.samplers[0].output_values[5], 3.0F,
                  "translation output values should load");
    require(animation.channels[0].node_index == 0, "animation channel target node should load");
    require(animation.channels[0].target_path == cubey::asset::GltfAnimationTargetPath::Translation,
            "animation channel target path should load");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_loads_normalized_rotation_animation_output() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_normalized_rotation_animation");

    std::vector<std::uint8_t> bytes;
    const std::size_t time_offset = bytes.size();
    append_f32(bytes, 0.0F);
    append_f32(bytes, 1.0F);
    const std::size_t rotation_offset = bytes.size();
    append_i16_quat(bytes, 0, 0, 0, 32767);
    append_i16_quat(bytes, 0, 23170, 0, 23170);
    cubey::write_binary_file(dir / "rotation_animation.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "nodes": [{"name": "Animated"}],
  "animations": [{
    "name": "Turn",
    "samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}],
    "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}]
  }],
  "buffers": [{"uri": "rotation_animation.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(time_offset) +
                             R"JSON(, "byteLength": 8},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(rotation_offset) +
                             R"JSON(, "byteLength": 16}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR",
     "min": [0.0], "max": [1.0]},
    {"bufferView": 1, "componentType": 5122, "count": 2, "type": "VEC4", "normalized": true}
  ]
})JSON";
    const std::filesystem::path path = dir / "rotation_animation.gltf";
    write_text_file(path, gltf);

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.animations.size() == 1, "normalized rotation animation should load");
    const cubey::asset::GltfAnimationSampler& sampler = asset.animations[0].samplers[0];
    require(sampler.component_count == 4, "rotation sampler should expose vec4 component count");
    require_close(sampler.output_values[3], 1.0F,
                  "normalized int16 quaternion output should unpack to float");
    require_close(sampler.output_values[5], 0.707114F,
                  "normalized int16 quaternion y should unpack to float");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_loads_skinning_and_morph_data() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_skin_morph");

    std::vector<std::uint8_t> bytes;
    const std::size_t position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 1.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 1.0F, 0.0F);
    const std::size_t normal_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    const std::size_t joints_offset = bytes.size();
    append_u16_vec4(bytes, 0, 0, 0, 0);
    append_u16_vec4(bytes, 0, 0, 0, 0);
    append_u16_vec4(bytes, 0, 0, 0, 0);
    const std::size_t weights_offset = bytes.size();
    append_vec4(bytes, 1.0F, 0.0F, 0.0F, 0.0F);
    append_vec4(bytes, 1.0F, 0.0F, 0.0F, 0.0F);
    append_vec4(bytes, 1.0F, 0.0F, 0.0F, 0.0F);
    const std::size_t morph_position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.1F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 0.2F, 0.0F);
    const std::size_t index_offset = bytes.size();
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 2);
    const std::size_t inverse_bind_offset = bytes.size();
    append_mat4_identity(bytes);

    cubey::write_binary_file(dir / "skin_morph.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "SkinnedMesh", "mesh": 0, "skin": 0, "children": [1]},
    {"name": "Joint"}
  ],
  "skins": [{"name": "OneJoint", "joints": [1], "inverseBindMatrices": 6}],
  "meshes": [{
    "name": "MorphMesh",
    "weights": [0.25],
    "extras": {"targetNames": ["Smile"]},
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
      "targets": [{"POSITION": 4}],
      "indices": 5
    }]
  }],
  "buffers": [{"uri": "skin_morph.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(normal_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(joints_offset) +
                             R"JSON(, "byteLength": 24},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(weights_offset) +
                             R"JSON(, "byteLength": 48},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(morph_position_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(index_offset) +
                             R"JSON(, "byteLength": 6},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(inverse_bind_offset) +
                             R"JSON(, "byteLength": 64}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": 3, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 5, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 6, "componentType": 5126, "count": 1, "type": "MAT4"}
  ]
})JSON";
    const std::filesystem::path path = dir / "skin_morph.gltf";
    write_text_file(path, gltf);

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.skins.size() == 1, "loader should preserve one skin");
    require(asset.skins[0].label == "OneJoint", "skin name should load");
    require(asset.skins[0].joints == std::vector<std::uint32_t>({1}),
            "skin joints should map to node indices");
    require(asset.skins[0].inverse_bind_matrices.size() == 1, "inverse bind matrices should load");
    require_close(asset.skins[0].inverse_bind_matrices[0][0][0], 1.0F,
                  "inverse bind matrix should load as identity");

    require(asset.nodes[0].skin_index == 0, "node skin index should load");
    require(asset.meshes[0].weights == std::vector<float>({0.25F}),
            "mesh default morph weights should load");

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require(primitive.vertices[0].joints0 == std::array<std::uint16_t, 4>({0, 0, 0, 0}),
            "vertex joints should load");
    require_close(primitive.vertices[0].weights0.x, 1.0F, "vertex weights should load");
    require(primitive.morph_targets.size() == 1, "primitive morph target should load");
    require(primitive.morph_targets[0].label == "Smile",
            "mesh targetNames extras should name morph targets");
    require(primitive.morph_targets[0].position_deltas.size() == 3,
            "morph position deltas should load");
    require_close(primitive.morph_targets[0].position_deltas[1].x, 0.1F,
                  "morph position delta should load");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_loads_sparse_animation_output() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_sparse_animation");

    std::vector<std::uint8_t> bytes;
    const std::size_t time_offset = bytes.size();
    append_f32(bytes, 0.0F);
    append_f32(bytes, 1.0F);
    const std::size_t translation_sparse_indices_offset = bytes.size();
    append_u8(bytes, 1);
    pad_to_alignment(bytes, 4);
    const std::size_t translation_sparse_values_offset = bytes.size();
    append_vec3(bytes, 4.0F, 5.0F, 6.0F);

    cubey::write_binary_file(dir / "sparse_animation.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "nodes": [{"name": "Animated"}],
  "animations": [{
    "name": "SparseMove",
    "samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}],
    "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]
  }],
  "buffers": [{"uri": "sparse_animation.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(time_offset) +
                             R"JSON(, "byteLength": 8},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(translation_sparse_indices_offset) +
                             R"JSON(, "byteLength": 1},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(translation_sparse_values_offset) +
                             R"JSON(, "byteLength": 12}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR",
     "min": [0.0], "max": [1.0]},
    {"componentType": 5126, "count": 2, "type": "VEC3",
     "sparse": {
       "count": 1,
       "indices": {"bufferView": 1, "componentType": 5121},
       "values": {"bufferView": 2}
     }}
  ]
})JSON";
    const std::filesystem::path path = dir / "sparse_animation.gltf";
    write_text_file(path, gltf);

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.animations.size() == 1, "sparse animation should load");
    const std::vector<float>& output = asset.animations[0].samplers[0].output_values;
    require(output.size() == 6, "sparse animation output should flatten vec3 samples");
    require_close(output[0], 0.0F, "missing sparse base output should default to zero");
    require_close(output[3], 4.0F, "sparse animation output x should load");
    require_close(output[4], 5.0F, "sparse animation output y should load");
    require_close(output[5], 6.0F, "sparse animation output z should load");

    std::filesystem::remove_all(dir);
}
