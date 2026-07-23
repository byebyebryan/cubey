#pragma once

#include <cubey/procedural/artifact_cache.h>
#include <cubey/procedural/artifact_metadata.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cubey::render {

inline constexpr std::uint32_t kNightSkyAtlasExtent = 512;
inline constexpr std::string_view kNightSkyAtlasFormulaVersion = "atmosphere-night-sky-atlas-v2";

enum class NightSkyLayerView : std::uint32_t {
    Final = 0,
    StellarEmission = 1,
    DustTau = 2,
    StarClouds = 3,
    HiiEmission = 4,
    Speckles = 5,
};

inline constexpr std::array<NightSkyLayerView, 6> kNightSkyLayerViews{
    NightSkyLayerView::Final,      NightSkyLayerView::StellarEmission, NightSkyLayerView::DustTau,
    NightSkyLayerView::StarClouds, NightSkyLayerView::HiiEmission,     NightSkyLayerView::Speckles,
};

struct NightSkyAtlasMip {
    std::uint32_t extent = 1;
    std::size_t byte_offset = 0;
    std::size_t byte_count = 0;
};

struct NightSkyAtlasConfig {
    float procedural_variation = 0.0F;
    NightSkyLayerView layer = NightSkyLayerView::Final;
};

struct NightSkyAtlas {
    std::uint32_t extent = kNightSkyAtlasExtent;
    std::uint32_t mip_levels = 1;
    std::vector<float> rgba32f{};
    std::vector<NightSkyAtlasMip> mips{};
    NightSkyLayerView layer = NightSkyLayerView::Final;
    cubey::procedural::ProceduralArtifactMetadata metadata{};
};

[[nodiscard]] std::uint32_t night_sky_atlas_mip_count(std::uint32_t extent);
[[nodiscard]] cubey::procedural::ProceduralArtifactRecipe
night_sky_atlas_recipe(const NightSkyAtlasConfig& config,
                       std::uint32_t extent = kNightSkyAtlasExtent);
[[nodiscard]] NightSkyAtlas generate_night_sky_atlas(const NightSkyAtlasConfig& config,
                                                     std::uint32_t extent = kNightSkyAtlasExtent);
[[nodiscard]] std::uint64_t night_sky_atlas_hash(std::span<const float> values);

} // namespace cubey::render
