#pragma once

#include <cstdint>

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DVelocity {
    float x_m_per_s = 0.0F;
    float y_m_per_s = 0.0F;
};

// Depth-integrated Cartesian momentum (h*u, h*v), in m2/s. It belongs to the
// finite-volume comparison only; River V0 virtual-pipes has separate
// persistent directed-discharge storage.
struct Fluid25DMomentum {
    float x_m2_per_s = 0.0F;
    float y_m2_per_s = 0.0F;
};

struct Fluid25DStepLedger {
    double volume_before_m3 = 0.0;
    double source_volume_m3 = 0.0;
    double sink_volume_m3 = 0.0;
    double boundary_outflow_volume_m3 = 0.0;
    double volume_after_m3 = 0.0;

    [[nodiscard]] double conservation_error_m3() const noexcept {
        return volume_after_m3 - volume_before_m3 - source_volume_m3 + sink_volume_m3 +
               boundary_outflow_volume_m3;
    }
};

} // namespace cubey::projects::fluid::fluid_25d
