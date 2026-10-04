#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_scenarios.h"
#include "fluid_25d_solver_state.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

// CPU-only numerical study, never selected by the application. Double state
// isolates discretization accuracy; it does not establish float GPU accuracy.
struct Fluid25DTransportStudyCell {
    double depth_m = 0.0;
    double momentum_x_m2_per_s = 0.0;
    double momentum_y_m2_per_s = 0.0;
    double tracer_q_m = 0.0;
};

enum class Fluid25DTransportStudyBoundary {
    ScenarioFaces,
    PeriodicX
};

enum class Fluid25DTransportStudyMethod {
    ReconstructedHydrostatic,
    FreeSurfaceUpwind
};

// Signed, stage-weighted transfers actually applied during the last successful
// public step. Internal faces are owned once: left -> right or down -> up.
// Boundary faces have right_cell == kFluid25DNoCell and outward normals.
struct Fluid25DTransportStudyFaceTransfer {
    std::size_t left_cell = kFluid25DNoCell;
    std::size_t right_cell = kFluid25DNoCell;
    double normal_x = 0.0;
    double normal_y = 0.0;
    double water_volume_m3 = 0.0;
    double tracer_amount_m3 = 0.0;
};

class Fluid25DTransportStudy {
  public:
    // Conservative study-stage limit, not a proof of positivity for every
    // possible wet/dry reconstruction. Each SSPRK stage is checked.
    inline static constexpr double kTargetCfl = 0.225;
    inline static constexpr double kFallbackAxisCfl = 0.125;
    inline static constexpr double kFallbackDatumPaddingM = 0.01;

    Fluid25DTransportStudy(
        Fluid25DConfig config, Fluid25DScenarioData scenario,
        Fluid25DTransportStudyBoundary boundary = Fluid25DTransportStudyBoundary::ScenarioFaces,
        std::vector<Fluid25DMomentum> initial_momentum = {},
        std::vector<double> initial_tracer_q_m = {},
        Fluid25DTransportStudyMethod method =
            Fluid25DTransportStudyMethod::ReconstructedHydrostatic);

    [[nodiscard]] Fluid25DTracerStepResult step(double source_rate_scale = 1.0,
                                                double source_concentration = 0.0);
    [[nodiscard]] Fluid25DTracerStepResult step_with_dye(double source_rate_scale = 1.0);
    void reset();

    [[nodiscard]] const std::vector<Fluid25DTransportStudyCell>& cells() const noexcept {
        return cells_;
    }
    [[nodiscard]] const Fluid25DConfig& config() const noexcept {
        return config_;
    }
    [[nodiscard]] const Fluid25DScenarioData& scenario() const noexcept {
        return scenario_;
    }
    [[nodiscard]] Fluid25DTransportStudyMethod method() const noexcept {
        return method_;
    }
    [[nodiscard]] const std::vector<Fluid25DTransportStudyFaceTransfer>&
    last_face_transfers() const noexcept {
        return last_face_transfers_;
    }
    [[nodiscard]] double last_cfl_number() const noexcept {
        return last_cfl_;
    }
    [[nodiscard]] std::uint64_t last_clipped_faces() const noexcept {
        return last_clipped_faces_;
    }
    [[nodiscard]] std::uint64_t completed_steps() const noexcept {
        return dye_schedule_.completed_steps();
    }
    [[nodiscard]] double total_water_volume_m3() const;
    [[nodiscard]] double total_tracer_amount_m3() const;

  private:
    Fluid25DConfig config_{};
    Fluid25DScenarioData scenario_{};
    Fluid25DTransportStudyBoundary boundary_{};
    Fluid25DTransportStudyMethod method_{};
    double minimum_bed_m_ = 0.0;
    std::vector<Fluid25DMomentum> initial_momentum_{};
    std::vector<double> initial_tracer_q_m_{};
    std::vector<Fluid25DTransportStudyCell> cells_{};
    std::vector<Fluid25DTransportStudyFaceTransfer> last_face_transfers_{};
    Fluid25DDyeSourceSchedule dye_schedule_{};
    double last_cfl_ = 0.0;
    std::uint64_t last_clipped_faces_ = 0;
};

} // namespace cubey::projects::fluid::fluid_25d
