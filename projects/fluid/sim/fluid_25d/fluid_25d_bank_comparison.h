#pragma once

#include "fluid_25d_presentation.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace cubey::projects::fluid::fluid_25d {

enum class Fluid25DBankView {
    Reference,
    Bspline2x,
    MarchingSquares
};
[[nodiscard]] Fluid25DBankView fluid_25d_bank_view(std::string_view name);
[[nodiscard]] const char* fluid_25d_bank_view_name(Fluid25DBankView view);
// Only reconstruction flags are changed. Time, camera, fields and cue histories
// belong to the viewer and are deliberately not arguments to this operation.
void apply_fluid_25d_bank_view(Fluid25DCatchmentRenderOptions& render, Fluid25DBankView view,
                               bool matching_mask_ready);

struct Fluid25DBankWindowTime {
    double time_s;
    bool at_end = false;
    bool looped = false;
};

// Completed-recording comparison only. Masks must cover one consecutive native
// frame window; gaps are never filled with stale masks or invented fields.
class Fluid25DBankComparisonWindow {
  public:
    Fluid25DBankComparisonWindow(std::span<const double> source_times,
                                 std::span<const double> mask_times);
    [[nodiscard]] double first_s() const noexcept {
        return first_s_;
    }
    [[nodiscard]] double last_saved_s() const noexcept {
        return last_saved_s_;
    }
    [[nodiscard]] double end_s() const noexcept {
        return end_s_;
    }
    [[nodiscard]] std::size_t frame_count() const noexcept {
        return count_;
    }
    [[nodiscard]] bool contains(double time_s) const noexcept;
    [[nodiscard]] bool at_end(double time_s) const noexcept;
    [[nodiscard]] Fluid25DBankWindowTime map(double requested_s, bool loop) const;

  private:
    double first_s_ = 0.0, last_saved_s_ = 0.0, end_s_ = 0.0, exclusive_end_s_ = 0.0;
    std::size_t count_ = 0;
};

} // namespace cubey::projects::fluid::fluid_25d
