#include <cubey/asset/hdr_image.h>

#include <stb_image.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace cubey::asset {
namespace {

[[nodiscard]] std::runtime_error hdr_error(const std::string& message) {
    return std::runtime_error("HDR image: " + message);
}

struct StbiImageDeleter {
    void operator()(float* pixels) const noexcept {
        stbi_image_free(pixels);
    }
};

[[nodiscard]] std::size_t checked_rgba32f_count(int width, int height,
                                                const std::filesystem::path& path) {
    if (width <= 0 || height <= 0) {
        throw hdr_error("decoded image has invalid dimensions: " + path.string());
    }
    const std::size_t pixel_count = static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height);
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 4U) {
        throw hdr_error("decoded image is too large: " + path.string());
    }
    return pixel_count * 4U;
}

} // namespace

HdrImage load_hdr_image(const std::filesystem::path& path) {
    const std::string path_string = path.string();
    if (stbi_is_hdr(path_string.c_str()) == 0) {
        throw hdr_error("expected Radiance .hdr input: " + path_string);
    }

    int width = 0;
    int height = 0;
    int channel_count = 0;
    std::unique_ptr<float, StbiImageDeleter> pixels{
        stbi_loadf(path_string.c_str(), &width, &height, &channel_count, 4)};
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        throw hdr_error("failed to decode " + path_string +
                        (reason != nullptr ? std::string{": "} + reason : std::string{}));
    }

    const std::size_t value_count = checked_rgba32f_count(width, height, path);
    return {
        .source_path = path,
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .rgba32f = std::vector<float>{pixels.get(), pixels.get() + value_count},
    };
}

} // namespace cubey::asset
