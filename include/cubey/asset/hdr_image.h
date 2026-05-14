#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace cubey::asset {

struct HdrImage {
    std::filesystem::path source_path{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba32f{};
};

[[nodiscard]] HdrImage load_hdr_image(const std::filesystem::path& path);

} // namespace cubey::asset
