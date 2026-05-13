#include <cubey/asset/gltf_asset.h>
#include <cubey/core/file_io.h>

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

void append_f32(std::vector<std::uint8_t>& bytes, float value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(float));
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
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
    "alphaMode": "MASK",
    "alphaCutoff": 0.35,
    "doubleSided": true
  }],
  "textures": [{"source": 0}],
  "images": [{
    "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="
  }]
})JSON";
    const std::filesystem::path gltf_path = dir / "triangle.gltf";
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

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require(primitive.vertices.size() == 3, "primitive should load vertices");
    require(primitive.indices == std::vector<std::uint32_t>({0, 1, 2}),
            "primitive should load uint16 indices as uint32");
    require(primitive.material_index == 1, "primitive material should account for default slot");
    require_close(primitive.vertices[1].position.x, 1.0F, "position accessor should load");
    require_close(primitive.vertices[2].texcoord0.y, 1.0F, "uv accessor should load");
    require_close(primitive.vertices[0].tangent.x, 1.0F,
                  "loader should generate missing tangents");
    require_close(primitive.local_bounds.center.x, 0.5F, "bounds center should be computed");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_rejects_unsupported_animation() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_animation");
    const std::filesystem::path path = dir / "animation.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "animations": [{"channels": [], "samplers": []}]
})JSON");

    bool threw = false;
    try {
        static_cast<void>(cubey::asset::load_gltf_asset(path));
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("animations are not supported") !=
                std::string::npos;
    }
    require(threw, "loader should reject animations with a clear error");
    std::filesystem::remove_all(dir);
}
