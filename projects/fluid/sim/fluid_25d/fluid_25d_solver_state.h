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

// Conservative tracer accounting uses the same physical-volume units as the
// water ledger, but remains deliberately separate: tracer is a depth
// equivalent q=h*c, so its integrated amount is q*cell area (m3 equivalent).
struct Fluid25DTracerStepLedger {
    double amount_before_m3 = 0.0;
    double source_amount_m3 = 0.0;
    double sink_amount_m3 = 0.0;
    double boundary_outflow_amount_m3 = 0.0;
    double amount_after_m3 = 0.0;

    [[nodiscard]] double conservation_error_m3() const noexcept {
        return amount_after_m3 - amount_before_m3 - source_amount_m3 + sink_amount_m3 +
               boundary_outflow_amount_m3;
    }
};

struct Fluid25DTracerStepResult {
    Fluid25DStepLedger water{};
    Fluid25DTracerStepLedger tracer{};
};

} // namespace cubey::projects::fluid::fluid_25d
