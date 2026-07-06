#include <cubey/core/file_io.h>
#include <cubey/core/image_io.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint32_t read_be_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4U > bytes.size()) {
        throw std::runtime_error("PNG is too small for requested integer");
    }
    return (static_cast<std::uint32_t>(bytes.at(offset)) << 24U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 1U)) << 16U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 2U)) << 8U) |
           static_cast<std::uint32_t>(bytes.at(offset + 3U));
}

} // namespace

void test_image_io_writes_rgba_png() {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_image_io_test.png";
    std::filesystem::remove(output);

    constexpr std::array<std::uint8_t, 16> pixels{
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };
    cubey::write_png_rgba8(output, 2, 2, pixels);

    const std::vector<std::uint8_t> bytes = cubey::read_binary_file(output);
    constexpr std::array<std::uint8_t, 8> png_signature{
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
    };
    require(bytes.size() > 33U, "generated PNG should contain signature and IHDR");
    require(std::equal(png_signature.begin(), png_signature.end(), bytes.begin()),
            "generated PNG should use PNG signature");
    require(read_be_u32(bytes, 16) == 2, "generated PNG should preserve width");
    require(read_be_u32(bytes, 20) == 2, "generated PNG should preserve height");

    std::filesystem::remove(output);
}

void test_image_io_reads_rgba_image() {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_image_io_read_test.png";
    std::filesystem::remove(output);

    constexpr std::array<std::uint8_t, 16> pixels{
        255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 255, 32,
    };
    cubey::write_png_rgba8(output, 2, 2, pixels);

    const cubey::ImageRgba8 image = cubey::read_image_rgba8(output);
    require(image.width == 2, "read_image_rgba8 should preserve width");
    require(image.height == 2, "read_image_rgba8 should preserve height");
    require(image.pixels.size() == pixels.size(), "read_image_rgba8 should decode RGBA8 bytes");
    require(std::equal(pixels.begin(), pixels.end(), image.pixels.begin()),
            "read_image_rgba8 should preserve RGBA pixel values");

    std::filesystem::remove(output);
}

void test_image_io_read_rejects_missing_file() {
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "cubey_image_io_missing_test.png";
    std::filesystem::remove(missing);

    bool threw = false;
    try {
        (void)cubey::read_image_rgba8(missing);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "read_image_rgba8 should reject missing files");
}
