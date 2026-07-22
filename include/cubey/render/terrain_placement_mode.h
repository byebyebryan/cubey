#pragma once

#include <cstdint>

namespace cubey::render {

enum class TerrainPlacementMode : std::uint8_t {
    Selected,
    RawCenter,
    RawSample,
};

} // namespace cubey::render
