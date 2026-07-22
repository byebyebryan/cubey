#pragma once

#include <cstdint>

namespace cubey::render {

enum class TerrainBackdropDebugView : std::uint8_t {
    Surface = 0,
    Height = 1,
    Slope = 3,
    Clay = 6,
    Normal = 10,
    MaterialWeights = 11,
    AmbientVisibility = 12,
    ProjectedEdge = 14,
    MaterialAlbedo = 15,
    MaterialNormal = 16,
    MaterialRoughness = 18,
    SunVisibility = 19,
    ClassificationNormal = 21,
    Vegetation = 22,
    Moisture = 23,
    AmbientLighting = 24,
    DirectLighting = 25,
    StageOwnership = 27,
};

enum class TerrainBackdropMaterialMode : std::uint8_t {
    Flat,
    FilteredDetail,
};

} // namespace cubey::render
