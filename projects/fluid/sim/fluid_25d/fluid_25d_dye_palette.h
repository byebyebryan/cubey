#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace cubey::projects::fluid::fluid_25d {

// `c = 1` is the configured injected concentration. Below 0.0001 (0.01% of
// injection) the carrier stays clear. These fixed log-scale marks are shared
// with Transport Inspection's legend.
inline constexpr float kFluid25DHillsideMaterialCue = 4.0F;
inline constexpr float kFluid25DHillsideDyePaletteMinimumConcentration = 0.0001F;
inline constexpr float kFluid25DHillsideDyePaletteMaximumConcentration = 1.0F;
inline constexpr float kFluid25DHillsideDyePaletteClearFloorPercent =
    100.0F * kFluid25DHillsideDyePaletteMinimumConcentration /
    kFluid25DHillsideDyePaletteMaximumConcentration;
inline constexpr float kFluid25DHillsideDyePaletteMaximumTint = 0.82F;
// Relative to injected concentration: 0.1%, 1%, 10%, and 100%.
inline constexpr std::array<float, 4> kFluid25DHillsideDyePaletteLegendConcentrations{0.001F, 0.01F,
                                                                                      0.1F, 1.0F};

[[nodiscard]] inline float fluid_25d_hillside_dye_palette_position(float concentration) noexcept {
    if (!(concentration > kFluid25DHillsideDyePaletteMinimumConcentration)) {
        return 0.0F;
    }
    if (concentration >= kFluid25DHillsideDyePaletteMaximumConcentration) {
        return 1.0F;
    }

    const float minimum = kFluid25DHillsideDyePaletteMinimumConcentration;
    const float maximum = kFluid25DHillsideDyePaletteMaximumConcentration;
    return std::clamp(std::log(concentration / minimum) / std::log(maximum / minimum), 0.0F, 1.0F);
}

[[nodiscard]] inline float fluid_25d_hillside_dye_palette_tint(float concentration) noexcept {
    const float position = fluid_25d_hillside_dye_palette_position(concentration);
    const float eased_position = position * position * (3.0F - 2.0F * position);
    return kFluid25DHillsideDyePaletteMaximumTint * eased_position;
}

[[nodiscard]] constexpr bool
fluid_25d_hillside_dye_palette_enabled_for_material_cue(float material_cue) noexcept {
    return material_cue == kFluid25DHillsideMaterialCue;
}

} // namespace cubey::projects::fluid::fluid_25d
