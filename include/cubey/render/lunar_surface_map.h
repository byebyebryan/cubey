#pragma once

#include <cubey/procedural/artifact_metadata.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cubey::render {

inline constexpr std::uint32_t kLunarSurfaceMapWidth = 1024;
inline constexpr std::uint32_t kLunarSurfaceMapHeight = 512;

struct LunarSurfaceMapMip {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::size_t byte_offset = 0;
    std::size_t byte_count = 0;
};

struct LunarSurfaceMap {
    std::uint32_t width = kLunarSurfaceMapWidth;
    std::uint32_t height = kLunarSurfaceMapHeight;
    std::uint32_t mip_levels = 1;
    std::vector<std::uint8_t> rgba8{};
    std::vector<LunarSurfaceMapMip> mips{};
    cubey::procedural::ProceduralArtifactMetadata metadata{};
};

[[nodiscard]] std::uint32_t lunar_surface_map_mip_count(std::uint32_t width, std::uint32_t height);
[[nodiscard]] LunarSurfaceMap
generate_lunar_surface_map(std::uint32_t width = kLunarSurfaceMapWidth,
                           std::uint32_t height = kLunarSurfaceMapHeight);
[[nodiscard]] std::uint64_t lunar_surface_map_hash(std::span<const std::uint8_t> bytes);

} // namespace cubey::render
