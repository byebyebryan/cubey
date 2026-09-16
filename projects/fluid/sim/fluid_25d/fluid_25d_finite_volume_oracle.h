#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_scenarios.h"
#include "fluid_25d_solver_state.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

// Isolated CPU-only reference for a first-order Saint-Venant finite-volume
// comparison. It intentionally shares only immutable scenario/config fields
// and accounting types with River V0; it never changes virtual-pipe state or
// application/GPU behavior.
class Fluid25DFiniteVolumeOracle {
  public:
    // This two-dimensional first-order Rusanov update rejects a substep above
    // the conservative target. 0.5 is the mathematical ceiling for this
    // unsplit Cartesian form; retaining 0.45 leaves float/wet-dry margin.
    inline static constexpr float kTargetCfl = 0.45F;
    inline static constexpr float kHardCflCeiling = 0.50F;

    Fluid25DFiniteVolumeOracle(Fluid25DConfig config, Fluid25DScenarioData scenario);

    // `source_rate_scale` has the same public fixed-step meaning as the
    // virtual-pipes oracle: it multiplies sources only, not sinks or open
    // outflow faces.
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
    [[nodiscard]] const std::vector<Fluid25DMomentum>& momentum_m2_per_s() const noexcept {
        return momentum_m2_per_s_;
    }
    [[nodiscard]] const std::vector<Fluid25DVelocity>& velocity_m_per_s() const noexcept {
        return velocity_m_per_s_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& wet_mask() const noexcept {
        return wet_mask_;
    }
    [[nodiscard]] float last_cfl_number() const noexcept {
        return last_cfl_number_;
    }
    [[nodiscard]] double total_water_volume_m3() const;

  private:
    struct TransportDelta {
        double depth_m_per_s = 0.0;
        double momentum_x_m2_per_s2 = 0.0;
        double momentum_y_m2_per_s2 = 0.0;
    };

    [[nodiscard]] Fluid25DStepLedger step_substep(float delta_seconds, float source_rate_scale);
    void derive_velocity_and_wet_state();

    Fluid25DConfig config_{};
    Fluid25DScenarioData scenario_{};
    std::vector<float> water_depth_m_{};
    std::vector<float> source_sink_depth_m_{};
    std::vector<float> next_water_depth_m_{};
    std::vector<Fluid25DMomentum> momentum_m2_per_s_{};
    std::vector<Fluid25DMomentum> source_sink_momentum_m2_per_s_{};
    std::vector<Fluid25DMomentum> next_momentum_m2_per_s_{};
    std::vector<TransportDelta> transport_delta_{};
    std::vector<Fluid25DVelocity> velocity_m_per_s_{};
    std::vector<std::uint8_t> wet_mask_{};
    float last_cfl_number_ = 0.0F;
};

} // namespace cubey::projects::fluid::fluid_25d
