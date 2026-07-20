#pragma once

#include <cstdint>

namespace cubey::projects::terrain {

enum class TerrainPlacementMode : std::uint8_t {
    Selected,
    RawCenter,
    RawSample,
};

} // namespace cubey::projects::terrain
