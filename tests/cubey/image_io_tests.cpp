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
