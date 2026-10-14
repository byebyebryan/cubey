#pragma once
#include <string>

namespace cubey::asset {
struct TerrainLandscapePreset;
struct TerrainWaterCapture;
} // namespace cubey::asset

namespace cubey::projects::terrain {
struct TerrainProjectConfig;
struct TerrainLandscapePresentation {
    std::string title, mode, water_coverage, terrain_coverage, provenance;
    bool has_water = false;
};
// Labels derive only from the admitted selection/capture, not GUI state or
// guessed hydrology. Also logged headlessly for first-use contract checks.
[[nodiscard]] std::string
terrain_landscape_selection_label(const cubey::asset::TerrainLandscapePreset& preset);
[[nodiscard]] TerrainLandscapePresentation
terrain_landscape_presentation(const TerrainProjectConfig& config,
                               const cubey::asset::TerrainWaterCapture& capture);
} // namespace cubey::projects::terrain
