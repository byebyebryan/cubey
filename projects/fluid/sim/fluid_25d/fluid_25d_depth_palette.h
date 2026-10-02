#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::array<float, 4> kFluid25DHillsideDepthLegendM{0.01F, 0.1F, 1.0F, 10.0F};

// Fixed three-decade scale, mirrored in water.frag; never frame-normalized.
inline float fluid_25d_hillside_depth_palette_position(float depth_m) {
    if (!std::isfinite(depth_m) || depth_m <= 0.01F)
        return 0.0F;
    return std::clamp(std::log(depth_m / 0.01F) / std::log(1000.0F), 0.0F, 1.0F);
}

} // namespace cubey::projects::fluid::fluid_25d
