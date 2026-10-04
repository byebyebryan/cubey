#pragma once

#include "fluid_25d_transport_study.h"

#include <array>

namespace cubey::projects::fluid::fluid_25d {

enum class Fluid25DGeometryStudyMethod {
    BsgmSharedGeometry
};

// New, explicitly declared numerical geometry. All heights are relative to
// bed_datum_m(). Raw DEM center samples in scenario() are never replaced.
// Corners: SW, SE, NW, NE. Faces: west, east, south, north. Edge midpoint
// quadrature is used, not an exact integrated 2D hydrostatic pressure rule.
struct Fluid25DGeometryStudyBedCell {
    double mean_bed_m = 0.0;
    std::array<double, 4> face_bed_m{};
    std::array<double, 4> corner_bed_m{};
};

struct Fluid25DGeometryStudyFaceRate {
    std::size_t left_cell = kFluid25DNoCell;
    std::size_t right_cell = kFluid25DNoCell;
    double normal_x = 0.0;
    double normal_y = 0.0;
    double water_rate_m3_per_s = 0.0;
    double tracer_rate_m3_per_s = 0.0;
};

struct Fluid25DGeometryStudyObservation {
    // Unforced semidiscrete RHS, before the film momentum cutoff or forcing.
    std::vector<Fluid25DTransportStudyCell> rhs_per_second{};
    std::vector<Fluid25DGeometryStudyFaceRate> face_rates{};
    double maximum_reconstructed_bed_gap_m = 0.0;
    double maximum_mean_bed_change_m = 0.0;
    std::uint64_t positivity_corrected_axis_pairs = 0;
    std::uint64_t hydrostatic_zero_clipped_faces = 0;
};

// Isolated double-state CPU prototype of Chen/Noelle's BSGM ingredients.
// No production selector, GPU implementation or calibrated hydrology claim.
class Fluid25DGeometryStudy {
  public:
    inline static constexpr double kTargetCfl = 0.225;

    Fluid25DGeometryStudy(
        Fluid25DConfig config, Fluid25DScenarioData scenario,
        Fluid25DTransportStudyBoundary boundary = Fluid25DTransportStudyBoundary::ScenarioFaces,
        std::vector<Fluid25DMomentum> initial_momentum = {},
        std::vector<double> initial_tracer_q_m = {},
        Fluid25DGeometryStudyMethod method = Fluid25DGeometryStudyMethod::BsgmSharedGeometry);

    [[nodiscard]] Fluid25DTracerStepResult step(double source_rate_scale = 1.0,
                                                double source_concentration = 0.0);
    [[nodiscard]] Fluid25DTracerStepResult step_with_dye(double source_rate_scale = 1.0);
    void reset();

    [[nodiscard]] const auto& cells() const noexcept {
        return cells_;
    }
    [[nodiscard]] const auto& config() const noexcept {
        return config_;
    }
    [[nodiscard]] const auto& scenario() const noexcept {
        return scenario_;
    }
    [[nodiscard]] const auto& geometry() const noexcept {
        return geometry_;
    }
    [[nodiscard]] double bed_datum_m() const noexcept {
        return bed_datum_m_;
    }
    [[nodiscard]] auto method() const noexcept {
        return method_;
    }
    [[nodiscard]] const auto& last_face_transfers() const noexcept {
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
    [[nodiscard]] Fluid25DGeometryStudyObservation inspect_transport() const;

  private:
    Fluid25DConfig config_{};
    Fluid25DScenarioData scenario_{};
    Fluid25DTransportStudyBoundary boundary_{};
    Fluid25DGeometryStudyMethod method_{};
    double bed_datum_m_ = 0.0;
    std::vector<Fluid25DGeometryStudyBedCell> geometry_{};
    std::vector<Fluid25DMomentum> initial_momentum_{};
    std::vector<double> initial_tracer_q_m_{};
    std::vector<Fluid25DTransportStudyCell> cells_{};
    std::vector<Fluid25DTransportStudyFaceTransfer> last_face_transfers_{};
    Fluid25DDyeSourceSchedule dye_schedule_{};
    double last_cfl_ = 0.0;
    std::uint64_t last_clipped_faces_ = 0;
};

} // namespace cubey::projects::fluid::fluid_25d
