#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace cubey::projects::fluid::fluid_25d {

// Artistic compressed weather response, not a calibrated rain energy model.
// Independent of rain particle visibility, density and falling speed.
[[nodiscard]] inline float
fluid_25d_water_rain_response(std::optional<double> applied_mm_per_hour) noexcept {
    if (!applied_mm_per_hour || !std::isfinite(*applied_mm_per_hour) || *applied_mm_per_hour <= 0.0)
        return 0.0F;
    return float(std::clamp(std::sqrt(*applied_mm_per_hour / 1024.0), 0.0, 1.0));
}

} // namespace cubey::projects::fluid::fluid_25d
