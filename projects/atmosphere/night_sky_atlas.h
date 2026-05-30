#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cubey::projects::atmosphere {

inline constexpr std::uint32_t kNightSkyAtlasExtent = 512;
inline constexpr char kNoaaMilkyWayPanoramaSha256[] =
    "7ec4ac3afc42c48651f937f8e89bbc6354386867e8d5bc7a745e12fb5a8480c1";
inline constexpr char kNasaDeepStarMap8KSha256[] =
    "ec28e645863d55d4c0513a07fc846eaf06fc4f4b2246e4a6b10535f990309360";

enum class NightSkyAtlasSource : std::uint32_t {
    Data = 0,
    Procedural = 1,
};

struct NightSkyAtlasMip {
    std::uint32_t extent = 1;
    std::size_t byte_offset = 0;
    std::size_t byte_count = 0;
};

struct NightSkyAtlasConfig {
    NightSkyAtlasSource source = NightSkyAtlasSource::Procedural;
    std::optional<std::filesystem::path> data_path{};
    float procedural_variation = 0.0F;
};

struct NightSkyAtlas {
    std::uint32_t extent = kNightSkyAtlasExtent;
    std::uint32_t mip_levels = 1;
    std::vector<float> rgba32f{};
    std::vector<NightSkyAtlasMip> mips{};
    NightSkyAtlasSource source = NightSkyAtlasSource::Procedural;
};

[[nodiscard]] std::uint32_t night_sky_atlas_mip_count(std::uint32_t extent);
[[nodiscard]] NightSkyAtlas generate_night_sky_atlas(
    const NightSkyAtlasConfig& config, std::uint32_t extent = kNightSkyAtlasExtent);
[[nodiscard]] std::uint64_t night_sky_atlas_hash(std::span<const float> values);

} // namespace cubey::projects::atmosphere
