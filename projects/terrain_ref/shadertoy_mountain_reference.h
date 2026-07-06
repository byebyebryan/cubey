#pragma once

#include <cubey/procedural/field_2d.h>

#include <cstdint>

namespace cubey::projects::terrain_ref {

inline constexpr float kShadertoyMountainReferenceWaterHeightM = 180.0F;

enum class ShadertoyMountainReferenceDetail : std::uint8_t {
    Geometry,
    Surface,
};

[[nodiscard]] float
shadertoy_mountain_reference_height(float world_x, float world_z, std::uint64_t seed,
                                    ShadertoyMountainReferenceDetail detail =
                                        ShadertoyMountainReferenceDetail::Geometry);
[[nodiscard]] cubey::procedural::ScalarField2D
shadertoy_mountain_reference_height_field(cubey::procedural::Grid2DDesc desc,
                                          std::uint64_t seed);
[[nodiscard]] float shadertoy_mountain_reference_normal_cos_v(float world_x, float world_z,
                                                              std::uint64_t seed);

} // namespace cubey::projects::terrain_ref
