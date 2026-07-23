#pragma once

#include <cubey/procedural/artifact_cache.h>
#include <cubey/render/atmosphere_night_sky_atlas.h>
#include <cubey/render/lunar_surface_map.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace cubey::render {

enum class AtmosphereAtlasPreparationSource : std::uint32_t {
    Cache = 0U,
    Generated = 1U,
};

struct AtmosphereAtlasCacheDiagnostics {
    AtmosphereAtlasPreparationSource source = AtmosphereAtlasPreparationSource::Generated;
    cubey::procedural::ProceduralArtifactCacheLoadOutcome lookup =
        cubey::procedural::ProceduralArtifactCacheLoadOutcome::Miss;
    bool stored = false;
    double load_milliseconds = 0.0;
    double generation_milliseconds = 0.0;
    double store_milliseconds = 0.0;
    std::filesystem::path path{};
    std::string diagnostic{};
};

struct PreparedNightSkyAtlas {
    NightSkyAtlas atlas{};
    AtmosphereAtlasCacheDiagnostics cache{};
};

struct PreparedLunarSurfaceMap {
    LunarSurfaceMap map{};
    AtmosphereAtlasCacheDiagnostics cache{};
};

[[nodiscard]] PreparedNightSkyAtlas
prepare_night_sky_atlas(cubey::procedural::ProceduralArtifactCache& cache,
                        const NightSkyAtlasConfig& config,
                        std::uint32_t extent = kNightSkyAtlasExtent);
[[nodiscard]] PreparedLunarSurfaceMap
prepare_lunar_surface_map(cubey::procedural::ProceduralArtifactCache& cache,
                          std::uint32_t width = kLunarSurfaceMapWidth,
                          std::uint32_t height = kLunarSurfaceMapHeight);

} // namespace cubey::render
