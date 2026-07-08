#pragma once

#include <cstdint>

namespace cubey::projects::terrain_ref {

inline constexpr float kShadertoyAlpineReferenceWaterHeightM = 260.0F;
inline constexpr float kShadertoyDunesReferenceWaterHeightM = -1000.0F;
inline constexpr float kShadertoyLakeBasinReferenceWaterHeightM = 165.0F;

[[nodiscard]] float shadertoy_alpine_reference_height(float world_x, float world_z,
                                                      std::uint64_t seed);
[[nodiscard]] float shadertoy_dunes_reference_height(float world_x, float world_z,
                                                     std::uint64_t seed);
[[nodiscard]] float shadertoy_lake_basin_reference_height(float world_x, float world_z,
                                                          std::uint64_t seed);

} // namespace cubey::projects::terrain_ref
