#include <cubey/core/image_io.h>

#include <stb_image_write.h>

#include <cstddef>
#include <limits>
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
