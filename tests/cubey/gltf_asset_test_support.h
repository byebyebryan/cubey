#pragma once

#include <cubey/asset/gltf_asset.h>
#include <cubey/core/file_io.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::test_support {

void require(bool condition, const char* message);
void require_close(float value, float expected, const char* message);

template <typename Action> void require_throws(Action&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

template <typename Action>
void require_throws_with_message(Action&& action, std::string_view expected_message,
                                 const char* message) {
    try {
        action();
    } catch (const std::exception& error) {
        require(std::string_view{error.what()}.find(expected_message) != std::string_view::npos,
                message);
        return;
    }
    throw std::runtime_error(message);
}

void append_f32(std::vector<std::uint8_t>& bytes, float value);
void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value);
void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value);
void append_i16(std::vector<std::uint8_t>& bytes, std::int16_t value);
void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value);
void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value);

[[nodiscard]] std::vector<std::uint8_t> minimal_ktx2_header(std::uint32_t width,
                                                            std::uint32_t height,
                                                            std::uint32_t mip_levels,
                                                            std::uint32_t layer_count = 0);

void append_vec3(std::vector<std::uint8_t>& bytes, float x, float y, float z);
void append_vec2(std::vector<std::uint8_t>& bytes, float x, float y);
void append_vec4(std::vector<std::uint8_t>& bytes, float x, float y, float z, float w);
void append_u16_vec4(std::vector<std::uint8_t>& bytes, std::uint16_t x, std::uint16_t y,
                     std::uint16_t z, std::uint16_t w);
void append_u8_vec4(std::vector<std::uint8_t>& bytes, std::uint8_t x, std::uint8_t y,
                    std::uint8_t z, std::uint8_t w);
void append_i16_quat(std::vector<std::uint8_t>& bytes, std::int16_t x, std::int16_t y,
                     std::int16_t z, std::int16_t w);
void pad_to_alignment(std::vector<std::uint8_t>& bytes, std::size_t alignment);
void append_mat4_identity(std::vector<std::uint8_t>& bytes);

void write_text_file(const std::filesystem::path& path, const std::string& text);
void append_little_endian_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value);

[[nodiscard]] std::vector<std::uint8_t> make_glb(std::string json,
                                                 std::size_t embedded_bin_bytes = 0U);
void write_glb_file(const std::filesystem::path& path, std::string json,
                    std::size_t embedded_bin_bytes = 0U);
[[nodiscard]] std::vector<std::uint8_t> make_glb_header(std::uint32_t version,
                                                        std::uint32_t declared_length,
                                                        std::uint32_t chunk_length,
                                                        std::uint32_t chunk_magic);

std::filesystem::path test_dir(const char* name);
std::filesystem::path write_triangle_gltf(const std::filesystem::path& dir);
std::filesystem::path write_mirrored_uv_tangent_gltf(const std::filesystem::path& dir);
std::filesystem::path write_uv1_color_transform_gltf(const std::filesystem::path& dir);
std::filesystem::path write_uv1_normal_map_tangent_gltf(const std::filesystem::path& dir);
std::filesystem::path write_anisotropy_tangent_contract_gltf(
    const std::filesystem::path& dir, bool include_tangent, bool include_texcoord0,
    bool include_texcoord1, bool include_normal_texture, std::uint32_t normal_texcoord,
    bool include_anisotropy_texture, std::uint32_t anisotropy_texcoord);
std::filesystem::path write_missing_normal_wedge_gltf(const std::filesystem::path& dir);

} // namespace cubey::test_support
