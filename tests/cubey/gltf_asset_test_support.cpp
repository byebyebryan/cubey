#include "gltf_asset_test_support.h"

#include <cstring>
#include <fstream>

namespace cubey::test_support {

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

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(std::uint16_t));
    std::memcpy(bytes.data() + offset, &value, sizeof(std::uint16_t));
}

void append_i16(std::vector<std::uint8_t>& bytes, std::int16_t value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(std::int16_t));
    std::memcpy(bytes.data() + offset, &value, sizeof(std::int16_t));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(std::uint32_t));
    std::memcpy(bytes.data() + offset, &value, sizeof(std::uint32_t));
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(std::uint64_t));
    std::memcpy(bytes.data() + offset, &value, sizeof(std::uint64_t));
}

[[nodiscard]] std::vector<std::uint8_t> minimal_ktx2_header(std::uint32_t width,
                                                            std::uint32_t height,
                                                            std::uint32_t mip_levels,
                                                            std::uint32_t layer_count) {
    std::vector<std::uint8_t> bytes{
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A,
    };
    append_u32(bytes, 0); // vkFormat: undefined for Basis Universal payloads.
    append_u32(bytes, 1); // typeSize.
    append_u32(bytes, width);
    append_u32(bytes, height);
    append_u32(bytes, 0); // pixelDepth.
    append_u32(bytes, layer_count);
    append_u32(bytes, 1); // faceCount.
    append_u32(bytes, mip_levels);
    append_u32(bytes, 1); // supercompressionScheme: BasisLZ / ETC1S.
    append_u32(bytes, 0); // dfdByteOffset.
    append_u32(bytes, 0); // dfdByteLength.
    append_u32(bytes, 0); // kvdByteOffset.
    append_u32(bytes, 0); // kvdByteLength.
    append_u64(bytes, 0); // sgdByteOffset.
    append_u64(bytes, 0); // sgdByteLength.
    return bytes;
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

void append_i16_quat(std::vector<std::uint8_t>& bytes, std::int16_t x, std::int16_t y,
                     std::int16_t z, std::int16_t w) {
    append_i16(bytes, x);
    append_i16(bytes, y);
    append_i16(bytes, z);
    append_i16(bytes, w);
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

void append_little_endian_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

[[nodiscard]] std::vector<std::uint8_t> make_glb(std::string json, std::size_t embedded_bin_bytes) {
    while (json.size() % 4U != 0U) {
        json.push_back(' ');
    }
    require(json.size() <= std::numeric_limits<std::uint32_t>::max(),
            "GLB test JSON should fit the chunk length field");
    require(embedded_bin_bytes <= std::numeric_limits<std::uint32_t>::max(),
            "GLB test BIN should fit the chunk length field");
    const std::uint64_t total_size =
        12U + 8U + json.size() + (embedded_bin_bytes == 0U ? 0U : 8U + embedded_bin_bytes);
    require(total_size <= std::numeric_limits<std::uint32_t>::max(),
            "GLB test should fit the declared length field");

    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(total_size));
    append_little_endian_u32(bytes, 0x46546C67U); // glTF magic.
    append_little_endian_u32(bytes, 2U);          // GLB v2.
    append_little_endian_u32(bytes, static_cast<std::uint32_t>(total_size));
    append_little_endian_u32(bytes, static_cast<std::uint32_t>(json.size()));
    append_little_endian_u32(bytes, 0x4E4F534AU); // JSON chunk.
    for (const char character : json) {
        bytes.push_back(static_cast<std::uint8_t>(character));
    }
    if (embedded_bin_bytes != 0U) {
        append_little_endian_u32(bytes, static_cast<std::uint32_t>(embedded_bin_bytes));
        append_little_endian_u32(bytes, 0x004E4942U); // BIN chunk.
        bytes.insert(bytes.end(), embedded_bin_bytes, 0xA5U);
    }
    return bytes;
}

void write_glb_file(const std::filesystem::path& path, std::string json,
                    std::size_t embedded_bin_bytes) {
    cubey::write_binary_file(path, make_glb(std::move(json), embedded_bin_bytes));
}

[[nodiscard]] std::vector<std::uint8_t> make_glb_header(std::uint32_t version,
                                                        std::uint32_t declared_length,
                                                        std::uint32_t chunk_length,
                                                        std::uint32_t chunk_magic) {
    std::vector<std::uint8_t> bytes;
    append_little_endian_u32(bytes, 0x46546C67U);
    append_little_endian_u32(bytes, version);
    append_little_endian_u32(bytes, declared_length);
    append_little_endian_u32(bytes, chunk_length);
    append_little_endian_u32(bytes, chunk_magic);
    return bytes;
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
    "KHR_materials_clearcoat",
    "KHR_materials_sheen",
    "KHR_materials_anisotropy",
    "KHR_materials_iridescence"
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
      "KHR_materials_clearcoat": {
        "clearcoatFactor": 0.6,
        "clearcoatTexture": {"index": 3},
        "clearcoatRoughnessFactor": 0.25,
        "clearcoatRoughnessTexture": {"index": 4},
        "clearcoatNormalTexture": {"index": 5, "scale": 0.8}
      },
      "KHR_materials_sheen": {
        "sheenColorFactor": [0.2, 0.3, 0.4],
        "sheenColorTexture": {"index": 6},
        "sheenRoughnessFactor": 0.45,
        "sheenRoughnessTexture": {"index": 7}
      },
      "KHR_materials_anisotropy": {
        "anisotropyStrength": 0.55,
        "anisotropyRotation": 1.25,
        "anisotropyTexture": {"index": 8}
      },
      "KHR_materials_iridescence": {
        "iridescenceFactor": 0.65,
        "iridescenceTexture": {"index": 9},
        "iridescenceIor": 1.4,
        "iridescenceThicknessMinimum": 120.0,
        "iridescenceThicknessMaximum": 520.0,
        "iridescenceThicknessTexture": {"index": 10}
      }
    }
  }],
  "textures": [
    {"source": 0}, {"source": 0}, {"source": 0}, {"source": 0},
    {"source": 0}, {"source": 0}, {"source": 0}, {"source": 0},
    {"source": 0}, {"source": 0}, {"source": 0}
  ],
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

std::filesystem::path write_uv1_normal_map_tangent_gltf(const std::filesystem::path& dir) {
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
    append_vec2(bytes, 0.0F, 0.0F);
    append_vec2(bytes, 0.0F, 1.0F);
    append_vec2(bytes, 1.0F, 0.0F);
    const std::size_t index_offset = bytes.size();
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 2);

    cubey::write_binary_file(dir / "uv1_normal_tangent.bin", bytes);

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{
    "primitives": [{
      "attributes": {
        "POSITION": 0,
        "NORMAL": 1,
        "TEXCOORD_0": 2,
        "TEXCOORD_1": 3
      },
      "indices": 4,
      "material": 0
    }]
  }],
  "buffers": [{"uri": "uv1_normal_tangent.bin", "byteLength": )JSON") +
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
                             std::to_string(index_offset) +
                             R"JSON(, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "materials": [{
    "normalTexture": {"index": 0, "texCoord": 1}
  }],
  "textures": [{"source": 0}],
  "images": [{
    "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="
  }]
})JSON";
    const std::filesystem::path gltf_path = dir / "uv1_normal_tangent.gltf";
    write_text_file(gltf_path, gltf);
    return gltf_path;
}

std::filesystem::path write_anisotropy_tangent_contract_gltf(
    const std::filesystem::path& dir, bool include_tangent, bool include_texcoord0,
    bool include_texcoord1, bool include_normal_texture, std::uint32_t normal_texcoord,
    bool include_anisotropy_texture, std::uint32_t anisotropy_texcoord) {
    std::vector<std::uint8_t> bytes;
    const std::size_t position_offset = bytes.size();
    append_vec3(bytes, 0.0F, 0.0F, 0.0F);
    append_vec3(bytes, 1.0F, 0.0F, 0.0F);
    append_vec3(bytes, 0.0F, 1.0F, 0.0F);
    const std::size_t normal_offset = bytes.size();
    for (std::size_t index = 0; index < 3; ++index) {
        append_vec3(bytes, 0.0F, 0.0F, 1.0F);
    }
    const std::size_t texcoord0_offset = bytes.size();
    append_vec2(bytes, 0.0F, 0.0F);
    append_vec2(bytes, 1.0F, 0.0F);
    append_vec2(bytes, 0.0F, 1.0F);
    const std::size_t texcoord1_offset = bytes.size();
    append_vec2(bytes, 0.0F, 0.0F);
    append_vec2(bytes, 0.0F, 1.0F);
    append_vec2(bytes, 1.0F, 0.0F);
    const std::size_t tangent_offset = bytes.size();
    for (std::size_t index = 0; index < 3; ++index) {
        append_vec4(bytes, 1.0F, 0.0F, 0.0F, 1.0F);
    }
    const std::size_t index_offset = bytes.size();
    append_u16(bytes, 0);
    append_u16(bytes, 1);
    append_u16(bytes, 2);
    cubey::write_binary_file(dir / "anisotropy_tangent.bin", bytes);

    std::string attributes = "\"POSITION\": 0, \"NORMAL\": 1";
    if (include_texcoord0) {
        attributes += ", \"TEXCOORD_0\": 2";
    }
    if (include_texcoord1) {
        attributes += ", \"TEXCOORD_1\": 3";
    }
    if (include_tangent) {
        attributes += ", \"TANGENT\": 4";
    }

    std::string material = "{\"extensions\": {\"KHR_materials_anisotropy\": {";
    if (include_anisotropy_texture) {
        material += "\"anisotropyTexture\": {\"index\": 0, \"texCoord\": " +
                    std::to_string(anisotropy_texcoord) + "}";
    }
    material += "}}";
    if (include_normal_texture) {
        material +=
            ", \"normalTexture\": {\"index\": 0, \"texCoord\": " + std::to_string(normal_texcoord) +
            "}";
    }
    material += "}";

    const std::string gltf = std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_anisotropy"],
  "extensionsRequired": ["KHR_materials_anisotropy"],
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {)JSON") +
                             attributes + R"JSON(}, "indices": 5, "material": 0}]}],
  "buffers": [{"uri": "anisotropy_tangent.bin", "byteLength": )JSON" +
                             std::to_string(bytes.size()) + R"JSON(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(position_offset) + R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(normal_offset) + R"JSON(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(texcoord0_offset) + R"JSON(, "byteLength": 24},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(texcoord1_offset) + R"JSON(, "byteLength": 24},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(tangent_offset) + R"JSON(, "byteLength": 48},
    {"buffer": 0, "byteOffset": )JSON" +
                             std::to_string(index_offset) + R"JSON(, "byteLength": 6}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 5, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "materials": [)JSON" + material +
                             R"JSON(],
  "textures": [{"source": 0}],
  "images": [{"uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="}]
})JSON";
    const std::filesystem::path path = dir / "anisotropy_tangent.gltf";
    write_text_file(path, gltf);
    return path;
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

} // namespace cubey::test_support
