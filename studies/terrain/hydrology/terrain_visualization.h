#pragma once

#include <cubey/procedural/field_2d.h>

#include <string_view>

namespace cubey::projects::terrain_hydrology_lab {

enum class TerrainFieldDisplayScale {
    Linear,
    Logarithmic,
};

enum class TerrainFieldDisplayPalette {
    Sequential,
    Diverging,
};

struct TerrainFieldDisplaySpec {
    float low = 0.0F;
    float high = 1.0F;
    TerrainFieldDisplayScale scale = TerrainFieldDisplayScale::Linear;
    TerrainFieldDisplayPalette palette = TerrainFieldDisplayPalette::Sequential;
    bool patch_relative = false;
};

[[nodiscard]] TerrainFieldDisplaySpec
terrain_field_display_spec(std::string_view field_name,
                           const cubey::procedural::ScalarField2D& field);
[[nodiscard]] float terrain_field_display_value(float value,
                                                const TerrainFieldDisplaySpec& spec);
[[nodiscard]] std::string_view terrain_field_display_scale_name(TerrainFieldDisplayScale scale);

} // namespace cubey::projects::terrain_hydrology_lab
