#include "terrain_visualization.h"

#include "terrain_patch.h"

#include <cubey/procedural/operators.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] bool unit_field(std::string_view name) {
    return name == kTerrainFieldMountainSupport || name.ends_with("_mask") ||
           name == kTerrainFieldDischargeProxy || name == kTerrainFieldUpliftPotential ||
           name == kTerrainFieldMacroMass;
}

} // namespace

TerrainFieldDisplaySpec terrain_field_display_spec(std::string_view field_name,
                                                   const cubey::procedural::ScalarField2D& field) {
    if (field_name == kTerrainFieldSourceHeightM || field_name == kTerrainFieldHeightM ||
        field_name == kTerrainFieldRoutingSurfaceM) {
        return {.low = 0.0F, .high = 2500.0F};
    }
    if (field_name == kTerrainFieldSlope) {
        return {.low = 0.0F, .high = 2.5F};
    }
    if (field_name == kTerrainFieldCurvature) {
        return {
            .low = -30.0F,
            .high = 30.0F,
            .palette = TerrainFieldDisplayPalette::Diverging,
        };
    }
    if (field_name == kTerrainFieldLocalReliefM) {
        return {.low = 0.0F, .high = 600.0F};
    }
    if (field_name == kTerrainFieldBaseReliefM) {
        return {
            .low = -750.0F,
            .high = 750.0F,
            .palette = TerrainFieldDisplayPalette::Diverging,
        };
    }
    if (field_name == kTerrainFieldRoutingFillDeltaM) {
        return {.low = 0.0F, .high = 400.0F};
    }
    if (field_name == kTerrainFieldFlowDirectionX || field_name == kTerrainFieldFlowDirectionZ) {
        return {
            .low = -1.0F,
            .high = 1.0F,
            .palette = TerrainFieldDisplayPalette::Diverging,
        };
    }
    if (field_name == kTerrainFieldContributingAreaM2) {
        const float cell_area = field.desc().cell_size * field.desc().cell_size;
        return {
            .low = std::max(cell_area, 1.0F),
            .high = 100'000'000.0F,
            .scale = TerrainFieldDisplayScale::Logarithmic,
        };
    }
    if (field_name == kTerrainFieldStreamOrder) {
        return {.low = 1.0F, .high = 10.0F};
    }
    if (unit_field(field_name)) {
        return {.low = 0.0F, .high = 1.0F};
    }

    const cubey::procedural::ScalarFieldStats stats = field.summarize();
    return {
        .low = stats.min,
        .high = stats.max,
        .patch_relative = true,
    };
}

float terrain_field_display_value(float value, const TerrainFieldDisplaySpec& spec) {
    if (!(spec.high > spec.low)) {
        return 0.5F;
    }
    if (spec.scale == TerrainFieldDisplayScale::Logarithmic) {
        const float low = std::max(spec.low, 0.000001F);
        const float clamped = std::clamp(value, low, spec.high);
        return cubey::procedural::saturate(std::log(clamped / low) / std::log(spec.high / low));
    }
    return cubey::procedural::saturate((value - spec.low) / (spec.high - spec.low));
}

std::string_view terrain_field_display_scale_name(TerrainFieldDisplayScale scale) {
    switch (scale) {
    case TerrainFieldDisplayScale::Linear:
        return "linear";
    case TerrainFieldDisplayScale::Logarithmic:
        return "logarithmic";
    }
    return "unknown";
}

} // namespace cubey::projects::terrain
