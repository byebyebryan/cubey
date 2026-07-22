#pragma once

#include <cstdint>

namespace cubey::terrain {

enum class TerrainPlacementMode : std::uint8_t {
    Selected,
    RawCenter,
    RawSample,
};

} // namespace cubey::terrain
