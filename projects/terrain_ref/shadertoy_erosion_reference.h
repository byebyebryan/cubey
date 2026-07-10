#pragma once

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain_ref {

inline constexpr std::string_view kTerrainRefRecipeShadertoyErosionFilter =
    "shadertoy-erosion-filter";
inline constexpr float kShadertoyErosionReferenceWaterHeightM = -100000.0F;

enum class ShadertoyErosionReferenceSurface : std::uint8_t {
    Base,
    Filtered,
};

struct ShadertoyErosionReferenceSample {
    float base_height_m = 0.0F;
    float filtered_height_m = 0.0F;
    float erosion_delta_m = 0.0F;
    float gradient_x = 0.0F;
    float gradient_z = 0.0F;
};

[[nodiscard]] ShadertoyErosionReferenceSample
shadertoy_erosion_reference_sample(float world_x, float world_z, std::uint64_t seed);
[[nodiscard]] float shadertoy_erosion_reference_height(
    float world_x, float world_z, std::uint64_t seed,
    ShadertoyErosionReferenceSurface surface = ShadertoyErosionReferenceSurface::Filtered);
[[nodiscard]] float shadertoy_erosion_reference_normal_cos_v(float world_x, float world_z,
                                                             std::uint64_t seed);

} // namespace cubey::projects::terrain_ref
