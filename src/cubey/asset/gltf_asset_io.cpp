#include "gltf_asset_internal.h"

#include <cubey/core/file_io.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace cubey::asset::gltf_internal {

std::runtime_error gltf_error(const std::string& message) {
    return std::runtime_error("glTF asset: " + message);
}

std::string label_or_empty(const char* label) {
    return label != nullptr ? std::string(label) : std::string{};
}

bool starts_with(std::string_view value, std::string_view prefix) noexcept {
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

std::string percent_decode(std::string_view uri) {
    std::string decoded;
    decoded.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%') {
            if (i + 2 >= uri.size()) {
                throw gltf_error("truncated percent-encoded URI");
            }
            decoded.push_back(
                static_cast<char>((hex_value(uri[i + 1]) << 4U) | hex_value(uri[i + 2])));
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
                decoded.push_back(static_cast<std::uint8_t>((block[1] << 4U) | (block[2] >> 2U)));
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

std::vector<std::uint8_t> decode_data_uri(std::string_view uri) {
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

GltfImage decode_image(const cgltf_image& source, const std::filesystem::path& source_path) {
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

} // namespace cubey::asset::gltf_internal
