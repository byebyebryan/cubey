#include <cubey/asset/gltf_asset.h>
#include <cubey/core/file_io.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float value, float expected, const char* message) {
    constexpr float kTolerance = 0.0001F;
    if (value < expected - kTolerance || value > expected + kTolerance) {
        throw std::runtime_error(message);
    }
}

template <typename Action> void require_throws(Action&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void append_f32(std::vector<std::uint8_t>& bytes, float value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(float));
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(std::uint16_t));
    std::memcpy(bytes.data() + offset, &value, sizeof(std::uint16_t));
}

void append_vec3(std::vector<std::uint8_t>& bytes, float x, float y, float z) {
    append_f32(bytes, x);
    append_f32(bytes, y);
    append_f32(bytes, z);
}

void append_vec2(std::vector<std::uint8_t>& bytes, float x, float y) {
    append_f32(bytes, x);
    append_f32(bytes, y);
}

void append_vec4(std::vector<std::uint8_t>& bytes, float x, float y, float z, float w) {
    append_f32(bytes, x);
    append_f32(bytes, y);
    append_f32(bytes, z);
    append_f32(bytes, w);
}

void append_u16_vec4(std::vector<std::uint8_t>& bytes, std::uint16_t x, std::uint16_t y,
                     std::uint16_t z, std::uint16_t w) {
    append_u16(bytes, x);
    append_u16(bytes, y);
    append_u16(bytes, z);
    append_u16(bytes, w);
}

void append_u8_vec4(std::vector<std::uint8_t>& bytes, std::uint8_t x, std::uint8_t y,
                    std::uint8_t z, std::uint8_t w) {
    append_u8(bytes, x);
    append_u8(bytes, y);
    append_u8(bytes, z);
    append_u8(bytes, w);
}

void pad_to_alignment(std::vector<std::uint8_t>& bytes, std::size_t alignment) {
    while (bytes.size() % alignment != 0) {
        bytes.push_back(0);
    }
}

void append_mat4_identity(std::vector<std::uint8_t>& bytes) {
    for (std::size_t index = 0; index < 16; ++index) {
        append_f32(bytes, index % 5 == 0 ? 1.0F : 0.0F);
    }
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open test file");
    }
    file << text;
}

std::filesystem::path test_dir(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path write_triangle_gltf(const std::filesystem::path& dir) {
    std::vector<std::uint8_t> bytes;
    const std::size_t position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 1.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 1.0F, 0.0F);
    const std::size_t normal_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    const std::size_t uv_offset = bytes.size();
    append_vec2(bytes, 0.0F, 0.0F);
    append_vec2(bytes, 1.0F, 0.0F);
    append_vec2(bytes, 0.0F, 1.0F);
    const std::size_t index_offset = bytes.size();
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 2);

    cubey::write_binary_file(dir / "triangle.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_ior",
    "KHR_materials_specular",
    "KHR_materials_emissive_strength",
    "KHR_materials_unlit"
  ],
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "TriangleNode", "mesh": 0, "translation": [1.0, 2.0, 3.0]}],
  "meshes": [{
    "name": "TriangleMesh",
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3,
      "material": 0
    }]
  }],
  "buffers": [{"uri": "triangle.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(normal_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(uv_offset) +
                             R"JSON(, "byteLength": 24},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(index_offset) +
                             R"JSON(, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "materials": [{
    "name": "Helmet-ish",
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.8, 0.7, 0.6, 1.0],
      "metallicFactor": 0.2,
      "roughnessFactor": 0.4,
      "baseColorTexture": {"index": 0}
    },
    "emissiveFactor": [0.1, 0.2, 0.3],
    "alphaMode": "MASK",
    "alphaCutoff": 0.35,
    "doubleSided": true,
    "extensions": {
      "KHR_materials_ior": {"ior": 1.8},
      "KHR_materials_specular": {
        "specularFactor": 0.7,
        "specularColorFactor": [0.9, 0.8, 0.7],
        "specularTexture": {"index": 1},
        "specularColorTexture": {"index": 2}
      },
      "KHR_materials_emissive_strength": {"emissiveStrength": 2.0},
      "KHR_materials_unlit": {}
    }
  }],
  "textures": [{"source": 0}, {"source": 0}, {"source": 0}],
  "images": [{
    "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="
  }]
})JSON";
    const std::filesystem::path gltf_path = dir / "triangle.gltf";
    write_text_file(gltf_path, gltf);
    return gltf_path;
}

std::filesystem::path write_mirrored_uv_tangent_gltf(const std::filesystem::path& dir) {
    std::vector<std::uint8_t> bytes;
    const std::size_t position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 1.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 1.0F, 0.0F);
    append_vec3(bytes, 2.0F, 0.0F, 0.0F);
    append_vec3(bytes, 3.0F, 0.0F, 0.0F);
    append_vec3(bytes, 2.0F, 1.0F, 0.0F);
    const std::size_t normal_offset = bytes.size();
    for (std::size_t i = 0; i < 6; ++i) {
        append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    }
    const std::size_t uv_offset = bytes.size();
    append_vec2(bytes, 0.0F, 0.0F);
    append_vec2(bytes, 1.0F, 0.0F);
    append_vec2(bytes, 0.0F, 1.0F);
    append_vec2(bytes, 0.0F, 0.0F);
    append_vec2(bytes, 0.0F, 1.0F);
    append_vec2(bytes, 1.0F, 0.0F);
    const std::size_t index_offset = bytes.size();
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 2);
    append_u16(bytes, 3);
    append_u16(bytes, 4);
    append_u16(bytes, 5);

    cubey::write_binary_file(dir / "mirrored_uv_tangent.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
      "indices": 3
    }]
  }],
  "buffers": [{"uri": "mirrored_uv_tangent.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_offset) +
                             R"JSON(, "byteLength": 72},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(normal_offset) +
                             R"JSON(, "byteLength": 72},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(uv_offset) +
                             R"JSON(, "byteLength": 48},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(index_offset) +
                             R"JSON(, "byteLength": 12}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 6, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [3.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 6, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 6, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ]
})JSON";
    const std::filesystem::path gltf_path = dir / "mirrored_uv_tangent.gltf";
    write_text_file(gltf_path, gltf);
    return gltf_path;
}

std::filesystem::path write_uv1_color_transform_gltf(const std::filesystem::path& dir) {
    std::vector<std::uint8_t> bytes;
    const std::size_t position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 1.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 1.0F, 0.0F);
    const std::size_t normal_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    const std::size_t uv0_offset = bytes.size();
    append_vec2(bytes, 0.0F, 0.0F);
    append_vec2(bytes, 1.0F, 0.0F);
    append_vec2(bytes, 0.0F, 1.0F);
    const std::size_t uv1_offset = bytes.size();
    append_vec2(bytes, 0.25F, 0.25F);
    append_vec2(bytes, 0.75F, 0.25F);
    append_vec2(bytes, 0.25F, 0.75F);
    const std::size_t color_offset = bytes.size();
    append_u8_vec4(bytes, 255, 0, 0, 255);
    append_u8_vec4(bytes, 0, 128, 255, 64);
    append_u8_vec4(bytes, 64, 255, 0, 128);
    pad_to_alignment(bytes, 4);
    const std::size_t index_offset = bytes.size();
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 2);

    cubey::write_binary_file(dir / "uv1_color_transform.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_texture_transform"],
  "extensionsRequired": ["KHR_texture_transform"],
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {
        "POSITION": 0,
        "NORMAL": 1,
        "TEXCOORD_0": 2,
        "TEXCOORD_1": 3,
        "COLOR_0": 4
      },
      "indices": 5,
      "material": 0
    }]
  }],
  "buffers": [{"uri": "uv1_color_transform.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(normal_offset) +
                             R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(uv0_offset) +
                             R"JSON(, "byteLength": 24},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(uv1_offset) +
                             R"JSON(, "byteLength": 24},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(color_offset) +
                             R"JSON(, "byteLength": 12},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(index_offset) +
                             R"JSON(, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 4, "componentType": 5121, "count": 3, "type": "VEC4", "normalized": true},
    {"bufferView": 5, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorTexture": {
        "index": 0,
        "texCoord": 0,
        "extensions": {
          "KHR_texture_transform": {
            "offset": [0.25, 0.5],
            "rotation": 1.570796,
            "scale": [2.0, 3.0],
            "texCoord": 1
          }
        }
      }
    }
  }],
  "textures": [{"source": 0}],
  "images": [{
    "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="
  }]
})JSON";
    const std::filesystem::path gltf_path = dir / "uv1_color_transform.gltf";
    write_text_file(gltf_path, gltf);
    return gltf_path;
}

std::filesystem::path write_missing_normal_wedge_gltf(const std::filesystem::path& dir) {
    std::vector<std::uint8_t> bytes;
    const std::size_t position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 1.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 1.0F, 0.0F);
    append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    const std::size_t joints_offset = bytes.size();
    append_u16_vec4(bytes, 0, 1, 0, 0);
    append_u16_vec4(bytes, 1, 0, 0, 0);
    append_u16_vec4(bytes, 0, 1, 0, 0);
    append_u16_vec4(bytes, 1, 0, 0, 0);
    const std::size_t weights_offset = bytes.size();
    append_vec4(bytes, 0.75F, 0.25F, 0.0F, 0.0F);
    append_vec4(bytes, 1.0F, 0.0F, 0.0F, 0.0F);
    append_vec4(bytes, 0.5F, 0.5F, 0.0F, 0.0F);
    append_vec4(bytes, 0.25F, 0.75F, 0.0F, 0.0F);
    const std::size_t morph_position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.1F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 0.2F, 0.0F);
    append_vec3(bytes, 0.0F, 0.0F, 0.3F);
    const std::size_t index_offset = bytes.size();
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 2);
    append_u16(bytes, 0);
    append_u16(bytes, 2);
    append_u16(bytes, 3);

    cubey::write_binary_file(dir / "missing_normals.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "MissingNormals", "mesh": 0}],
  "meshes": [{
    "name": "Wedge",
    "primitives": [{
      "attributes": {"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2},
      "targets": [{"POSITION": 3}],
      "indices": 4
    }]
  }],
  "buffers": [{"uri": "missing_normals.bin", "byteLength": )JSON") +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_offset) +
                             R"JSON(, "byteLength": 48},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(joints_offset) +
                             R"JSON(, "byteLength": 32},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(weights_offset) +
                             R"JSON(, "byteLength": 64},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(morph_position_offset) +
                             R"JSON(, "byteLength": 48},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(index_offset) +
                             R"JSON(, "byteLength": 12}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 1.0]},
    {"bufferView": 1, "componentType": 5123, "count": 4, "type": "VEC4"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 4, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ]
})JSON";
    const std::filesystem::path gltf_path = dir / "missing_normals.gltf";
    write_text_file(gltf_path, gltf);
    return gltf_path;
}

} // namespace

void test_gltf_asset_loads_static_pbr_triangle() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_triangle");
    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(write_triangle_gltf(dir));

    require(asset.scenes.size() == 1, "asset should load one scene");
    require(asset.default_scene == 0, "asset should preserve default scene");
    require(asset.nodes.size() == 1, "asset should load one node");
    require(asset.meshes.size() == 1, "asset should load one mesh");
    require(asset.materials.size() == 2, "asset should include default plus glTF material");
    require(asset.images.size() == 1, "asset should decode embedded PNG image");
    require(asset.images[0].width == 1 && asset.images[0].height == 1,
            "embedded image should decode to 1x1");

    const cubey::asset::GltfMaterial& material = asset.materials[1];
    require(material.alpha_mode == cubey::asset::GltfAlphaMode::Mask,
            "material should preserve alpha mode");
    require(material.double_sided, "material should preserve double-sided flag");
    require(material.base_color_texture.texture_index == 0,
            "material should map base color texture");
    require_close(material.base_color_factor.r, 0.8F, "base color factor should load");
    require_close(material.metallic_factor, 0.2F, "metallic factor should load");
    require_close(material.roughness_factor, 0.4F, "roughness factor should load");
    require_close(material.reflectance, 0.714285F, "IOR extension should load as reflectance");
    require_close(material.specular_factor, 0.7F, "specular factor should load");
    require_close(material.specular_color_factor.r, 0.9F, "specular color factor red should load");
    require_close(material.specular_color_factor.g, 0.8F,
                  "specular color factor green should load");
    require_close(material.specular_color_factor.b, 0.7F, "specular color factor blue should load");
    require(material.specular_texture.texture_index == 1,
            "specular texture should load from KHR_materials_specular");
    require(material.specular_color_texture.texture_index == 2,
            "specular color texture should load from KHR_materials_specular");
    require_close(material.emissive_factor.r, 0.2F, "emissive strength should scale red");
    require_close(material.emissive_factor.g, 0.4F, "emissive strength should scale green");
    require_close(material.emissive_factor.b, 0.6F, "emissive strength should scale blue");
    require(material.unlit, "unlit material extension should load");

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require(primitive.vertices.size() == 3, "primitive should load vertices");
    require(primitive.indices == std::vector<std::uint32_t>({0, 1, 2}),
            "primitive should load uint16 indices as uint32");
    require(primitive.material_index == 1, "primitive material should account for default slot");
    require_close(primitive.vertices[1].position.x, 1.0F, "position accessor should load");
    require_close(primitive.vertices[2].texcoord0.y, 1.0F, "uv accessor should load");
    require_close(primitive.vertices[0].tangent.x, 1.0F, "loader should generate missing tangents");
    require_close(primitive.local_bounds.center.x, 0.5F, "bounds center should be computed");

    std::filesystem::remove_all(dir);
}

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

void test_gltf_asset_ignores_unknown_optional_extensions() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_optional_extension");
    const std::filesystem::path path = dir / "optional_extension.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["VENDOR_optional_debug_data"]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.materials.size() == 1, "loader should still create the default material");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_rejects_unknown_required_extensions() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_required_extension");
    const std::filesystem::path path = dir / "required_extension.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["VENDOR_required_geometry"],
  "extensionsRequired": ["VENDOR_required_geometry"]
})JSON");

    require_throws([&path] { (void)cubey::asset::load_gltf_asset(path); },
                   "loader should reject unknown required extensions");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_accepts_supported_required_extensions() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_supported_required_extension");
    const std::filesystem::path path = dir / "supported_required_extension.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_ior",
    "KHR_materials_emissive_strength",
    "KHR_materials_unlit"
  ],
  "extensionsRequired": [
    "KHR_materials_ior",
    "KHR_materials_emissive_strength",
    "KHR_materials_unlit"
  ],
  "materials": [{
    "emissiveFactor": [0.2, 0.3, 0.4],
    "extensions": {
      "KHR_materials_ior": {"ior": 1.8},
      "KHR_materials_emissive_strength": {"emissiveStrength": 3.0},
      "KHR_materials_unlit": {}
    }
  }]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.materials.size() == 2, "loader should preserve material with required extension");
    require_close(asset.materials[1].reflectance, 0.714285F,
                  "supported required material extension should load");
    require_close(asset.materials[1].emissive_factor.g, 0.9F,
                  "supported required emissive strength should load");
    require(asset.materials[1].unlit, "supported required unlit extension should load");
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
