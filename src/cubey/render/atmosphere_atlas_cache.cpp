#include <cubey/render/atmosphere_atlas_cache.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::render {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsed_milliseconds(Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

void append_diagnostic(std::string& destination, std::string value) {
    if (value.empty()) {
        return;
    }
    if (!destination.empty()) {
        destination += "; ";
    }
    destination += std::move(value);
}

[[nodiscard]] std::span<const std::uint8_t> night_sky_bytes(const NightSkyAtlas& atlas) {
    return {
        reinterpret_cast<const std::uint8_t*>(atlas.rgba32f.data()),
        atlas.rgba32f.size() * sizeof(float),
    };
}

[[nodiscard]] NightSkyAtlas
decode_night_sky_atlas(const cubey::procedural::CachedProceduralArtifact& artifact,
                       const NightSkyAtlasConfig& config, std::uint32_t extent) {
    if (artifact.payload.size() % sizeof(float) != 0U) {
        throw std::runtime_error("cached night-sky atlas payload is not float-aligned");
    }
    NightSkyAtlas atlas{
        .extent = extent,
        .mip_levels = night_sky_atlas_mip_count(extent),
        .layer = config.layer,
        .metadata = artifact.metadata,
    };
    atlas.rgba32f.resize(artifact.payload.size() / sizeof(float));
    std::memcpy(atlas.rgba32f.data(), artifact.payload.data(), artifact.payload.size());
    atlas.mips.reserve(atlas.mip_levels);
    std::size_t byte_offset = 0U;
    for (std::uint32_t mip = 0U; mip < atlas.mip_levels; ++mip) {
        const std::uint32_t mip_extent = std::max(1U, extent >> mip);
        const std::size_t byte_count =
            static_cast<std::size_t>(mip_extent) * mip_extent * 6U * 4U * sizeof(float);
        atlas.mips.push_back({mip_extent, byte_offset, byte_count});
        byte_offset += byte_count;
    }
    if (byte_offset != artifact.payload.size() ||
        night_sky_atlas_hash(atlas.rgba32f) != atlas.metadata.content_hash) {
        throw std::runtime_error("cached night-sky atlas metadata does not match its payload");
    }
    return atlas;
}

[[nodiscard]] LunarSurfaceMap
decode_lunar_surface_map(const cubey::procedural::CachedProceduralArtifact& artifact,
                         std::uint32_t width, std::uint32_t height) {
    LunarSurfaceMap map{
        .width = width,
        .height = height,
        .mip_levels = lunar_surface_map_mip_count(width, height),
        .rgba8 = artifact.payload,
        .metadata = artifact.metadata,
    };
    map.mips.reserve(map.mip_levels);
    std::size_t byte_offset = 0U;
    std::uint32_t mip_width = width;
    std::uint32_t mip_height = height;
    for (std::uint32_t mip = 0U; mip < map.mip_levels; ++mip) {
        const std::size_t byte_count = static_cast<std::size_t>(mip_width) * mip_height * 4U;
        map.mips.push_back({mip_width, mip_height, byte_offset, byte_count});
        byte_offset += byte_count;
        mip_width = std::max(mip_width / 2U, 1U);
        mip_height = std::max(mip_height / 2U, 1U);
    }
    if (byte_offset != map.rgba8.size() ||
        lunar_surface_map_hash(map.rgba8) != map.metadata.content_hash) {
        throw std::runtime_error("cached lunar surface metadata does not match its payload");
    }
    return map;
}

void reject_typed_entry(const std::filesystem::path& path, std::string& diagnostic,
                        std::string message) {
    append_diagnostic(diagnostic, std::move(message));
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        append_diagnostic(diagnostic, "failed to remove rejected cache entry: " + error.message());
    }
}

} // namespace

PreparedNightSkyAtlas prepare_night_sky_atlas(cubey::procedural::ProceduralArtifactCache& cache,
                                              const NightSkyAtlasConfig& config,
                                              std::uint32_t extent) {
    const cubey::procedural::ProceduralArtifactRecipe recipe =
        night_sky_atlas_recipe(config, extent);
    AtmosphereAtlasCacheDiagnostics diagnostics;
    const Clock::time_point load_started = Clock::now();
    cubey::procedural::ProceduralArtifactCacheLoadResult loaded = cache.load(recipe);
    diagnostics.load_milliseconds = elapsed_milliseconds(load_started);
    diagnostics.lookup = loaded.outcome;
    diagnostics.path = loaded.path;
    append_diagnostic(diagnostics.diagnostic, std::move(loaded.diagnostic));
    if (loaded.artifact.has_value()) {
        try {
            NightSkyAtlas atlas = decode_night_sky_atlas(loaded.artifact.value(), config, extent);
            diagnostics.source = AtmosphereAtlasPreparationSource::Cache;
            return {.atlas = std::move(atlas), .cache = std::move(diagnostics)};
        } catch (const std::exception& error) {
            diagnostics.lookup = cubey::procedural::ProceduralArtifactCacheLoadOutcome::Rejected;
            reject_typed_entry(loaded.path, diagnostics.diagnostic, error.what());
        }
    }

    const Clock::time_point generation_started = Clock::now();
    NightSkyAtlas atlas = generate_night_sky_atlas(config, extent);
    diagnostics.generation_milliseconds = elapsed_milliseconds(generation_started);
    const Clock::time_point store_started = Clock::now();
    const cubey::procedural::ProceduralArtifactCacheStoreResult stored =
        cache.store(recipe, atlas.metadata, night_sky_bytes(atlas));
    diagnostics.store_milliseconds = elapsed_milliseconds(store_started);
    diagnostics.stored = stored.stored;
    diagnostics.path = stored.path;
    append_diagnostic(diagnostics.diagnostic, stored.diagnostic);
    return {.atlas = std::move(atlas), .cache = std::move(diagnostics)};
}

PreparedLunarSurfaceMap prepare_lunar_surface_map(cubey::procedural::ProceduralArtifactCache& cache,
                                                  std::uint32_t width, std::uint32_t height) {
    const cubey::procedural::ProceduralArtifactRecipe recipe =
        lunar_surface_map_recipe(width, height);
    AtmosphereAtlasCacheDiagnostics diagnostics;
    const Clock::time_point load_started = Clock::now();
    cubey::procedural::ProceduralArtifactCacheLoadResult loaded = cache.load(recipe);
    diagnostics.load_milliseconds = elapsed_milliseconds(load_started);
    diagnostics.lookup = loaded.outcome;
    diagnostics.path = loaded.path;
    append_diagnostic(diagnostics.diagnostic, std::move(loaded.diagnostic));
    if (loaded.artifact.has_value()) {
        try {
            LunarSurfaceMap map = decode_lunar_surface_map(loaded.artifact.value(), width, height);
            diagnostics.source = AtmosphereAtlasPreparationSource::Cache;
            return {.map = std::move(map), .cache = std::move(diagnostics)};
        } catch (const std::exception& error) {
            diagnostics.lookup = cubey::procedural::ProceduralArtifactCacheLoadOutcome::Rejected;
            reject_typed_entry(loaded.path, diagnostics.diagnostic, error.what());
        }
    }

    const Clock::time_point generation_started = Clock::now();
    LunarSurfaceMap map = generate_lunar_surface_map(width, height);
    diagnostics.generation_milliseconds = elapsed_milliseconds(generation_started);
    const Clock::time_point store_started = Clock::now();
    const cubey::procedural::ProceduralArtifactCacheStoreResult stored =
        cache.store(recipe, map.metadata, map.rgba8);
    diagnostics.store_milliseconds = elapsed_milliseconds(store_started);
    diagnostics.stored = stored.stored;
    diagnostics.path = stored.path;
    append_diagnostic(diagnostics.diagnostic, stored.diagnostic);
    return {.map = std::move(map), .cache = std::move(diagnostics)};
}

} // namespace cubey::render
