#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cubey::projects::atmosphere {

inline constexpr std::uint32_t kLunarAtlasExtent = 512;

struct LunarAtlasMip {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::size_t byte_offset = 0;
    std::size_t byte_count = 0;
};

struct LunarAtlas {
    std::uint32_t width = kLunarAtlasExtent;
    std::uint32_t height = kLunarAtlasExtent;
    std::uint32_t mip_levels = 1;
    std::vector<std::uint8_t> rgba8{};
    std::vector<LunarAtlasMip> mips{};
};

[[nodiscard]] std::uint32_t lunar_atlas_mip_count(std::uint32_t extent);
[[nodiscard]] LunarAtlas generate_lunar_atlas(std::uint32_t extent = kLunarAtlasExtent);
[[nodiscard]] std::uint64_t lunar_atlas_hash(std::span<const std::uint8_t> bytes);

} // namespace cubey::projects::atmosphere
