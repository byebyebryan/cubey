#pragma once

#include <cubey/asset/gltf_asset.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct cgltf_image;

namespace cubey::asset::gltf_internal {

[[nodiscard]] std::runtime_error gltf_error(const std::string& message);
[[nodiscard]] std::string label_or_empty(const char* label);
[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) noexcept;
[[nodiscard]] std::string percent_decode(std::string_view uri);
[[nodiscard]] std::vector<std::uint8_t> decode_data_uri(std::string_view uri);
[[nodiscard]] GltfImage decode_image(const cgltf_image& source,
                                     const std::filesystem::path& source_path);

} // namespace cubey::asset::gltf_internal
