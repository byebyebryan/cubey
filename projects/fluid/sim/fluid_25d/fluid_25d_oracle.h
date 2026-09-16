#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_scenarios.h"
#include "fluid_25d_solver_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

using Fluid25DFaceFlux = std::array<float, 4>;

// A small deterministic CPU reference for the River V0 virtual-pipes scheme.
// Each cell owns four persistent outgoing discharge rates (m3/s).  Each rate
// retains damped prior flow and integrates the current free-surface difference;
// it is then limited against the cell's available volume for the current
// substep and gathered by the neighboring cell.  The implementation is
// intentionally project-local; it is an oracle for future GPU work rather than
// a shared simulation framework.
class Fluid25DOracle {
  public:
    Fluid25DOracle(Fluid25DConfig config, Fluid25DScenarioData scenario);

    // `source_rate_scale` multiplies only source fields for this public fixed
    // step. Sinks and outflow boundaries retain their physical rates.
    [[nodiscard]] Fluid25DStepLedger step(float source_rate_scale = 1.0F);
    void reset();

    [[nodiscard]] const Fluid25DConfig& config() const noexcept {
        return config_;
    }
    [[nodiscard]] const Fluid25DScenarioData& scenario() const noexcept {
        return scenario_;
    }
    [[nodiscard]] const std::vector<float>& terrain_height_m() const noexcept {
        return scenario_.terrain_height_m;
    }
    [[nodiscard]] const std::vector<float>& water_depth_m() const noexcept {
        return water_depth_m_;
    }
    [[nodiscard]] const std::vector<Fluid25DFaceFlux>& outgoing_flux_m3_per_s() const noexcept {
        return outgoing_flux_m3_per_s_;
    }
    [[nodiscard]] const std::vector<Fluid25DVelocity>& velocity_m_per_s() const noexcept {
        return velocity_m_per_s_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& wet_mask() const noexcept {
        return wet_mask_;
    }
    [[nodiscard]] double total_water_volume_m3() const;

  private:
    [[nodiscard]] Fluid25DStepLedger step_substep(float delta_seconds, float source_rate_scale);
    void derive_velocity_and_wet_state();

    Fluid25DConfig config_{};
    Fluid25DScenarioData scenario_{};
    std::vector<float> water_depth_m_{};
    std::vector<float> next_water_depth_m_{};
    std::vector<Fluid25DFaceFlux> outgoing_flux_m3_per_s_{};
    std::vector<Fluid25DVelocity> velocity_m_per_s_{};
    std::vector<std::uint8_t> wet_mask_{};
};

} // namespace cubey::projects::fluid::fluid_25d
