#include "fluid_25d_bank_comparison.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {

Fluid25DBankView fluid_25d_bank_view(std::string_view name) {
    if (name == "reference")
        return Fluid25DBankView::Reference;
    if (name == "bspline-2x")
        return Fluid25DBankView::Bspline2x;
    if (name == "marching-squares")
        return Fluid25DBankView::MarchingSquares;
    throw std::invalid_argument("unknown bank view");
}

const char* fluid_25d_bank_view_name(Fluid25DBankView view) {
    switch (view) {
    case Fluid25DBankView::Reference:
        return "reference";
    case Fluid25DBankView::Bspline2x:
        return "bspline-2x";
    case Fluid25DBankView::MarchingSquares:
        return "marching-squares";
    }
    throw std::invalid_argument("unknown bank view");
}

void apply_fluid_25d_bank_view(Fluid25DCatchmentRenderOptions& render, Fluid25DBankView view,
                               bool matching_mask_ready) {
    // Validate before changing any state.
    (void)fluid_25d_bank_view_name(view);
    if (view == Fluid25DBankView::MarchingSquares && !matching_mask_ready)
        throw std::invalid_argument("marching-squares coverage requires a matching recorded mask");
    render.native_bilinear_water = false;
    render.native_bspline_surface = view == Fluid25DBankView::Bspline2x;
    render.native_display_coverage = view == Fluid25DBankView::MarchingSquares;
    if (view == Fluid25DBankView::Bspline2x)
        render.native_surface_subdivision = 2U;
}

Fluid25DBankComparisonWindow::Fluid25DBankComparisonWindow(std::span<const double> source_times,
                                                           std::span<const double> mask_times) {
    const auto ordered = [](std::span<const double> times) {
        double previous = -1.0;
        for (double time : times) {
            if (!std::isfinite(time) || time < 0.0 || time <= previous)
                return false;
            previous = time;
        }
        return !times.empty();
    };
    if (!ordered(source_times) || !ordered(mask_times) || mask_times.size() < 2U ||
        mask_times.size() > 16U)
        throw std::invalid_argument(
            "bank comparison requires 2..16 ordered, consecutive saved masks");
    const auto first =
        std::lower_bound(source_times.begin(), source_times.end(), mask_times.front());
    const auto remaining = static_cast<std::size_t>(source_times.end() - first);
    if (remaining < mask_times.size() || !std::equal(mask_times.begin(), mask_times.end(), first))
        throw std::invalid_argument(
            "bank comparison mask times must be a consecutive native window");
    first_s_ = mask_times.front();
    last_saved_s_ = mask_times.back();
    count_ = mask_times.size();
    const auto next = first + static_cast<std::ptrdiff_t>(count_);
    // Hold the final masked state until just before the next unmasked frame.
    // If the window reaches the recording's end, do not extend recorded time.
    exclusive_end_s_ = next == source_times.end() ? last_saved_s_ : *next;
    end_s_ =
        next == source_times.end() ? last_saved_s_ : std::nextafter(exclusive_end_s_, first_s_);
}

bool Fluid25DBankComparisonWindow::contains(double time_s) const noexcept {
    return std::isfinite(time_s) && time_s >= first_s_ && time_s <= end_s_;
}
bool Fluid25DBankComparisonWindow::at_end(double time_s) const noexcept {
    return time_s >= end_s_;
}

Fluid25DBankWindowTime Fluid25DBankComparisonWindow::map(double requested_s, bool loop) const {
    if (!std::isfinite(requested_s))
        throw std::invalid_argument("bank window time must be finite");
    if (requested_s >= end_s_) {
        if (loop)
            return {first_s_ + std::fmod(std::max(0.0, requested_s - exclusive_end_s_),
                                         exclusive_end_s_ - first_s_),
                    false, true};
        return {end_s_, true, false};
    }
    return {std::max(first_s_, requested_s), false, false};
}

} // namespace cubey::projects::fluid::fluid_25d
