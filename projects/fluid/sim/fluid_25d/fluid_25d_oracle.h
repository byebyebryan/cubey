#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_scenarios.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

enum class Fluid25DFace : std::uint32_t {
    Left = 0,
    Right = 1,
    Down = 2,
    Up = 3,
};

using Fluid25DFaceFlux = std::array<float, 4>;

struct Fluid25DVelocity {
    float x_m_per_s = 0.0F;
    float y_m_per_s = 0.0F;
};

struct Fluid25DStepLedger {
    double volume_before_m3 = 0.0;
    double source_volume_m3 = 0.0;
    double sink_volume_m3 = 0.0;
    double volume_after_m3 = 0.0;

    [[nodiscard]] double conservation_error_m3() const noexcept {
        return volume_after_m3 - volume_before_m3 - source_volume_m3 + sink_volume_m3;
    }
};

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

    [[nodiscard]] Fluid25DStepLedger step();
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
    [[nodiscard]] Fluid25DStepLedger step_substep(float delta_seconds);
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
