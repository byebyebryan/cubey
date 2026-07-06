#include <cubey/core/image_io.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace cubey {
namespace {

[[nodiscard]] int checked_image_dimension(std::uint32_t value, const char* name) {
    if (value == 0) {
        throw std::runtime_error(std::string{name} + " must be nonzero");
    }
    if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string{name} + " exceeds PNG writer limit");
    }
    return static_cast<int>(value);
}

} // namespace

ImageRgba8 read_image_rgba8(const std::filesystem::path& path) {
    const std::string path_string = path.string();
    int width = 0;
    int height = 0;
    int channel_count = 0;
    using StbiPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    StbiPixels decoded{stbi_load(path_string.c_str(), &width, &height, &channel_count, 4),
                       stbi_image_free};
    if (decoded == nullptr) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error("failed to decode image: " + path_string + ": " +
                                 (reason == nullptr ? "unknown decoder error" : reason));
    }
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("decoded image has invalid dimensions: " + path_string);
    }
    constexpr std::size_t kBytesPerPixel = 4;
    const auto checked_width = static_cast<std::uint32_t>(width);
    const auto checked_height = static_cast<std::uint32_t>(height);
    if (static_cast<std::size_t>(checked_width) >
        std::numeric_limits<std::size_t>::max() /
            (static_cast<std::size_t>(checked_height) * kBytesPerPixel)) {
        throw std::runtime_error("decoded image byte size overflows: " + path_string);
    }
    const std::size_t byte_count =
        static_cast<std::size_t>(checked_width) * static_cast<std::size_t>(checked_height) *
        kBytesPerPixel;
    return ImageRgba8{
        .width = checked_width,
        .height = checked_height,
        .pixels = std::vector<std::uint8_t>(decoded.get(), decoded.get() + byte_count),
    };
}

void write_png_rgba8(const std::filesystem::path& output_path, std::uint32_t width,
                     std::uint32_t height, std::span<const std::uint8_t> pixels) {
    const int checked_width = checked_image_dimension(width, "PNG width");
    const int checked_height = checked_image_dimension(height, "PNG height");
    constexpr int kComponents = 4;
    constexpr std::size_t kBytesPerPixel = 4;
    const std::size_t expected_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kBytesPerPixel;
    if (expected_bytes > pixels.size()) {
        throw std::runtime_error("PNG pixel buffer is smaller than width * height * 4");
    }
    if (expected_bytes != pixels.size()) {
        throw std::runtime_error("PNG pixel buffer must match width * height * 4 exactly");
    }

    const std::size_t stride = static_cast<std::size_t>(width) * kBytesPerPixel;
    if (stride > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("PNG row stride exceeds writer limit");
    }

    const std::string path = output_path.string();
    if (stbi_write_png(path.c_str(), checked_width, checked_height, kComponents, pixels.data(),
                       static_cast<int>(stride)) == 0) {
        throw std::runtime_error("failed to write PNG output");
    }
}

} // namespace cubey
