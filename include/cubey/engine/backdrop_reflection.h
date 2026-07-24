#pragma once

#include <cubey/core/math.h>

namespace cubey {

struct BackdropReflection {
    math::Vec3 radiance{0.0F, 0.0F, 0.0F};
    float strength = 0.0F;
    float horizon_elevation_sine = 0.0F;
    float horizon_softness = 0.12F;
};

} // namespace cubey
