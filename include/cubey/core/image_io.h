#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace cubey {

struct ImageRgba8 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels{};
};

[[nodiscard]] ImageRgba8 read_image_rgba8(const std::filesystem::path& path);
void write_png_rgba8(const std::filesystem::path& output_path, std::uint32_t width,
                     std::uint32_t height, std::span<const std::uint8_t> pixels);

} // namespace cubey
