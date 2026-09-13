#include "gltf_asset_test_support.h"

using namespace cubey::test_support;

void test_gltf_asset_generates_tangent_handedness_from_mirrored_uvs() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_mirrored_uv_tangent");
    const cubey::asset::GltfAsset asset =
        cubey::asset::load_gltf_asset(write_mirrored_uv_tangent_gltf(dir));

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require(primitive.vertices.size() == 6, "mirrored UV tangent test should load vertices");
    require_close(primitive.vertices[0].tangent.w, -1.0F,
                  "generated tangent should preserve standard UV handedness");
    require_close(primitive.vertices[3].tangent.w, 1.0F,
                  "generated tangent should mark mirrored UV handedness");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_loads_uv1_vertex_color_and_texture_transform() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_uv1_color_transform");
    const cubey::asset::GltfAsset asset =
        cubey::asset::load_gltf_asset(write_uv1_color_transform_gltf(dir));

    const cubey::asset::GltfMaterial& material = asset.materials[1];
    require(material.base_color_texture.texture_index == 0,
            "texture transform material should preserve texture index");
    require(material.base_color_texture.texcoord == 1,
            "texture transform should override the base texture coordinate set");
    require_close(material.base_color_texture.offset.x, 0.25F,
                  "texture transform offset x should load");
    require_close(material.base_color_texture.offset.y, 0.5F,
                  "texture transform offset y should load");
    require_close(material.base_color_texture.rotation, 1.570796F,
                  "texture transform rotation should load");
    require_close(material.base_color_texture.scale.x, 2.0F,
                  "texture transform scale x should load");
    require_close(material.base_color_texture.scale.y, 3.0F,
                  "texture transform scale y should load");

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require_close(primitive.vertices[1].texcoord1.x, 0.75F, "UV1 x should load");
    require_close(primitive.vertices[2].texcoord1.y, 0.75F, "UV1 y should load");
    require_close(primitive.vertices[1].color0.g, 128.0F / 255.0F,
                  "normalized vertex color should unpack to float");
    require_close(primitive.vertices[1].color0.b, 1.0F,
                  "normalized vertex color blue should unpack to float");
    require_close(primitive.vertices[1].color0.a, 64.0F / 255.0F,
                  "normalized vertex color alpha should unpack to float");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_generates_tangents_from_normal_texture_uv_set() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_uv1_normal_tangent");
    const cubey::asset::GltfAsset asset =
        cubey::asset::load_gltf_asset(write_uv1_normal_map_tangent_gltf(dir));

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require_close(primitive.vertices[0].tangent.x, 0.0F,
                  "UV1 normal maps should not generate tangents from UV0");
    require_close(primitive.vertices[0].tangent.y, 1.0F,
                  "UV1 normal maps should generate tangents from TEXCOORD_1");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_rejects_unsupported_texture_coordinate_set() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_unsupported_texcoord");
    const std::filesystem::path path = dir / "unsupported_texcoord.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorTexture": {"index": 0, "texCoord": 2}
    }
  }],
  "textures": [{"source": 0}],
  "images": [{
    "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="
  }]
})JSON");

    require_throws([&path] { (void)cubey::asset::load_gltf_asset(path); },
                   "loader should reject texture coordinate sets above UV1");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_generates_flat_normals_when_missing() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_missing_normals");
    const cubey::asset::GltfAsset asset =
        cubey::asset::load_gltf_asset(write_missing_normal_wedge_gltf(dir));

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require(primitive.vertices.size() == 6,
            "missing normals should expand indexed triangles for flat normals");
    require(primitive.indices == std::vector<std::uint32_t>({0, 1, 2, 3, 4, 5}),
            "expanded flat-normal primitive should use sequential indices");
    require_close(primitive.vertices[0].normal.z, 1.0F,
                  "first generated face normal should point along +Z");
    require_close(primitive.vertices[3].normal.x, 1.0F,
                  "second generated face normal should point along +X");
    require_close(primitive.vertices[3].position.x, 0.0F,
                  "expanded vertex should preserve source position");
    require(primitive.vertices[3].joints0 == std::array<std::uint16_t, 4>({0, 1, 0, 0}),
            "expanded vertex should preserve source joints");
    require_close(primitive.vertices[3].weights0.y, 0.25F,
                  "expanded vertex should preserve source weights");
    require(primitive.morph_targets.size() == 1, "morph target should survive expansion");
    require(primitive.morph_targets[0].position_deltas.size() == 6,
            "morph deltas should expand with generated normals");
    require_close(primitive.morph_targets[0].position_deltas[5].z, 0.3F,
                  "expanded morph delta should preserve source vertex delta");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_can_reject_missing_normals_when_generation_is_disabled() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_missing_normals_disabled");
    const std::filesystem::path path = write_missing_normal_wedge_gltf(dir);
    cubey::asset::GltfLoadConfig config;
    config.generate_missing_normals = false;

    require_throws([&path, config] { (void)cubey::asset::load_gltf_asset(path, config); },
                   "loader should reject missing normals when generation is disabled");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_loads_sparse_mesh_accessors() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_sparse_mesh");

    std::vector<std::uint8_t> bytes;
    const std::size_t normal_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    const std::size_t weights_offset = bytes.size();
    append_vec4(bytes, 1.0F, 0.0F, 0.0F, 0.0F);
    append_vec4(bytes, 1.0F, 0.0F, 0.0F, 0.0F);
    append_vec4(bytes, 1.0F, 0.0F, 0.0F, 0.0F);
    const std::size_t position_sparse_indices_offset = bytes.size();
    append_u8(bytes, 1);
    append_u8(bytes, 2);
    pad_to_alignment(bytes, 4);
    const std::size_t position_sparse_values_offset = bytes.size();
    append_vec3(bytes, 1.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 1.0F, 0.0F);
    const std::size_t joints_sparse_indices_offset = bytes.size();
    append_u8(bytes, 2);
    pad_to_alignment(bytes, 4);
    const std::size_t joints_sparse_values_offset = bytes.size();
    append_u8_vec4(bytes, 3, 4, 5, 6);
    pad_to_alignment(bytes, 4);
    const std::size_t morph_sparse_indices_offset = bytes.size();
    append_u8(bytes, 1);
    pad_to_alignment(bytes, 4);
    const std::size_t morph_sparse_values_offset = bytes.size();
    append_vec3(bytes, 0.25F, 0.0F, 0.0F);

    cubey::write_binary_file(dir / "sparse_mesh.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "SparseMeshNode", "mesh": 0}],
  "meshes": [{
    "name": "SparseMesh",
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
      "targets": [{"POSITION": 4}]
    }]
  }],
  "buffers": [{"uri": "sparse_mesh.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(normal_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(weights_offset) +
                             R"JSON(, "byteLength": 48},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_sparse_indices_offset) +
                             R"JSON(, "byteLength": 2},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_sparse_values_offset) +
                             R"JSON(, "byteLength": 24},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(joints_sparse_indices_offset) +
                             R"JSON(, "byteLength": 1},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(joints_sparse_values_offset) +
                             R"JSON(, "byteLength": 4},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(morph_sparse_indices_offset) +
                             R"JSON(, "byteLength": 1},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(morph_sparse_values_offset) +
                             R"JSON(, "byteLength": 12}
  ],
  "accessors": [
    {"componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0],
     "sparse": {
       "count": 2,
       "indices": {"bufferView": 2, "componentType": 5121},
       "values": {"bufferView": 3}
     }},
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"componentType": 5121, "count": 3, "type": "VEC4",
     "sparse": {
       "count": 1,
       "indices": {"bufferView": 4, "componentType": 5121},
       "values": {"bufferView": 5}
     }},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"componentType": 5126, "count": 3, "type": "VEC3",
     "sparse": {
       "count": 1,
       "indices": {"bufferView": 6, "componentType": 5121},
       "values": {"bufferView": 7}
     }}
  ]
})JSON";
    const std::filesystem::path path = dir / "sparse_mesh.gltf";
    write_text_file(path, gltf);

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require_close(primitive.vertices[1].position.x, 1.0F,
                  "sparse POSITION should override base zero values");
    require_close(primitive.vertices[2].position.y, 1.0F,
                  "sparse POSITION should load every override");
    require(primitive.vertices[2].joints0 == std::array<std::uint16_t, 4>({3, 4, 5, 6}),
            "sparse JOINTS_0 should load unsigned byte overrides");
    require(primitive.morph_targets.size() == 1, "sparse morph target should load");
    require_close(primitive.morph_targets[0].position_deltas[1].x, 0.25F,
                  "sparse morph deltas should override base zero values");

    std::filesystem::remove_all(dir);
}
