#pragma once
#include <cubey/asset/terrain_site.h>
#include <cubey/asset/terrain_water_capture.h>

namespace cubey::asset {
// Optional sidecar; the pinned site package remains immutable and owns its source.
[[nodiscard]] std::filesystem::path
load_terrain_site_water_binding(const std::filesystem::path& binding,
                                const std::filesystem::path& site_manifest,
                                const TerrainSite& site);
void validate_terrain_site_water_capture(const TerrainWaterCapture& capture,
                                         const TerrainSite& site);
} // namespace cubey::asset
